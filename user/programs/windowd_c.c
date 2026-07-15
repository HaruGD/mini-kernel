#include <os64/os64.h>

#include "windowd/window_compositor.h"
#include "windowd/window_input_router.h"
#include "windowd/window_protocol.h"
#include "windowd/window_state.h"

#define WINDOW_FRAME_DEADLINE_TICKS 10u
#define WINDOW_PRESENT_TIMEOUT_TICKS 50u

static WindowSingleState window_state;
static WindowInputRouter input_router;
static OsProcessIdentity display_owner;
static OsGraphicsInfo display_info;
static OsHandle composite_surface;
static uint32_t* composite_pixels;
static OsGraphicsSurfaceHandleInfo composite_info;
static OsHandle client_surface;
static const uint32_t* client_pixels;
static OsGraphicsSurfaceHandleInfo client_info;
static uint32_t next_frame_generation;
static uint32_t pending_full_frame;

static int identity_equal(OsProcessIdentity left, OsProcessIdentity right) {
    return left.pid != 0 && left.pid == right.pid && left.generation != 0 &&
           left.generation == right.generation;
}

static void close_handle(OsHandle handle) {
    if (handle != 0) {
        os_handle_close(handle);
    }
}

static void close_message_handles(const OsIpcMessageV2* message) {
    if (message == 0) {
        return;
    }
    for (uint32_t i = 0; i < message->handle_count &&
                         i < OS_IPC_V2_MAX_HANDLES; i++) {
        close_handle(message->handles[i]);
    }
}

static void release_client_surface(void) {
    if (client_pixels != 0 && client_surface != 0) {
        os_surface_unmap(client_surface, (void*)client_pixels);
    }
    client_pixels = 0;
    close_handle(client_surface);
    client_surface = 0;
    os_memset(&client_info, 0, sizeof(client_info));
}

static void release_composite_surface(void) {
    if (composite_pixels != 0 && composite_surface != 0) {
        os_surface_unmap(composite_surface, composite_pixels);
    }
    composite_pixels = 0;
    close_handle(composite_surface);
    composite_surface = 0;
}

static int query_display(OsProcessIdentity* identity, OsGraphicsInfo* info) {
    if (os_service_find_owner_identity("display", identity) < 0) {
        return 0;
    }
    OsServiceQueryRequest request;
    request.size = sizeof(request);
    request.command = OS_SERVICE_QUERY_DISPLAY_INFO;
    request.flags = 0;
    request.request_id = os_msg_next_request_id();

    OsIpcMessage message;
    os_msg_init(&message, OS_IPC_MESSAGE_REQUEST);
    message.flags = OS_IPC_FLAG_REQUEST_REPLY;
    message.length = sizeof(request);
    os_memcpy(message.payload, &request, sizeof(request));
    if (os_msg_send_to_identity(*identity, &message) < 0 ||
        os_msg_wait_timeout(&message, WINDOW_PRESENT_TIMEOUT_TICKS) < 0 ||
        message.type != OS_IPC_MESSAGE_REPLY ||
        message.length != sizeof(OsDisplayServiceInfoReply)) {
        return 0;
    }
    OsDisplayServiceInfoReply reply;
    os_memcpy(&reply, message.payload, sizeof(reply));
    if (reply.size != sizeof(reply) || reply.result != OS_SUCCESS ||
        reply.request_id != request.request_id || reply.ready == 0 ||
        reply.width == 0 || reply.height == 0 ||
        (reply.format != OS64_PIXEL_FORMAT_RGB &&
         reply.format != OS64_PIXEL_FORMAT_BGR)) {
        return 0;
    }
    info->width = reply.width;
    info->height = reply.height;
    info->pixels_per_scanline = reply.pixels_per_scanline;
    info->format = reply.format;
    return 1;
}

static long refresh_display_owner(void) {
    OsProcessIdentity current;
    long result = os_service_find_owner_identity("display", &current);
    if (result < 0) {
        display_owner.pid = 0;
        display_owner.generation = 0;
        pending_full_frame = 1;
        return result;
    }
    if (!identity_equal(current, display_owner)) {
        display_owner = current;
        pending_full_frame = 1;
        os_printf("[windowd] display connected pid=%u generation=%u\n",
                  current.pid,
                  current.generation);
    }
    return OS_SUCCESS;
}

