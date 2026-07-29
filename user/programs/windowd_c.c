#include <os64/os64.h>

#include "windowd/window_compositor.h"
#include "windowd/window_input_router.h"
#include "windowd/window_protocol.h"
#include "windowd/window_state.h"

#define WINDOW_FRAME_DEADLINE_TICKS 10u
#define WINDOW_PRESENT_TIMEOUT_TICKS 50u
#define WINDOW_BACKGROUND_COLOR OS_RGB(12, 16, 24)

typedef struct WindowSurfaceSlot {
    OsHandle handle;
    OsGraphicsSurfaceHandleInfo info;
} WindowSurfaceSlot;

typedef struct WindowSurfaceCandidate {
    OsHandle handle;
    const uint32_t* pixels;
    OsGraphicsSurfaceHandleInfo info;
} WindowSurfaceCandidate;

typedef struct WindowDamageTransaction {
    uint32_t active;
    OsProcessIdentity sender;
    uint32_t request_id;
    uint32_t window_id;
    uint32_t window_generation;
    uint32_t content_generation;
    uint32_t submission_id;
    uint32_t rect_count;
    uint32_t chunk_count;
    uint32_t next_chunk;
    uint32_t received_rects;
    uint32_t started_ticks;
    OsRect rects[OS_WINDOW_DAMAGE_MAX_RECTS];
} WindowDamageTransaction;

static WindowTable window_table;
static WindowInputRouter input_router;
static WindowSurfaceSlot window_surfaces[OS_WINDOW_MAX_WINDOWS];
static WindowCompositorSource compositor_sources[OS_WINDOW_MAX_WINDOWS];
static WindowDamageAccumulator screen_damage;
static WindowDamageTransaction damage_transaction;
static OsProcessIdentity display_owner;
static OsProcessIdentity input_owner;
static OsGraphicsInfo display_info;
static OsHandle composite_surface;
static uint32_t* composite_pixels;
static OsGraphicsSurfaceHandleInfo composite_info;
static OsHandle console_snapshot_surface;
static const uint32_t* console_snapshot_pixels;
static OsGraphicsSurfaceHandleInfo console_snapshot_info;
static WindowCompositorSource console_underlay;
static uint32_t gui_session_generation;
static uint32_t next_frame_generation;
static uint32_t pending_full_frame;

static int identity_equal(OsProcessIdentity left, OsProcessIdentity right) {
    return left.pid != 0 && left.pid == right.pid && left.generation != 0 &&
           left.generation == right.generation;
}

static void send_window_event(const WindowFocusEndpoint* endpoint,
                              uint32_t command,
                              uint64_t timestamp,
                              const OsInputEvent* input) {
    if (endpoint == 0 || endpoint->owner.pid == 0 ||
        endpoint->event_sequence == 0 ||
        os_process_identity_alive(endpoint->owner) < 0) {
        return;
    }
    OsWindowEvent event;
    os_memset(&event, 0, sizeof(event));
    event.size = sizeof(event);
    event.abi_version = OS64_WINDOW_ABI_VERSION;
    event.command = command;
    event.event_sequence = endpoint->event_sequence;
    event.window_id = endpoint->window_id;
    event.window_generation = endpoint->window_generation;
    if (input != 0) {
        event.input = *input;
    } else {
        event.input.type = OS_INPUT_EVENT_NONE;
        event.input.size = sizeof(event.input);
        event.input.timestamp_ticks = timestamp;
    }

    OsIpcMessageV2 message;
    os_msg_v2_init(&message, OS_IPC_MESSAGE_EVENT);
    message.length = sizeof(event);
    os_memcpy(message.payload, &event, sizeof(event));
    os_msg_v2_send_to_identity(endpoint->owner, &message);
}

static void emit_focus_change(const WindowFocusChange* change,
                              uint64_t timestamp) {
    if (change == 0) {
        return;
    }
    send_window_event(&change->focus_out, OS_WINDOW_EVENT_FOCUS_OUT,
                      timestamp, 0);
    send_window_event(&change->focus_in, OS_WINDOW_EVENT_FOCUS_IN,
                      timestamp, 0);
}

static WindowEntry* topmost_visible_window(void) {
    for (uint32_t i = window_table.count; i != 0; i--) {
        uint32_t slot = window_table.z_slots[i - 1u];
        if (slot < OS_WINDOW_MAX_WINDOWS) {
            WindowEntry* entry = &window_table.entries[slot];
            if (entry->active && entry->visible) {
                return entry;
            }
        }
    }
    return 0;
}

static void focus_entry(WindowEntry* entry, uint64_t timestamp) {
    WindowFocusChange change;
    if (entry != 0 && entry->active && entry->visible) {
        window_input_router_focus(&input_router, entry->owner,
                                  entry->window_id, entry->window_generation,
                                  timestamp,
                                  &change);
    } else {
        window_input_router_clear(&input_router, &change);
    }
    emit_focus_change(&change, timestamp);
}

static void reconcile_invalid_focus(uint64_t timestamp) {
    if (input_router.focused_window_id == 0) {
        return;
    }
    WindowEntry* focused = window_state_find(&window_table,
                                             input_router.focused_window_id,
                                             input_router.focused_window_generation);
    if (focused != 0 && focused->visible &&
        identity_equal(focused->owner, input_router.focused_owner)) {
        return;
    }
    focus_entry(topmost_visible_window(), timestamp);
}

