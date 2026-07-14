#include <os64/os64.h>

#include "../sdk/src/display_server_protocol.h"

#define DISPLAY_TRANSACTION_TIMEOUT_TICKS 50u

static OsDisplayPresentTransaction transaction;
static OsHandle transaction_surface;
static void* transaction_mapping;
static uint32_t transaction_deadline;

static int identity_equal(OsProcessIdentity left, OsProcessIdentity right) {
    return left.pid != 0 && left.pid == right.pid &&
           left.generation != 0 && left.generation == right.generation;
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
    for (uint32_t i = 0; i < message->handle_count && i < OS_IPC_V2_MAX_HANDLES; i++) {
        close_handle(message->handles[i]);
    }
}

static void release_transaction(void) {
    if (transaction_mapping != 0 && transaction_surface != 0) {
        os_surface_unmap(transaction_surface, transaction_mapping);
    }
    transaction_mapping = 0;
    close_handle(transaction_surface);
    transaction_surface = 0;
    transaction_deadline = 0;
    os_display_server_protocol_abort(&transaction);
}

static void send_info_reply(OsProcessIdentity target,
                            const OsServiceQueryRequest* request,
                            int result,
                            const OsGraphicsInfo* info) {
    OsDisplayServiceInfoReply reply;
    reply.size = sizeof(reply);
    reply.command = request != 0 ? request->command : OS_SERVICE_QUERY_DISPLAY_INFO;
    reply.result = result;
    reply.request_id = request != 0 ? request->request_id : 0;
    reply.abi_version = OS64_SERVICE_PROTOCOL_ABI_VERSION;
    reply.ready = result == OS_SUCCESS ? 1u : 0u;
    reply.width = info != 0 ? info->width : 0;
    reply.height = info != 0 ? info->height : 0;
    reply.pixels_per_scanline = info != 0 ? info->pixels_per_scanline : 0;
    reply.format = info != 0 ? info->format : 0;
    reply.capabilities = OS_SERVICE_CAP_FRAMEBUFFER_INFO;
    reply.reserved = 0;

    OsIpcMessage message;
    os_msg_init(&message, OS_IPC_MESSAGE_REPLY);
    message.length = sizeof(reply);
    os_memcpy(message.payload, &reply, sizeof(reply));
    os_msg_send_to_identity(target, &message);
}

static void send_present_reply(OsProcessIdentity target,
                               uint32_t request_id,
                               int32_t result,
                               uint32_t generation,
                               uint32_t presented_rects) {
    if (target.pid == 0 || request_id == 0) {
        return;
    }
    OsDisplayPresentReply reply;
    reply.size = sizeof(reply);
    reply.abi_version = OS64_DISPLAY_ABI_VERSION;
    reply.command = OS_DISPLAY_PRESENT_REPLY;
    reply.flags = 0;
    reply.result = result;
    reply.request_id = request_id;
    reply.accepted_generation = result == OS_SUCCESS ? generation :
        transaction.last_accepted_generation;
    reply.presented_rects = result == OS_SUCCESS ? presented_rects : 0;

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
    if (request.size != sizeof(request)) {
        return 0;
    }
    OsProcessIdentity sender = os_msg_v2_sender_identity(message);
    if (request.command == OS_SERVICE_QUERY_HEALTH) {
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
        os_msg_send_to_identity(sender, &response);
        return 1;
    }
    OsGraphicsInfo info;
    int result = OS_ERR_UNSUPPORTED;
    if (request.command == OS_SERVICE_QUERY_STATUS ||
        request.command == OS_SERVICE_QUERY_DISPLAY_INFO) {
        result = (int)os_gfx_get_info(&info);
    }
    send_info_reply(sender, &request, result, result == OS_SUCCESS ? &info : 0);
    return 1;
}

static void reject_message(const OsIpcMessageV2* message, int32_t result) {
    uint32_t request_id = message != 0 ? message->request_id : 0;
    uint32_t generation = 0;
    if (message != 0 && message->length >= 24) {
        const uint32_t* words = (const uint32_t*)message->payload;
        if (words[4] != 0) {
            request_id = words[4];
        }
        generation = words[5];
    }
    send_present_reply(os_msg_v2_sender_identity(message),
                       request_id,
                       result,
                       generation,
                       0);
}