static long present_composite(void) {
    long result = refresh_display_owner();
    if (result < 0) {
        return result;
    }
    uint32_t generation = next_frame_generation++;
    if (generation == 0) {
        generation = next_frame_generation++;
    }
    OsDisplayPresentReply reply;
    result = os_display_present(display_owner,
                                composite_surface,
                                generation,
                                0,
                                0,
                                WINDOW_PRESENT_TIMEOUT_TICKS,
                                &reply);
    if (result < 0 || reply.accepted_generation != generation) {
        pending_full_frame = 1;
        return result < 0 ? result : OS_ERR_BAD_BUFFER;
    }
    pending_full_frame = 0;
    os_printf("[windowd] frame ACK generation=%u window=%u\n",
              generation,
              window_state.active ? window_state.window_id : 0);
    return OS_SUCCESS;
}

static void send_window_reply(OsProcessIdentity target,
                              uint32_t request_id,
                              uint32_t operation,
                              int32_t result,
                              uint32_t window_id,
                              uint32_t window_generation,
                              uint32_t content_generation) {
    if (target.pid == 0 || target.generation == 0 || request_id == 0) {
        return;
    }
    OsWindowReply reply;
    reply.size = sizeof(reply);
    reply.abi_version = OS64_WINDOW_ABI_VERSION;
    reply.command = OS_WINDOW_REPLY;
    reply.flags = 0;
    reply.result = result;
    reply.request_id = request_id;
    reply.operation = operation;
    reply.window_id = window_id;
    reply.window_generation = window_generation;
    reply.accepted_content_generation = content_generation;

    OsIpcMessageV2 message;
    os_msg_v2_init(&message, OS_IPC_MESSAGE_REPLY);
    message.reply_to = request_id;
    message.length = sizeof(reply);
    os_memcpy(message.payload, &reply, sizeof(reply));
    os_msg_v2_send_to_identity(target, &message);
}

static int handle_service_query(const OsIpcMessageV2* message) {
    if (message == 0 || message->type != OS_IPC_MESSAGE_REQUEST ||
        message->length != sizeof(OsServiceQueryRequest) ||
        message->handle_count != 0) {
        return 0;
    }
    OsServiceQueryRequest request;
    os_memcpy(&request, message->payload, sizeof(request));
    if (request.size != sizeof(request) ||
        (request.command != OS_SERVICE_QUERY_HEALTH &&
         request.command != OS_SERVICE_QUERY_STATUS)) {
        return 0;
    }
    OsServiceHealthReply reply;
    reply.size = sizeof(reply);
    reply.command = request.command;
    reply.result = OS_SUCCESS;
    reply.request_id = request.request_id;
    reply.abi_version = OS64_SERVICE_PROTOCOL_ABI_VERSION;
    reply.ready = 1;
    reply.health = OS_SERVICE_HEALTH_HEALTHY;
    reply.reserved = 0;
    OsIpcMessage response;
    os_msg_init(&response, OS_IPC_MESSAGE_REPLY);
    response.length = sizeof(reply);
    os_memcpy(response.payload, &reply, sizeof(reply));
    os_msg_send_to_identity(os_msg_v2_sender_identity(message), &response);
    return 1;
}

static long validate_surface(OsHandle surface,
                             uint32_t width,
                             uint32_t height,
                             uint32_t stride_pixels,
                             uint32_t pixel_format,
                             OsGraphicsSurfaceHandleInfo* info,
                             const uint32_t** pixels) {
    if (surface == 0 || info == 0 || pixels == 0 ||
        os_surface_get_info(surface, info) < 0 ||
        info->width != display_info.width || info->height != display_info.height ||
        width != info->width || height != info->height ||
        stride_pixels != info->stride_pixels || pixel_format != info->pixel_format ||
        pixel_format != display_info.format) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    *pixels = (const uint32_t*)os_surface_map(surface, OS_SURFACE_MAP_READ);
    return *pixels != 0 ? OS_SUCCESS : OS_ERR_PERMISSION_DENIED;
}