static void notify_input_service(void) {
    OsProcessIdentity input;
    if (os_service_find_owner_identity("input", &input) < 0) {
        return;
    }
    uint32_t command = OS_WINDOW_INPUT_EVENT;
    OsIpcMessage message;
    os_msg_init(&message, OS_IPC_MESSAGE_EVENT);
    message.length = sizeof(command);
    os_memcpy(message.payload, &command, sizeof(command));
    os_msg_send_to_identity(input, &message);
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

static void release_surface_values(OsHandle handle, const uint32_t* pixels) {
    if (handle != 0 && pixels != 0) {
        os_surface_unmap(handle, (void*)pixels);
    }
    close_handle(handle);
}

static void release_window_surface(uint32_t slot) {
    if (slot >= OS_WINDOW_MAX_WINDOWS) {
        return;
    }
    release_surface_values(window_surfaces[slot].handle,
                           compositor_sources[slot].pixels);
    window_surfaces[slot].handle = 0;
    os_memset(&window_surfaces[slot].info, 0,
              sizeof(window_surfaces[slot].info));
    compositor_sources[slot].pixels = 0;
    compositor_sources[slot].stride_pixels = 0;
}

static void install_window_surface(uint32_t slot,
                                   const WindowSurfaceCandidate* candidate) {
    if (slot >= OS_WINDOW_MAX_WINDOWS || candidate == 0) {
        return;
    }
    window_surfaces[slot].handle = candidate->handle;
    window_surfaces[slot].info = candidate->info;
    compositor_sources[slot].pixels = candidate->pixels;
    compositor_sources[slot].stride_pixels = candidate->info.stride_pixels;
}

static void release_composite_surface(void) {
    if (composite_pixels != 0 && composite_surface != 0) {
        os_surface_unmap(composite_surface, composite_pixels);
    }
    composite_pixels = 0;
    close_handle(composite_surface);
    composite_surface = 0;
}

static void drop_local_gui_session(void) {
    if (console_snapshot_pixels != 0 && console_snapshot_surface != 0) {
        os_surface_unmap(console_snapshot_surface,
                         (void*)console_snapshot_pixels);
    }
    console_snapshot_pixels = 0;
    close_handle(console_snapshot_surface);
    console_snapshot_surface = 0;
    os_memset(&console_snapshot_info, 0, sizeof(console_snapshot_info));
    console_underlay.pixels = 0;
    console_underlay.stride_pixels = 0;
    gui_session_generation = 0;
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
                  current.pid, current.generation);
    }
    return OS_SUCCESS;
}

static long acquire_gui_session(void) {
    if (gui_session_generation != 0) {
        return OS_SUCCESS;
    }
    OsHandle snapshot = os_surface_create(display_info.width,
                                          display_info.height,
                                          display_info.format);
    if (snapshot == 0) {
        return OS_ERR_NO_RESOURCES;
    }
    OsDisplaySessionInfo session;
    long result = os_display_session_acquire(snapshot, &session);
    if (result < 0) {
        close_handle(snapshot);
        return result;
    }
    const uint32_t* pixels = (const uint32_t*)os_surface_map(
        snapshot, OS_SURFACE_MAP_READ);
    if (pixels == 0 || os_surface_get_info(snapshot, &console_snapshot_info) < 0) {
        if (pixels != 0) {
            os_surface_unmap(snapshot, (void*)pixels);
        }
        os_display_session_release(session.generation);
        close_handle(snapshot);
        return OS_ERR_NOT_READY;
    }
    console_snapshot_surface = snapshot;
    console_snapshot_pixels = pixels;
    console_underlay.pixels = pixels;
    console_underlay.stride_pixels = console_snapshot_info.stride_pixels;
    gui_session_generation = session.generation;
    window_compositor_copy_full(composite_pixels,
                                composite_info.stride_pixels,
                                console_snapshot_pixels,
                                console_snapshot_info.stride_pixels,
                                display_info.width,
                                display_info.height);
    window_damage_full(&screen_damage);
    pending_full_frame = 1;
    os_printf("[windowd] GUI session acquired generation=%u underlay=read-only\n",
              gui_session_generation);
    return OS_SUCCESS;
}

static long ensure_gui_session(void) {
    if (refresh_display_owner() < 0) {
        return OS_ERR_NOT_READY;
    }
    if (gui_session_generation != 0) {
        OsDisplaySessionInfo current;
        if (os_display_session_get_info(&current) == OS_SUCCESS &&
            current.state == OS_DISPLAY_SESSION_GUI_ACTIVE &&
            current.generation == gui_session_generation &&
            identity_equal(display_owner,
                           (OsProcessIdentity){current.display_pid,
                                               current.display_generation})) {
            return OS_SUCCESS;
        }
        drop_local_gui_session();
    }
    return acquire_gui_session();
}

static long release_gui_session(void) {
    long result = OS_SUCCESS;
    uint32_t generation = gui_session_generation;
    if (generation != 0) {
        result = os_display_session_release(generation);
    }
    drop_local_gui_session();
    window_damage_reset(&screen_damage);
    pending_full_frame = 0;
    os_printf("[windowd] GUI session released generation=%u result=%ld\n",
              generation, result);
    return result;
}

static long compose_pending(void) {
    if (screen_damage.count == 0) {
        return OS_SUCCESS;
    }
    if (console_underlay.pixels == 0) {
        return OS_ERR_NOT_READY;
    }
    return window_compositor_compose_underlay(composite_pixels,
                                              composite_info.stride_pixels,
                                              display_info.width,
                                              display_info.height,
                                              &console_underlay,
                                              &window_table,
                                              compositor_sources,
                                              &screen_damage);
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
    const OsRect* rects = screen_damage.rects;
    uint32_t rect_count = screen_damage.count;
    if (pending_full_frame || screen_damage.full_screen) {
        rects = 0;
        rect_count = 0;
    }
    OsDisplayPresentReply reply;
    result = os_display_present(display_owner,
                                composite_surface,
                                generation,
                                rects,
                                rect_count,
                                WINDOW_PRESENT_TIMEOUT_TICKS,
                                &reply);
    if (result < 0 || reply.accepted_generation != generation) {
        pending_full_frame = 1;
        return result < 0 ? result : OS_ERR_BAD_BUFFER;
    }
    os_printf("[windowd] frame ACK generation=%u windows=%u damage=%u\n",
              generation, window_table.count,
              rect_count == 0 ? 1u : rect_count);
    pending_full_frame = 0;
    window_damage_reset(&screen_damage);
    return OS_SUCCESS;
}

static long compose_and_present(void) {
    long result = ensure_gui_session();
    if (result < 0) {
        return result;
    }
    result = compose_pending();
    return result < 0 ? result : present_composite();
}