static void handle_begin(const OsIpcMessageV2* message) {
    OsProcessIdentity sender = os_msg_v2_sender_identity(message);
    if (message->length != sizeof(OsDisplayPresentBegin) ||
        message->handle_count != 1 ||
        !(message->flags & OS_IPC_FLAG_HAS_HANDLES)) {
        close_message_handles(message);
        reject_message(message, OS_ERR_INVALID_ARGUMENT);
        return;
    }
    OsDisplayPresentBegin begin;
    os_memcpy(&begin, message->payload, sizeof(begin));
    if (os_display_server_protocol_should_replace(&transaction, &begin)) {
        os_puts("[displayd] newer full frame replaced pending transaction");
        release_transaction();
    }
    long result = os_display_server_protocol_begin(&transaction, sender, &begin);
    if (result < 0) {
        close_message_handles(message);
        send_present_reply(sender, begin.request_id, (int32_t)result,
                           begin.frame_generation, 0);
        return;
    }

    OsGraphicsInfo display_info;
    OsGraphicsSurfaceHandleInfo surface_info;
    OsHandle surface = message->handles[0];
    if (os_gfx_get_info(&display_info) != OS_SUCCESS ||
        os_surface_get_info(surface, &surface_info) != OS_SUCCESS ||
        begin.width != display_info.width || begin.height != display_info.height ||
        begin.width != surface_info.width || begin.height != surface_info.height ||
        begin.stride_pixels != surface_info.stride_pixels ||
        begin.pixel_format != surface_info.pixel_format) {
        close_handle(surface);
        os_display_server_protocol_abort(&transaction);
        send_present_reply(sender, begin.request_id, OS_ERR_INVALID_ARGUMENT,
                           begin.frame_generation, 0);
        return;
    }
    void* mapping = os_surface_map(surface, OS_SURFACE_MAP_READ);
    if (mapping == 0) {
        close_handle(surface);
        os_display_server_protocol_abort(&transaction);
        send_present_reply(sender, begin.request_id, OS_ERR_PERMISSION_DENIED,
                           begin.frame_generation, 0);
        return;
    }
    transaction_surface = surface;
    transaction_mapping = mapping;
    transaction_deadline = (uint32_t)os_time_ticks() +
        DISPLAY_TRANSACTION_TIMEOUT_TICKS;
}

static void handle_damage(const OsIpcMessageV2* message) {
    if (message->handle_count != 0 ||
        message->length != sizeof(OsDisplayPresentDamage)) {
        close_message_handles(message);
        reject_message(message, OS_ERR_INVALID_ARGUMENT);
        return;
    }
    OsDisplayPresentDamage damage;
    os_memcpy(&damage, message->payload, sizeof(damage));
    OsProcessIdentity sender = os_msg_v2_sender_identity(message);
    long result = os_display_server_protocol_damage(&transaction, sender, &damage);
    if (result < 0) {
        send_present_reply(sender, damage.request_id, (int32_t)result,
                           damage.frame_generation, 0);
        if (identity_equal(sender, transaction.sender)) {
            release_transaction();
        }
        return;
    }
    transaction_deadline = (uint32_t)os_time_ticks() +
        DISPLAY_TRANSACTION_TIMEOUT_TICKS;
}