static void handle_create(const OsIpcMessageV2* message) {
    OsProcessIdentity sender = os_msg_v2_sender_identity(message);
    OsWindowCreateRequest request;
    if (message->length != sizeof(request) || message->handle_count != 1 ||
        !(message->flags & OS_IPC_FLAG_HAS_HANDLES)) {
        close_message_handles(message);
        send_window_reply(sender, message->request_id, OS_WINDOW_CREATE,
                          OS_ERR_INVALID_ARGUMENT, 0, 0, 0);
        return;
    }
    os_memcpy(&request, message->payload, sizeof(request));
    long result = window_protocol_validate_create(&request);
    if (result == OS_SUCCESS && request.request_id != message->request_id) {
        result = OS_ERR_INVALID_ARGUMENT;
    }
    if (result == OS_SUCCESS) {
        result = window_state_can_create(&window_state,
                                         sender,
                                         request.content_generation);
    }
    OsGraphicsSurfaceHandleInfo candidate_info;
    const uint32_t* candidate_pixels = 0;
    if (result == OS_SUCCESS) {
        result = validate_surface(message->handles[0],
                                  request.width,
                                  request.height,
                                  request.stride_pixels,
                                  request.pixel_format,
                                  &candidate_info,
                                  &candidate_pixels);
    }
    if (result == OS_SUCCESS) {
        result = window_compositor_copy_full(composite_pixels,
                                             composite_info.stride_pixels,
                                             candidate_pixels,
                                             candidate_info.stride_pixels,
                                             display_info.width,
                                             display_info.height);
    }
    if (result == OS_SUCCESS) {
        pending_full_frame = 1;
        result = present_composite();
    }
    if (result < 0) {
        if (candidate_pixels != 0) {
            os_surface_unmap(message->handles[0], (void*)candidate_pixels);
        }
        close_handle(message->handles[0]);
        window_compositor_clear(composite_pixels,
                                composite_info.stride_pixels,
                                display_info.width,
                                display_info.height,
                                0);
        send_window_reply(sender, request.request_id, OS_WINDOW_CREATE,
                          (int32_t)result, 0, 0, 0);
        return;
    }
    client_surface = message->handles[0];
    client_pixels = candidate_pixels;
    client_info = candidate_info;
    window_state_commit_create(&window_state, sender, request.content_generation);
    send_window_reply(sender,
                      request.request_id,
                      OS_WINDOW_CREATE,
                      OS_SUCCESS,
                      window_state.window_id,
                      window_state.window_generation,
                      window_state.accepted_content_generation);
    os_printf("[windowd] created id=%u generation=%u owner=%u:%u\n",
              window_state.window_id,
              window_state.window_generation,
              sender.pid,
              sender.generation);
}

static void handle_set_surface(const OsIpcMessageV2* message) {
    OsProcessIdentity sender = os_msg_v2_sender_identity(message);
    OsWindowSetSurfaceRequest request;
    if (message->length != sizeof(request) || message->handle_count != 1 ||
        !(message->flags & OS_IPC_FLAG_HAS_HANDLES)) {
        close_message_handles(message);
        send_window_reply(sender, message->request_id, OS_WINDOW_SET_SURFACE,
                          OS_ERR_INVALID_ARGUMENT, 0, 0, 0);
        return;
    }
    os_memcpy(&request, message->payload, sizeof(request));
    long result = window_protocol_validate_set_surface(&request);
    if (result == OS_SUCCESS && request.request_id != message->request_id) {
        result = OS_ERR_INVALID_ARGUMENT;
    }
    if (result == OS_SUCCESS) {
        result = window_state_validate_content(&window_state,
                                               sender,
                                               request.window_id,
                                               request.window_generation,
                                               request.content_generation);
    }
    OsGraphicsSurfaceHandleInfo candidate_info;
    const uint32_t* candidate_pixels = 0;
    if (result == OS_SUCCESS) {
        result = validate_surface(message->handles[0],
                                  request.width,
                                  request.height,
                                  request.stride_pixels,
                                  request.pixel_format,
                                  &candidate_info,
                                  &candidate_pixels);
    }
    if (result == OS_SUCCESS) {
        result = window_compositor_copy_full(composite_pixels,
                                             composite_info.stride_pixels,
                                             candidate_pixels,
                                             candidate_info.stride_pixels,
                                             display_info.width,
                                             display_info.height);
    }
    if (result == OS_SUCCESS) {
        pending_full_frame = 1;
        result = present_composite();
    }
    if (result < 0) {
        if (candidate_pixels != 0) {
            os_surface_unmap(message->handles[0], (void*)candidate_pixels);
        }
        close_handle(message->handles[0]);
        if (client_pixels != 0) {
            window_compositor_copy_full(composite_pixels,
                                        composite_info.stride_pixels,
                                        client_pixels,
                                        client_info.stride_pixels,
                                        display_info.width,
                                        display_info.height);
        }
        send_window_reply(sender,
                          request.request_id,
                          OS_WINDOW_SET_SURFACE,
                          (int32_t)result,
                          window_state.window_id,
                          window_state.window_generation,
                          window_state.accepted_content_generation);
        return;
    }
    release_client_surface();
    client_surface = message->handles[0];
    client_pixels = candidate_pixels;
    client_info = candidate_info;
    window_state_commit_content(&window_state, request.content_generation);
    send_window_reply(sender,
                      request.request_id,
                      OS_WINDOW_SET_SURFACE,
                      OS_SUCCESS,
                      window_state.window_id,
                      window_state.window_generation,
                      window_state.accepted_content_generation);
    os_printf("[windowd] surface replaced content=%u\n",
              request.content_generation);
}

