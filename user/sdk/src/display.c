#include "os64/os64.h"

static void copy_bytes(uint8_t* destination, const uint8_t* source, uint32_t size) {
    for (uint32_t i = 0; i < size; i++) {
        destination[i] = source[i];
    }
}

static long wait_for_reply(OsProcessIdentity display,
                           uint32_t request_id,
                           uint32_t timeout_ticks,
                           OsDisplayPresentReply* reply) {
    OsIpcReceiveFilter filter;
    os_ipc_filter_init(&filter);
    filter.flags = OS_IPC_FILTER_SENDER | OS_IPC_FILTER_TYPE | OS_IPC_FILTER_REPLY_TO;
    filter.sender_pid = display.pid;
    filter.sender_generation = display.generation;
    filter.type = OS_IPC_MESSAGE_REPLY;
    filter.reply_to = request_id;

    uint32_t start = (uint32_t)os_time_ticks();
    while (1) {
        OsIpcMessageV2 message;
        long result = os_msg_v2_recv_match(&filter, &message);
        if (result == OS_SUCCESS) {
            if (message.length != sizeof(*reply)) {
                return OS_ERR_BAD_BUFFER;
            }
            copy_bytes((uint8_t*)reply, message.payload, sizeof(*reply));
            if (reply->size != sizeof(*reply) ||
                reply->abi_version != OS64_DISPLAY_ABI_VERSION ||
                reply->command != OS_DISPLAY_PRESENT_REPLY ||
                reply->request_id != request_id) {
                return OS_ERR_BAD_BUFFER;
            }
            return reply->result;
        }
        if (result != OS_ERR_WOULD_BLOCK) {
            return result;
        }
        if (timeout_ticks != 0 &&
            (uint32_t)(os_time_ticks() - start) >= timeout_ticks) {
            return OS_ERR_TIMEOUT;
        }
        os_sleep(1);
    }
}

long os_display_present(OsProcessIdentity display,
                        OsHandle surface,
                        uint32_t frame_generation,
                        const OsRect* rects,
                        uint32_t rect_count,
                        uint32_t timeout_ticks,
                        OsDisplayPresentReply* reply) {
    if (display.pid == 0 || surface == 0 || frame_generation == 0 ||
        reply == 0 || rect_count > OS_DISPLAY_DAMAGE_MAX_RECTS ||
        (rect_count != 0 && rects == 0)) {
        return OS_ERR_INVALID_ARGUMENT;
    }

    OsGraphicsSurfaceHandleInfo info;
    long result = os_surface_get_info(surface, &info);
    if (result < 0) {
        return result;
    }

    uint32_t request_id = os_msg_next_request_id();
    OsDisplayPresentBegin begin;
    begin.size = sizeof(begin);
    begin.abi_version = OS64_DISPLAY_ABI_VERSION;
    begin.command = OS_DISPLAY_PRESENT_BEGIN;
    begin.flags = rect_count == 0 ? OS_DISPLAY_PRESENT_FLAG_FULL_FRAME : 0;
    begin.request_id = request_id;
    begin.frame_generation = frame_generation;
    begin.width = info.width;
    begin.height = info.height;
    begin.stride_pixels = info.stride_pixels;
    begin.pixel_format = info.pixel_format;
    begin.rect_count = rect_count;
    begin.chunk_count = rect_count == 0 ? 0 :
        (rect_count + OS_DISPLAY_DAMAGE_RECTS_PER_CHUNK - 1u) /
            OS_DISPLAY_DAMAGE_RECTS_PER_CHUNK;

    OsIpcMessageV2 message;
    os_msg_v2_init(&message, OS_IPC_MESSAGE_REQUEST);
    message.flags = OS_IPC_FLAG_REQUEST_REPLY | OS_IPC_FLAG_HAS_HANDLES;
    message.request_id = request_id;
    message.handle_count = 1;
    message.handles[0] = surface;
    message.length = sizeof(begin);
    copy_bytes(message.payload, (const uint8_t*)&begin, sizeof(begin));
    result = os_msg_v2_send_to_identity(display, &message);
    if (result < 0) {
        return result;
    }

    uint32_t offset = 0;
    for (uint32_t chunk_index = 0; chunk_index < begin.chunk_count; chunk_index++) {
        OsDisplayPresentDamage damage;
        damage.size = sizeof(damage);
        damage.abi_version = OS64_DISPLAY_ABI_VERSION;
        damage.command = OS_DISPLAY_PRESENT_DAMAGE;
        damage.flags = 0;
        damage.request_id = request_id;
        damage.frame_generation = frame_generation;
        damage.chunk_index = chunk_index;
        damage.rect_count = rect_count - offset;
        if (damage.rect_count > OS_DISPLAY_DAMAGE_RECTS_PER_CHUNK) {
            damage.rect_count = OS_DISPLAY_DAMAGE_RECTS_PER_CHUNK;
        }
        for (uint32_t i = 0; i < OS_DISPLAY_DAMAGE_RECTS_PER_CHUNK; i++) {
            if (i < damage.rect_count) {
                damage.rects[i] = rects[offset + i];
            } else {
                damage.rects[i].x = 0;
                damage.rects[i].y = 0;
                damage.rects[i].width = 0;
                damage.rects[i].height = 0;
            }
        }
        offset += damage.rect_count;

        os_msg_v2_init(&message, OS_IPC_MESSAGE_REQUEST);
        message.request_id = request_id;
        message.length = sizeof(damage);
        copy_bytes(message.payload, (const uint8_t*)&damage, sizeof(damage));
        result = os_msg_v2_send_to_identity(display, &message);
        if (result < 0) {
            return result;
        }
    }

    OsDisplayPresentCommit commit;
    commit.size = sizeof(commit);
    commit.abi_version = OS64_DISPLAY_ABI_VERSION;
    commit.command = OS_DISPLAY_PRESENT_COMMIT;
    commit.flags = 0;
    commit.request_id = request_id;
    commit.frame_generation = frame_generation;
    os_msg_v2_init(&message, OS_IPC_MESSAGE_REQUEST);
    message.flags = OS_IPC_FLAG_REQUEST_REPLY;
    message.request_id = request_id;
    message.length = sizeof(commit);
    copy_bytes(message.payload, (const uint8_t*)&commit, sizeof(commit));
    result = os_msg_v2_send_to_identity(display, &message);
    if (result < 0) {
        return result;
    }
    return wait_for_reply(display, request_id, timeout_ticks, reply);
}