static void handle_commit(const OsIpcMessageV2* message) {
    if (message->handle_count != 0 ||
        message->length != sizeof(OsDisplayPresentCommit)) {
        close_message_handles(message);
        reject_message(message, OS_ERR_INVALID_ARGUMENT);
        return;
    }
    OsDisplayPresentCommit commit;
    os_memcpy(&commit, message->payload, sizeof(commit));
    OsProcessIdentity sender = os_msg_v2_sender_identity(message);
    long result = os_display_server_protocol_commit(&transaction, sender, &commit);
    if (result < 0) {
        send_present_reply(sender, commit.request_id, (int32_t)result,
                           commit.frame_generation, 0);
        if (identity_equal(sender, transaction.sender)) {
            release_transaction();
        }
        return;
    }

    OsRect full_rect;
    const OsRect* rects = transaction.rects;
    uint32_t rect_count = transaction.rect_count;
    if (transaction.flags & OS_DISPLAY_PRESENT_FLAG_FULL_FRAME) {
        OsGraphicsInfo info;
        if (os_gfx_get_info(&info) != OS_SUCCESS) {
            send_present_reply(sender, commit.request_id, OS_ERR_NOT_READY,
                               commit.frame_generation, 0);
            return;
        }
        full_rect.x = 0;
        full_rect.y = 0;
        full_rect.width = (int32_t)info.width;
        full_rect.height = (int32_t)info.height;
        rects = &full_rect;
        rect_count = 1;
    }
    result = os_gfx_present_surface(transaction_surface, rects, rect_count);
    if (result < 0) {
        send_present_reply(sender, commit.request_id, (int32_t)result,
                           commit.frame_generation, 0);
        transaction_deadline = (uint32_t)os_time_ticks() +
            DISPLAY_TRANSACTION_TIMEOUT_TICKS;
        return;
    }
    uint32_t generation = transaction.frame_generation;
    uint32_t request_id = transaction.request_id;
    uint32_t presented_rects = (uint32_t)result;
    os_display_server_protocol_accept(&transaction);
    if (transaction_mapping != 0) {
        os_surface_unmap(transaction_surface, transaction_mapping);
    }
    transaction_mapping = 0;
    close_handle(transaction_surface);
    transaction_surface = 0;
    transaction_deadline = 0;
    send_present_reply(sender, request_id, OS_SUCCESS, generation, presented_rects);
    os_printf("[displayd] accepted generation=%u rects=%u\n",
              generation,
              presented_rects);
}

static void handle_message(const OsIpcMessageV2* message) {
    if (handle_service_query(message)) {
        return;
    }
    if (message == 0 || message->type != OS_IPC_MESSAGE_REQUEST ||
        message->length < 12) {
        close_message_handles(message);
        reject_message(message, OS_ERR_INVALID_ARGUMENT);
        return;
    }
    const uint32_t* words = (const uint32_t*)message->payload;
    if (words[2] == OS_DISPLAY_PRESENT_BEGIN) {
        handle_begin(message);
    } else if (words[2] == OS_DISPLAY_PRESENT_DAMAGE) {
        handle_damage(message);
    } else if (words[2] == OS_DISPLAY_PRESENT_COMMIT) {
        handle_commit(message);
    } else {
        close_message_handles(message);
        reject_message(message, OS_ERR_UNSUPPORTED);
    }
}

int main(void) {
    os_display_server_protocol_init(&transaction);
    transaction_surface = 0;
    transaction_mapping = 0;
    transaction_deadline = 0;

    long result = os_service_register("display", OS_SERVICE_FLAG_SYSTEM);
    if (result < 0) {
        os_printf("[displayd] register failed %ld\n", result);
        return 1;
    }
    OsGraphicsInfo info;
    result = os_gfx_get_info(&info);
    if (result < 0) {
        os_printf("[displayd] backend unavailable %ld\n", result);
        return 1;
    }
    os_printf("[displayd] ready pid=%u size=%ux%u\n",
              (uint32_t)os_getpid(), info.width, info.height);

    while (1) {
        OsIpcMessageV2 message;
        result = os_msg_v2_wait_timeout(&message,
                                        transaction.active
                                            ? DISPLAY_TRANSACTION_TIMEOUT_TICKS
                                            : 0);
        if (result == OS_ERR_TIMEOUT) {
            if (transaction.active &&
                (int32_t)((uint32_t)os_time_ticks() - transaction_deadline) >= 0) {
                os_puts("[displayd] transaction timeout");
                release_transaction();
            }
            continue;
        }
        if (result < 0) {
            os_printf("[displayd] wait failed %ld\n", result);
            release_transaction();
            return 1;
        }
        handle_message(&message);
    }
}