static void handle_damage(const OsIpcMessageV2* message) {
    OsProcessIdentity sender = os_msg_v2_sender_identity(message);
    OsWindowDamageRequest request;
    if (message->length != sizeof(request) || message->handle_count != 0) {
        close_message_handles(message);
        send_window_reply(sender, message->request_id, OS_WINDOW_DAMAGE,
                          OS_ERR_INVALID_ARGUMENT, 0, 0, 0);
        return;
    }
    os_memcpy(&request, message->payload, sizeof(request));
    long result = window_protocol_validate_damage(&request);
    if (result == OS_SUCCESS && request.request_id != message->request_id) {
        result = OS_ERR_INVALID_ARGUMENT;
    }
    if (result == OS_SUCCESS) {
        result = window_state_validate_content(&window_state,
                                               sender,
                                               request.window_id,
                                               request.window_generation,
                                               request.content_generation);
    }
    if (result == OS_SUCCESS) {
        result = window_compositor_copy_full(composite_pixels,
                                             composite_info.stride_pixels,
                                             client_pixels,
                                             client_info.stride_pixels,
                                             display_info.width,
                                             display_info.height);
    }
    if (result == OS_SUCCESS) {
        pending_full_frame = 1;
        result = present_composite();
    }
    if (result == OS_SUCCESS) {
        window_state_commit_content(&window_state, request.content_generation);
    }
    send_window_reply(sender,
                      request.request_id,
                      OS_WINDOW_DAMAGE,
                      (int32_t)result,
                      window_state.window_id,
                      window_state.window_generation,
                      window_state.accepted_content_generation);
    if (result == OS_SUCCESS) {
        os_printf("[windowd] damage accepted content=%u\n",
                  request.content_generation);
    }
}

static void destroy_window(int unexpected) {
    uint32_t window_id = window_state.window_id;
    OsProcessIdentity owner = window_state.owner;
    release_client_surface();
    window_state_destroy(&window_state);
    window_input_router_reset(&input_router);
    if (unexpected) {
        os_printf("[windowd] owner exit cleanup id=%u owner=%u:%u retained-frame=1\n",
                  window_id,
                  owner.pid,
                  owner.generation);
        return;
    }
    window_compositor_clear(composite_pixels,
                            composite_info.stride_pixels,
                            display_info.width,
                            display_info.height,
                            0);
    pending_full_frame = 1;
    present_composite();
}

static void handle_destroy(const OsIpcMessageV2* message) {
    OsProcessIdentity sender = os_msg_v2_sender_identity(message);
    OsWindowDestroyRequest request;
    if (message->length != sizeof(request) || message->handle_count != 0) {
        close_message_handles(message);
        send_window_reply(sender, message->request_id, OS_WINDOW_DESTROY,
                          OS_ERR_INVALID_ARGUMENT, 0, 0, 0);
        return;
    }
    os_memcpy(&request, message->payload, sizeof(request));
    long result = window_protocol_validate_destroy(&request);
    if (result == OS_SUCCESS && request.request_id != message->request_id) {
        result = OS_ERR_INVALID_ARGUMENT;
    }
    if (result == OS_SUCCESS) {
        result = window_state_validate_target(&window_state,
                                              sender,
                                              request.window_id,
                                              request.window_generation);
    }
    uint32_t window_id = window_state.window_id;
    uint32_t window_generation = window_state.window_generation;
    if (result == OS_SUCCESS) {
        destroy_window(0);
    }
    send_window_reply(sender,
                      request.request_id,
                      OS_WINDOW_DESTROY,
                      (int32_t)result,
                      window_id,
                      window_generation,
                      0);
    if (result == OS_SUCCESS) {
        os_printf("[windowd] destroyed id=%u generation=%u\n",
                  window_id,
                  window_generation);
    }
}