static void rebuild_composite(void) {
    window_damage_full(&screen_damage);
    if (compose_pending() < 0) {
        return;
    }
    pending_full_frame = 1;
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

static void send_window_info_reply(OsProcessIdentity target,
                                   uint32_t request_id,
                                   int32_t result,
                                   const WindowEntry* entry) {
    if (target.pid == 0 || target.generation == 0 || request_id == 0) {
        return;
    }
    OsWindowInfoReply reply;
    os_memset(&reply, 0, sizeof(reply));
    reply.size = sizeof(reply);
    reply.abi_version = OS64_WINDOW_ABI_VERSION;
    reply.command = OS_WINDOW_GET_INFO;
    reply.result = result;
    reply.request_id = request_id;
    reply.capacity = OS_WINDOW_MAX_WINDOWS;
    if (result == OS_SUCCESS && entry != 0) {
        uint32_t slot = window_state_slot(&window_table, entry);
        reply.window_id = entry->window_id;
        reply.window_generation = entry->window_generation;
        reply.x = entry->x;
        reply.y = entry->y;
        reply.width = entry->width;
        reply.height = entry->height;
        reply.stride_pixels = slot < OS_WINDOW_MAX_WINDOWS
            ? window_surfaces[slot].info.stride_pixels : 0;
        reply.pixel_format = slot < OS_WINDOW_MAX_WINDOWS
            ? window_surfaces[slot].info.pixel_format : display_info.format;
        reply.content_generation = entry->accepted_content_generation;
        reply.visible = entry->visible;
        reply.focused = window_input_router_is_focused(
            &input_router, entry->owner, entry->window_id,
            entry->window_generation) ? 1u : 0u;
    } else if (result == OS_SUCCESS) {
        reply.width = display_info.width;
        reply.height = display_info.height;
        reply.stride_pixels = display_info.pixels_per_scanline;
        reply.pixel_format = display_info.format;
    }
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
                             WindowSurfaceCandidate* candidate) {
    if (surface == 0 || candidate == 0 || width == 0 || height == 0 ||
        width > display_info.width || height > display_info.height) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    candidate->handle = surface;
    candidate->pixels = 0;
    if (os_surface_get_info(surface, &candidate->info) < 0 ||
        candidate->info.width != width || candidate->info.height != height ||
        candidate->info.stride_pixels != stride_pixels ||
        candidate->info.pixel_format != pixel_format ||
        pixel_format != display_info.format) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    candidate->pixels = (const uint32_t*)os_surface_map(surface,
                                                         OS_SURFACE_MAP_READ);
    return candidate->pixels != 0 ? OS_SUCCESS : OS_ERR_PERMISSION_DENIED;
}

static void release_candidate(WindowSurfaceCandidate* candidate) {
    if (candidate == 0) {
        return;
    }
    release_surface_values(candidate->handle, candidate->pixels);
    candidate->handle = 0;
    candidate->pixels = 0;
}

static int request_matches(const OsIpcMessageV2* message,
                           uint32_t request_id) {
    return request_id != 0 && request_id == message->request_id;
}

static void handle_create(const OsIpcMessageV2* message) {
    OsProcessIdentity sender = os_msg_v2_sender_identity(message);
    uint32_t request_id = message->request_id;
    uint32_t content_generation = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;
    uint32_t format = 0;
    int32_t x = 0;
    int32_t y = 0;
    long result = OS_ERR_INVALID_ARGUMENT;
    if (message->handle_count != 1 ||
        !(message->flags & OS_IPC_FLAG_HAS_HANDLES)) {
        close_message_handles(message);
        send_window_reply(sender, request_id, OS_WINDOW_CREATE, result, 0, 0, 0);
        return;
    }
    if (message->length == sizeof(OsWindowCreateRequest)) {
        OsWindowCreateRequest request;
        os_memcpy(&request, message->payload, sizeof(request));
        result = window_protocol_validate_create(&request);
        request_id = request.request_id;
        content_generation = request.content_generation;
        width = request.width;
        height = request.height;
        stride = request.stride_pixels;
        format = request.pixel_format;
    } else if (message->length == sizeof(OsWindowCreateGeometryRequest)) {
        OsWindowCreateGeometryRequest request;
        os_memcpy(&request, message->payload, sizeof(request));
        result = window_protocol_validate_create_geometry(&request);
        request_id = request.request_id;
        content_generation = request.content_generation;
        x = request.x;
        y = request.y;
        width = request.width;
        height = request.height;
        stride = request.stride_pixels;
        format = request.pixel_format;
    }
    if (result == OS_SUCCESS && !request_matches(message, request_id)) {
        result = OS_ERR_INVALID_ARGUMENT;
    }
    if (result == OS_SUCCESS) {
        result = window_state_can_create(&window_table, sender,
                                         content_generation, width, height);
    }
    WindowSurfaceCandidate candidate;
    os_memset(&candidate, 0, sizeof(candidate));
    uint32_t candidate_released = 0;
    if (result == OS_SUCCESS) {
        result = validate_surface(message->handles[0], width, height, stride,
                                  format, &candidate);
    }
    if (result == OS_SUCCESS && window_table.count == 0) {
        result = ensure_gui_session();
    }
    WindowEntry* entry = 0;
    uint32_t slot = OS_WINDOW_MAX_WINDOWS;
    if (result == OS_SUCCESS) {
        entry = window_state_commit_create(&window_table, sender,
                                           content_generation, x, y,
                                           width, height);
        if (entry == 0) {
            result = OS_ERR_NO_RESOURCES;
        } else {
            slot = window_state_slot(&window_table, entry);
            install_window_surface(slot, &candidate);
            window_damage_add_screen(&screen_damage,
                                     window_state_screen_rect(entry));
            result = compose_and_present();
        }
    }
    if (result < 0) {
        if (entry != 0) {
            window_state_destroy(&window_table, entry);
            release_window_surface(slot);
            candidate.handle = 0;
            candidate.pixels = 0;
            candidate_released = 1;
            rebuild_composite();
        } else if (candidate.handle != 0) {
            release_candidate(&candidate);
            candidate_released = 1;
        }
        if (!candidate_released) {
            close_handle(message->handles[0]);
        }
        if (window_table.count == 0 && gui_session_generation != 0) {
            release_gui_session();
        }
        send_window_reply(sender, request_id, OS_WINDOW_CREATE,
                          (int32_t)result, 0, 0, 0);
        return;
    }
    send_window_reply(sender, request_id, OS_WINDOW_CREATE, OS_SUCCESS,
                      entry->window_id, entry->window_generation,
                      entry->accepted_content_generation);
    os_printf("[windowd] created id=%u generation=%u owner=%u:%u geometry=%d,%d %ux%u z=%u\n",
              entry->window_id, entry->window_generation,
              sender.pid, sender.generation, entry->x, entry->y,
              entry->width, entry->height, window_table.count - 1u);
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
    if (result == OS_SUCCESS && !request_matches(message, request.request_id)) {
        result = OS_ERR_INVALID_ARGUMENT;
    }
    WindowEntry* entry = 0;
    if (result == OS_SUCCESS) {
        result = window_state_validate_content(&window_table, sender,
                                               request.window_id,
                                               request.window_generation,
                                               request.content_generation,
                                               &entry);
    }
    if (result == OS_SUCCESS &&
        (request.width != entry->width || request.height != entry->height)) {
        result = OS_ERR_INVALID_ARGUMENT;
    }
    WindowSurfaceCandidate candidate;
    os_memset(&candidate, 0, sizeof(candidate));
    uint32_t candidate_released = 0;
    if (result == OS_SUCCESS) {
        result = validate_surface(message->handles[0], request.width,
                                  request.height, request.stride_pixels,
                                  request.pixel_format, &candidate);
    }
    uint32_t slot = entry != 0
        ? window_state_slot(&window_table, entry)
        : OS_WINDOW_MAX_WINDOWS;
    WindowSurfaceSlot old_surface;
    WindowCompositorSource old_source;
    os_memset(&old_surface, 0, sizeof(old_surface));
    os_memset(&old_source, 0, sizeof(old_source));
    if (result == OS_SUCCESS) {
        old_surface = window_surfaces[slot];
        old_source = compositor_sources[slot];
        install_window_surface(slot, &candidate);
        window_damage_add_screen(&screen_damage, window_state_screen_rect(entry));
        result = compose_and_present();
        if (result < 0) {
            window_surfaces[slot] = old_surface;
            compositor_sources[slot] = old_source;
            release_candidate(&candidate);
            candidate_released = 1;
            rebuild_composite();
        }
    }
    if (result < 0) {
        if (!candidate_released && candidate.handle != 0) {
            release_candidate(&candidate);
            candidate_released = 1;
        } else if (!candidate_released) {
            close_handle(message->handles[0]);
        }
        send_window_reply(sender, request.request_id, OS_WINDOW_SET_SURFACE,
                          (int32_t)result,
                          entry != 0 ? entry->window_id : 0,
                          entry != 0 ? entry->window_generation : 0,
                          entry != 0 ? entry->accepted_content_generation : 0);
        return;
    }
    release_surface_values(old_surface.handle, old_source.pixels);
    window_state_commit_content(entry, request.content_generation);
    send_window_reply(sender, request.request_id, OS_WINDOW_SET_SURFACE,
                      OS_SUCCESS, entry->window_id, entry->window_generation,
                      entry->accepted_content_generation);
    os_printf("[windowd] surface replaced id=%u content=%u\n",
              entry->window_id, request.content_generation);
}

static void handle_full_damage(const OsIpcMessageV2* message) {
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
    if (result == OS_SUCCESS && !request_matches(message, request.request_id)) {
        result = OS_ERR_INVALID_ARGUMENT;
    }
    WindowEntry* entry = 0;
    if (result == OS_SUCCESS) {
        result = window_state_validate_content(&window_table, sender,
                                               request.window_id,
                                               request.window_generation,
                                               request.content_generation,
                                               &entry);
    }
    if (result == OS_SUCCESS) {
        OsRect full = {0, 0, (int32_t)entry->width, (int32_t)entry->height};
        result = window_damage_add_window(&screen_damage, entry, full);
    }
    if (result == OS_SUCCESS) {
        result = compose_and_present();
    }
    if (result == OS_SUCCESS) {
        window_state_commit_content(entry, request.content_generation);
    }
    send_window_reply(sender, request.request_id, OS_WINDOW_DAMAGE,
                      (int32_t)result,
                      entry != 0 ? entry->window_id : 0,
                      entry != 0 ? entry->window_generation : 0,
                      entry != 0 ? entry->accepted_content_generation : 0);
    if (result == OS_SUCCESS) {
        os_printf("[windowd] damage accepted id=%u content=%u rects=1\n",
                  entry->window_id, request.content_generation);
    }
}

static void clear_damage_transaction(void) {
    os_memset(&damage_transaction, 0, sizeof(damage_transaction));
}

static void handle_damage_begin(const OsIpcMessageV2* message) {
    OsProcessIdentity sender = os_msg_v2_sender_identity(message);
    OsWindowDamageBeginRequest request;
    if (message->length != sizeof(request) || message->handle_count != 0) {
        close_message_handles(message);
        send_window_reply(sender, message->request_id, OS_WINDOW_DAMAGE,
                          OS_ERR_INVALID_ARGUMENT, 0, 0, 0);
        return;
    }
    os_memcpy(&request, message->payload, sizeof(request));
    long result = window_protocol_validate_damage_begin(&request);
    if (result == OS_SUCCESS && !request_matches(message, request.request_id)) {
        result = OS_ERR_INVALID_ARGUMENT;
    }
    WindowEntry* entry = 0;
    if (result == OS_SUCCESS) {
        result = window_state_validate_content(&window_table, sender,
                                               request.window_id,
                                               request.window_generation,
                                               request.content_generation,
                                               &entry);
    }
    if (result == OS_SUCCESS && damage_transaction.active) {
        result = OS_ERR_NO_RESOURCES;
    }
    if (result < 0) {
        send_window_reply(sender, request.request_id, OS_WINDOW_DAMAGE,
                          (int32_t)result,
                          entry != 0 ? entry->window_id : 0,
                          entry != 0 ? entry->window_generation : 0,
                          entry != 0 ? entry->accepted_content_generation : 0);
        return;
    }
    clear_damage_transaction();
    damage_transaction.active = 1;
    damage_transaction.sender = sender;
    damage_transaction.request_id = request.request_id;
    damage_transaction.window_id = request.window_id;
    damage_transaction.window_generation = request.window_generation;
    damage_transaction.content_generation = request.content_generation;
    damage_transaction.submission_id = request.submission_id;
    damage_transaction.rect_count = request.rect_count;
    damage_transaction.chunk_count = request.chunk_count;
    damage_transaction.started_ticks = (uint32_t)os_time_ticks();
}

static void reject_active_damage(int32_t result) {
    if (!damage_transaction.active) {
        return;
    }
    WindowEntry* entry = window_state_find(&window_table,
                                          damage_transaction.window_id,
                                          damage_transaction.window_generation);
    send_window_reply(damage_transaction.sender,
                      damage_transaction.request_id,
                      OS_WINDOW_DAMAGE,
                      result,
                      entry != 0 ? entry->window_id : 0,
                      entry != 0 ? entry->window_generation : 0,
                      entry != 0 ? entry->accepted_content_generation : 0);
    clear_damage_transaction();
}

static void handle_damage_rects(const OsIpcMessageV2* message) {
    OsProcessIdentity sender = os_msg_v2_sender_identity(message);
    OsWindowDamageRectsRequest request;
    if (message->length != sizeof(request) || message->handle_count != 0) {
        close_message_handles(message);
        if (damage_transaction.active &&
            identity_equal(sender, damage_transaction.sender)) {
            reject_active_damage(OS_ERR_INVALID_ARGUMENT);
        } else {
            send_window_reply(sender, message->request_id, OS_WINDOW_DAMAGE,
                              OS_ERR_INVALID_ARGUMENT, 0, 0, 0);
        }
        return;
    }
    os_memcpy(&request, message->payload, sizeof(request));
    long result = window_protocol_validate_damage_rects(&request);
    if (result == OS_SUCCESS &&
        (!damage_transaction.active ||
         !identity_equal(sender, damage_transaction.sender) ||
         request.request_id != damage_transaction.request_id ||
         request.submission_id != damage_transaction.submission_id ||
         request.chunk_index != damage_transaction.next_chunk ||
         request.chunk_index >= damage_transaction.chunk_count ||
         damage_transaction.received_rects + request.rect_count >
             damage_transaction.rect_count)) {
        result = OS_ERR_INVALID_ARGUMENT;
    }
    if (result < 0) {
        if (damage_transaction.active &&
            identity_equal(sender, damage_transaction.sender)) {
            reject_active_damage((int32_t)result);
        } else {
            send_window_reply(sender, message->request_id, OS_WINDOW_DAMAGE,
                              (int32_t)result, 0, 0, 0);
        }
        return;
    }
    for (uint32_t i = 0; i < request.rect_count; i++) {
        damage_transaction.rects[damage_transaction.received_rects++] =
            request.rects[i];
    }
    damage_transaction.next_chunk++;
}

static void handle_damage_commit(const OsIpcMessageV2* message) {
    OsProcessIdentity sender = os_msg_v2_sender_identity(message);
    OsWindowDamageCommitRequest request;
    if (message->length != sizeof(request) || message->handle_count != 0) {
        close_message_handles(message);
        if (damage_transaction.active &&
            identity_equal(sender, damage_transaction.sender)) {
            reject_active_damage(OS_ERR_INVALID_ARGUMENT);
        } else {
            send_window_reply(sender, message->request_id, OS_WINDOW_DAMAGE,
                              OS_ERR_INVALID_ARGUMENT, 0, 0, 0);
        }
        return;
    }
    os_memcpy(&request, message->payload, sizeof(request));
    long result = window_protocol_validate_damage_commit(&request);
    if (result == OS_SUCCESS &&
        (!damage_transaction.active ||
         !identity_equal(sender, damage_transaction.sender) ||
         request.request_id != damage_transaction.request_id ||
         request.submission_id != damage_transaction.submission_id ||
         damage_transaction.next_chunk != damage_transaction.chunk_count ||
         damage_transaction.received_rects != damage_transaction.rect_count)) {
        result = OS_ERR_INVALID_ARGUMENT;
    }
    WindowEntry* entry = 0;
    if (result == OS_SUCCESS) {
        result = window_state_validate_content(&window_table, sender,
                                               damage_transaction.window_id,
                                               damage_transaction.window_generation,
                                               damage_transaction.content_generation,
                                               &entry);
    }
    if (result == OS_SUCCESS) {
        for (uint32_t i = 0; i < damage_transaction.rect_count; i++) {
            result = window_damage_add_window(&screen_damage, entry,
                                              damage_transaction.rects[i]);
            if (result < 0) {
                break;
            }
        }
    }
    if (result == OS_SUCCESS) {
        result = compose_and_present();
    }
    uint32_t content_generation = damage_transaction.content_generation;
    uint32_t rect_count = damage_transaction.rect_count;
    if (result == OS_SUCCESS) {
        window_state_commit_content(entry, content_generation);
    }
    send_window_reply(sender,
                      damage_transaction.active
                          ? damage_transaction.request_id
                          : request.request_id,
                      OS_WINDOW_DAMAGE,
                      (int32_t)result,
                      entry != 0 ? entry->window_id : 0,
                      entry != 0 ? entry->window_generation : 0,
                      entry != 0 ? entry->accepted_content_generation : 0);
    clear_damage_transaction();
    if (result == OS_SUCCESS) {
        os_printf("[windowd] damage accepted id=%u content=%u rects=%u\n",
                  entry->window_id, content_generation, rect_count);
    }
}

static void handle_visibility(const OsIpcMessageV2* message, uint32_t command) {
    OsProcessIdentity sender = os_msg_v2_sender_identity(message);
    OsWindowStateRequest request;
    if (message->length != sizeof(request) || message->handle_count != 0) {
        close_message_handles(message);
        send_window_reply(sender, message->request_id, command,
                          OS_ERR_INVALID_ARGUMENT, 0, 0, 0);
        return;
    }
    os_memcpy(&request, message->payload, sizeof(request));
    long result = window_protocol_validate_state(&request, command);
    if (result == OS_SUCCESS && !request_matches(message, request.request_id)) {
        result = OS_ERR_INVALID_ARGUMENT;
    }
    WindowEntry* entry = 0;
    if (result == OS_SUCCESS) {
        result = window_state_validate_target(&window_table, sender,
                                              request.window_id,
                                              request.window_generation,
                                              &entry);
    }
    uint32_t old_visible = entry != 0 ? entry->visible : 0;
    uint8_t old_z[OS_WINDOW_MAX_WINDOWS];
    for (uint32_t i = 0; i < OS_WINDOW_MAX_WINDOWS; i++) {
        old_z[i] = window_table.z_slots[i];
    }
    if (result == OS_SUCCESS) {
        window_damage_add_screen(&screen_damage, window_state_screen_rect(entry));
        window_state_set_visible(&window_table, entry,
                                 command == OS_WINDOW_SHOW, 1);
        result = compose_and_present();
        if (result < 0) {
            entry->visible = old_visible;
            for (uint32_t i = 0; i < OS_WINDOW_MAX_WINDOWS; i++) {
                window_table.z_slots[i] = old_z[i];
            }
            rebuild_composite();
        }
    }
    send_window_reply(sender, request.request_id, command, (int32_t)result,
                      entry != 0 ? entry->window_id : 0,
                      entry != 0 ? entry->window_generation : 0,
                      entry != 0 ? entry->accepted_content_generation : 0);
    if (result == OS_SUCCESS) {
        os_printf("[windowd] %s id=%u\n",
                  command == OS_WINDOW_SHOW ? "shown" : "hidden",
                  entry->window_id);
        if (command == OS_WINDOW_HIDE) {
            reconcile_invalid_focus(os_time_ticks());
        }
    }
}

static void handle_focus(const OsIpcMessageV2* message) {
    OsProcessIdentity sender = os_msg_v2_sender_identity(message);
    OsWindowFocusRequest request;
    if (message->length != sizeof(request) || message->handle_count != 0) {
        close_message_handles(message);
        send_window_reply(sender, message->request_id, OS_WINDOW_FOCUS,
                          OS_ERR_INVALID_ARGUMENT, 0, 0, 0);
        return;
    }
    os_memcpy(&request, message->payload, sizeof(request));
    long result = window_protocol_validate_state(&request, OS_WINDOW_FOCUS);
    if (result == OS_SUCCESS && !request_matches(message, request.request_id)) {
        result = OS_ERR_INVALID_ARGUMENT;
    }
    WindowEntry* entry = 0;
    if (result == OS_SUCCESS) {
        result = window_state_validate_target(&window_table, sender,
                                              request.window_id,
                                              request.window_generation,
                                              &entry);
    }
    if (result == OS_SUCCESS && !entry->visible) {
        result = OS_ERR_NOT_READY;
    }
    if (result == OS_SUCCESS) {
        focus_entry(entry, os_time_ticks());
    }
    send_window_reply(sender, request.request_id, OS_WINDOW_FOCUS,
                      (int32_t)result,
                      entry != 0 ? entry->window_id : 0,
                      entry != 0 ? entry->window_generation : 0,
                      entry != 0 ? entry->accepted_content_generation : 0);
    if (result == OS_SUCCESS) {
        os_printf("[windowd] focused id=%u owner=%u:%u\n",
                  entry->window_id, entry->owner.pid, entry->owner.generation);
    }
}

static void handle_info(const OsIpcMessageV2* message) {
    OsProcessIdentity sender = os_msg_v2_sender_identity(message);
    OsWindowInfoRequest request;
    if (message->length != sizeof(request) || message->handle_count != 0) {
        close_message_handles(message);
        send_window_info_reply(sender, message->request_id,
                               OS_ERR_INVALID_ARGUMENT, 0);
        return;
    }
    os_memcpy(&request, message->payload, sizeof(request));
    long result = window_protocol_validate_info(&request);
    if (result == OS_SUCCESS && !request_matches(message, request.request_id)) {
        result = OS_ERR_INVALID_ARGUMENT;
    }
    WindowEntry* entry = 0;
    if (result == OS_SUCCESS && request.window_id != 0) {
        result = window_state_validate_target(&window_table, sender,
                                              request.window_id,
                                              request.window_generation,
                                              &entry);
    }
    send_window_info_reply(sender, request.request_id, (int32_t)result, entry);
}

static void handle_input_event(const OsIpcMessageV2* message) {
    if (message == 0 || message->type != OS_IPC_MESSAGE_EVENT ||
        message->flags != 0 || message->length != sizeof(OsWindowInputForward) ||
        message->request_id != 0 || message->reply_to != 0 ||
        message->handle_count != 0) {
        close_message_handles(message);
        return;
    }
    OsProcessIdentity sender = os_msg_v2_sender_identity(message);
    OsProcessIdentity registered;
    if (os_service_find_owner_identity("input", &registered) < 0 ||
        !identity_equal(sender, registered)) {
        return;
    }
    if (!identity_equal(registered, input_owner)) {
        input_owner = registered;
        input_router.input_sequence = 0;
        os_printf("[windowd] input connected pid=%u generation=%u\n",
                  registered.pid, registered.generation);
    }

    OsWindowInputForward forward;
    os_memcpy(&forward, message->payload, sizeof(forward));
    if (forward.size != sizeof(forward) ||
        forward.abi_version != OS64_WINDOW_ABI_VERSION ||
        forward.command != OS_WINDOW_INPUT_EVENT || forward.flags != 0 ||
        forward.reserved != 0 || forward.event.size != sizeof(forward.event) ||
        forward.event.type != OS_INPUT_EVENT_KEY ||
        (forward.event.data.key.type != OS_KEY_EVENT_DOWN &&
         forward.event.data.key.type != OS_KEY_EVENT_UP) ||
        window_input_router_accept_input(&input_router,
                                         forward.input_sequence) < 0) {
        return;
    }

    reconcile_invalid_focus(forward.event.timestamp_ticks);
    WindowEntry* focused = window_state_find(&window_table,
                                             input_router.focused_window_id,
                                             input_router.focused_window_generation);
    if (focused == 0 || !focused->visible ||
        !identity_equal(focused->owner, input_router.focused_owner) ||
        forward.event.timestamp_ticks < input_router.focused_since_ticks) {
        return;
    }
    WindowFocusEndpoint endpoint;
    endpoint.owner = focused->owner;
    endpoint.window_id = focused->window_id;
    endpoint.window_generation = focused->window_generation;
    endpoint.event_sequence = window_input_router_next_event(&input_router);
    send_window_event(&endpoint, OS_WINDOW_EVENT_KEY,
                      forward.event.timestamp_ticks, &forward.event);
}

static void handle_move(const OsIpcMessageV2* message) {
    OsProcessIdentity sender = os_msg_v2_sender_identity(message);
    OsWindowMoveRequest request;
    if (message->length != sizeof(request) || message->handle_count != 0) {
        close_message_handles(message);
        send_window_reply(sender, message->request_id, OS_WINDOW_MOVE,
                          OS_ERR_INVALID_ARGUMENT, 0, 0, 0);
        return;
    }
    os_memcpy(&request, message->payload, sizeof(request));
    long result = window_protocol_validate_move(&request);
    if (result == OS_SUCCESS && !request_matches(message, request.request_id)) {
        result = OS_ERR_INVALID_ARGUMENT;
    }
    WindowEntry* entry = 0;
    if (result == OS_SUCCESS) {
        result = window_state_validate_target(&window_table, sender,
                                              request.window_id,
                                              request.window_generation,
                                              &entry);
    }
    int32_t old_x = entry != 0 ? entry->x : 0;
    int32_t old_y = entry != 0 ? entry->y : 0;
    if (result == OS_SUCCESS) {
        window_damage_add_screen(&screen_damage, window_state_screen_rect(entry));
        window_state_move(entry, request.x, request.y);
        window_damage_add_screen(&screen_damage, window_state_screen_rect(entry));
        result = compose_and_present();
        if (result < 0) {
            window_state_move(entry, old_x, old_y);
            rebuild_composite();
        }
    }
    send_window_reply(sender, request.request_id, OS_WINDOW_MOVE,
                      (int32_t)result,
                      entry != 0 ? entry->window_id : 0,
                      entry != 0 ? entry->window_generation : 0,
                      entry != 0 ? entry->accepted_content_generation : 0);
    if (result == OS_SUCCESS) {
        os_printf("[windowd] moved id=%u geometry=%d,%d %ux%u\n",
                  entry->window_id, entry->x, entry->y,
                  entry->width, entry->height);
    }
}

static void handle_resize(const OsIpcMessageV2* message) {
    OsProcessIdentity sender = os_msg_v2_sender_identity(message);
    OsWindowResizeRequest request;
    if (message->length != sizeof(request) || message->handle_count != 1 ||
        !(message->flags & OS_IPC_FLAG_HAS_HANDLES)) {
        close_message_handles(message);
        send_window_reply(sender, message->request_id, OS_WINDOW_RESIZE,
                          OS_ERR_INVALID_ARGUMENT, 0, 0, 0);
        return;
    }
    os_memcpy(&request, message->payload, sizeof(request));
    long result = window_protocol_validate_resize(&request);
    if (result == OS_SUCCESS && !request_matches(message, request.request_id)) {
        result = OS_ERR_INVALID_ARGUMENT;
    }
    WindowEntry* entry = 0;
    if (result == OS_SUCCESS) {
        result = window_state_validate_content(&window_table, sender,
                                               request.window_id,
                                               request.window_generation,
                                               request.content_generation,
                                               &entry);
    }
    WindowSurfaceCandidate candidate;
    os_memset(&candidate, 0, sizeof(candidate));
    uint32_t candidate_released = 0;
    if (result == OS_SUCCESS) {
        result = validate_surface(message->handles[0], request.width,
                                  request.height, request.stride_pixels,
                                  request.pixel_format, &candidate);
    }
    uint32_t slot = entry != 0
        ? window_state_slot(&window_table, entry)
        : OS_WINDOW_MAX_WINDOWS;
    uint32_t old_width = entry != 0 ? entry->width : 0;
    uint32_t old_height = entry != 0 ? entry->height : 0;
    WindowSurfaceSlot old_surface;
    WindowCompositorSource old_source;
    os_memset(&old_surface, 0, sizeof(old_surface));
    os_memset(&old_source, 0, sizeof(old_source));
    if (result == OS_SUCCESS) {
        old_surface = window_surfaces[slot];
        old_source = compositor_sources[slot];
        window_damage_add_screen(&screen_damage, window_state_screen_rect(entry));
        window_state_resize(entry, request.width, request.height);
        install_window_surface(slot, &candidate);
        window_damage_add_screen(&screen_damage, window_state_screen_rect(entry));
        result = compose_and_present();
        if (result < 0) {
            window_state_resize(entry, old_width, old_height);
            window_surfaces[slot] = old_surface;
            compositor_sources[slot] = old_source;
            release_candidate(&candidate);
            candidate_released = 1;
            rebuild_composite();
        }
    }
    if (result < 0) {
        if (!candidate_released && candidate.handle != 0) {
            release_candidate(&candidate);
            candidate_released = 1;
        } else if (!candidate_released) {
            close_handle(message->handles[0]);
        }
        send_window_reply(sender, request.request_id, OS_WINDOW_RESIZE,
                          (int32_t)result,
                          entry != 0 ? entry->window_id : 0,
                          entry != 0 ? entry->window_generation : 0,
                          entry != 0 ? entry->accepted_content_generation : 0);
        return;
    }
    release_surface_values(old_surface.handle, old_source.pixels);
    window_state_commit_content(entry, request.content_generation);
    send_window_reply(sender, request.request_id, OS_WINDOW_RESIZE, OS_SUCCESS,
                      entry->window_id, entry->window_generation,
                      entry->accepted_content_generation);
    os_printf("[windowd] resized id=%u geometry=%d,%d %ux%u content=%u\n",
              entry->window_id, entry->x, entry->y,
              entry->width, entry->height, request.content_generation);
}

static void destroy_entry(WindowEntry* entry, int unexpected) {
    if (entry == 0 || !entry->active) {
        return;
    }
    uint32_t slot = window_state_slot(&window_table, entry);
    uint32_t window_id = entry->window_id;
    OsProcessIdentity owner = entry->owner;
    OsRect old_rect = window_state_screen_rect(entry);
    window_state_destroy(&window_table, entry);
    release_window_surface(slot);
    window_damage_add_screen(&screen_damage, old_rect);
    if (unexpected) {
        os_printf("[windowd] owner exit cleanup id=%u owner=%u:%u retained-frame=0\n",
                  window_id, owner.pid, owner.generation);
    }
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
    if (result == OS_SUCCESS && !request_matches(message, request.request_id)) {
        result = OS_ERR_INVALID_ARGUMENT;
    }
    WindowEntry* entry = 0;
    if (result == OS_SUCCESS) {
        result = window_state_validate_target(&window_table, sender,
                                              request.window_id,
                                              request.window_generation,
                                              &entry);
    }
    uint32_t id = entry != 0 ? entry->window_id : 0;
    uint32_t generation = entry != 0 ? entry->window_generation : 0;
    if (result == OS_SUCCESS) {
        destroy_entry(entry, 0);
        if (window_table.count == 0) {
            long release_result = release_gui_session();
            if (release_result < 0 && release_result != OS_ERR_NOT_READY) {
                result = release_result;
            }
        } else if (compose_and_present() < 0) {
            pending_full_frame = 1;
        }
    }
    send_window_reply(sender, request.request_id, OS_WINDOW_DESTROY,
                      (int32_t)result, id, generation, 0);
    if (result == OS_SUCCESS) {
        reconcile_invalid_focus(os_time_ticks());
        os_printf("[windowd] destroyed id=%u generation=%u remaining=%u\n",
                  id, generation, window_table.count);
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
    if (message != 0 && message->type == OS_IPC_MESSAGE_EVENT) {
        handle_input_event(message);
        return;
    }
    if (message == 0 || message->type != OS_IPC_MESSAGE_REQUEST ||
        message->length < 12) {
        reject_message(message, OS_ERR_INVALID_ARGUMENT);
        return;
    }
    const uint32_t* words = (const uint32_t*)message->payload;
    switch (words[2]) {
        case OS_WINDOW_CREATE: handle_create(message); break;
        case OS_WINDOW_SET_SURFACE: handle_set_surface(message); break;
        case OS_WINDOW_DAMAGE: handle_full_damage(message); break;
        case OS_WINDOW_DESTROY: handle_destroy(message); break;
        case OS_WINDOW_SHOW: handle_visibility(message, OS_WINDOW_SHOW); break;
        case OS_WINDOW_HIDE: handle_visibility(message, OS_WINDOW_HIDE); break;
        case OS_WINDOW_FOCUS: handle_focus(message); break;
        case OS_WINDOW_GET_INFO: handle_info(message); break;
        case OS_WINDOW_MOVE: handle_move(message); break;
        case OS_WINDOW_RESIZE: handle_resize(message); break;
        case OS_WINDOW_DAMAGE_BEGIN: handle_damage_begin(message); break;
        case OS_WINDOW_DAMAGE_RECTS: handle_damage_rects(message); break;
        case OS_WINDOW_DAMAGE_COMMIT: handle_damage_commit(message); break;
        default: reject_message(message, OS_ERR_UNSUPPORTED); break;
    }
}

static void cleanup_dead_owners(void) {
    uint32_t removed = 0;
    for (uint32_t i = 0; i < OS_WINDOW_MAX_WINDOWS; i++) {
        WindowEntry* entry = &window_table.entries[i];
        if (entry->active && os_process_identity_alive(entry->owner) < 0) {
            if (damage_transaction.active &&
                identity_equal(damage_transaction.sender, entry->owner)) {
                clear_damage_transaction();
            }
            destroy_entry(entry, 1);
            removed++;
        }
    }
    if (removed != 0) {
        if (window_table.count == 0) {
            release_gui_session();
        } else if (compose_and_present() < 0) {
            pending_full_frame = 1;
        }
    }
    if (removed != 0) {
        reconcile_invalid_focus(os_time_ticks());
    }
}

static void service_deadlines(void) {
    cleanup_dead_owners();
    if (damage_transaction.active &&
        (uint32_t)(os_time_ticks() - damage_transaction.started_ticks) >=
            WINDOW_PRESENT_TIMEOUT_TICKS) {
        reject_active_damage(OS_ERR_TIMEOUT);
    }
    if (window_table.count != 0 && ensure_gui_session() < 0) {
        pending_full_frame = 1;
        return;
    }
    OsProcessIdentity previous = display_owner;
    if (refresh_display_owner() == OS_SUCCESS &&
        (!identity_equal(previous, display_owner) || pending_full_frame)) {
        if (screen_damage.count == 0) {
            window_damage_full(&screen_damage);
            compose_pending();
        }
        present_composite();
        if (!identity_equal(previous, display_owner)) {
            os_puts("[windowd] display reconnect full frame submitted");
        }
    }
}

int main(void) {
    OsThreadIdentity self;
    if (os_thread_self(&self) != OS_SUCCESS ||
        os_thread_set_affinity(self, 1u) != OS_SUCCESS) {
        os_puts("[windowd] CPU0 ownership failed");
        return 1;
    }
    window_state_init(&window_table);
    window_input_router_init(&input_router);
    os_memset(window_surfaces, 0, sizeof(window_surfaces));
    os_memset(compositor_sources, 0, sizeof(compositor_sources));
    clear_damage_transaction();
    display_owner.pid = 0;
    display_owner.generation = 0;
    input_owner.pid = 0;
    input_owner.generation = 0;
    composite_surface = 0;
    composite_pixels = 0;
    console_snapshot_surface = 0;
    console_snapshot_pixels = 0;
    gui_session_generation = 0;
    console_underlay.pixels = 0;
    console_underlay.stride_pixels = 0;
    next_frame_generation = (uint32_t)os_time_ticks() + 1u;
    pending_full_frame = 0;

    if (!query_display(&display_owner, &display_info)) {
        os_puts("[windowd] display unavailable");
        return 1;
    }
    window_damage_init(&screen_damage, display_info.width, display_info.height);
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
                            WINDOW_BACKGROUND_COLOR);
    long result = os_service_register("window", OS_SERVICE_FLAG_SYSTEM);
    if (result < 0) {
        os_printf("[windowd] register failed %ld\n", result);
        release_composite_surface();
        return 1;
    }
    notify_input_service();
    os_printf("[windowd] ready pid=%u size=%ux%u display=%u:%u capacity=%u\n",
              (uint32_t)os_getpid(), display_info.width, display_info.height,
              display_owner.pid, display_owner.generation,
              OS_WINDOW_MAX_WINDOWS);

    while (1) {
        OsIpcMessageV2 message;
        result = os_msg_v2_wait_timeout(&message, WINDOW_FRAME_DEADLINE_TICKS);
        if (result == OS_ERR_TIMEOUT) {
            service_deadlines();
            continue;
        }
        if (result < 0) {
            os_printf("[windowd] wait failed %ld\n", result);
            for (uint32_t i = 0; i < OS_WINDOW_MAX_WINDOWS; i++) {
                release_window_surface(i);
            }
            release_composite_surface();
            return 1;
        }
        handle_message(&message);
        service_deadlines();
    }
}