static void reject_message(const OsIpcMessageV2* message, int32_t result) {
    OsProcessIdentity sender = os_msg_v2_sender_identity(message);
    uint32_t request_id = message != 0 ? message->request_id : 0;
    uint32_t operation = 0;
    if (message != 0 && message->length >= 20) {
        const uint32_t* words = (const uint32_t*)message->payload;
        operation = words[2];
        if (words[4] != 0) {
            request_id = words[4];
        }
    }
    close_message_handles(message);
    send_window_reply(sender, request_id, operation, result, 0, 0, 0);
}

static void handle_message(const OsIpcMessageV2* message) {
    if (handle_service_query(message)) {
        return;
    }
    if (message == 0 || message->type != OS_IPC_MESSAGE_REQUEST ||
        message->length < 12) {
        reject_message(message, OS_ERR_INVALID_ARGUMENT);
        return;
    }
    const uint32_t* words = (const uint32_t*)message->payload;
    if (words[2] == OS_WINDOW_CREATE) {
        handle_create(message);
    } else if (words[2] == OS_WINDOW_SET_SURFACE) {
        handle_set_surface(message);
    } else if (words[2] == OS_WINDOW_DAMAGE) {
        handle_damage(message);
    } else if (words[2] == OS_WINDOW_DESTROY) {
        handle_destroy(message);
    } else {
        reject_message(message, OS_ERR_UNSUPPORTED);
    }
}

static void service_deadlines(void) {
    if (window_state.active && os_process_identity_alive(window_state.owner) < 0) {
        destroy_window(1);
        return;
    }
    OsProcessIdentity previous = display_owner;
    if (refresh_display_owner() == OS_SUCCESS &&
        (!identity_equal(previous, display_owner) || pending_full_frame)) {
        present_composite();
        if (!identity_equal(previous, display_owner)) {
            os_puts("[windowd] display reconnect full frame submitted");
        }
    }
}

int main(void) {
    window_state_init(&window_state);
    window_input_router_init(&input_router);
    display_owner.pid = 0;
    display_owner.generation = 0;
    client_surface = 0;
    client_pixels = 0;
    composite_surface = 0;
    composite_pixels = 0;
    next_frame_generation = (uint32_t)os_time_ticks() + 1u;
    pending_full_frame = 0;

    if (!query_display(&display_owner, &display_info)) {
        os_puts("[windowd] display unavailable");
        return 1;
    }
    composite_surface = os_surface_create(display_info.width,
                                          display_info.height,
                                          display_info.format);
    composite_pixels = composite_surface != 0
        ? (uint32_t*)os_surface_map(composite_surface,
                                   OS_SURFACE_MAP_READ | OS_SURFACE_MAP_WRITE)
        : 0;
    if (composite_pixels == 0 ||
        os_surface_get_info(composite_surface, &composite_info) < 0) {
        os_puts("[windowd] composite allocation failed");
        release_composite_surface();
        return 1;
    }
    window_compositor_clear(composite_pixels,
                            composite_info.stride_pixels,
                            display_info.width,
                            display_info.height,
                            0);
    long result = os_service_register("window", OS_SERVICE_FLAG_SYSTEM);
    if (result < 0) {
        os_printf("[windowd] register failed %ld\n", result);
        release_composite_surface();
        return 1;
    }
    os_printf("[windowd] ready pid=%u size=%ux%u display=%u:%u\n",
              (uint32_t)os_getpid(),
              display_info.width,
              display_info.height,
              display_owner.pid,
              display_owner.generation);

    while (1) {
        OsIpcMessageV2 message;
        result = os_msg_v2_wait_timeout(&message, WINDOW_FRAME_DEADLINE_TICKS);
        if (result == OS_ERR_TIMEOUT) {
            service_deadlines();
            continue;
        }
        if (result < 0) {
            os_printf("[windowd] wait failed %ld\n", result);
            release_client_surface();
            release_composite_surface();
            return 1;
        }
        handle_message(&message);
        service_deadlines();
    }
}
