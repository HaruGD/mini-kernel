#include "display_server_protocol.h"

#include "os64/result.h"

static int identity_equal(OsProcessIdentity left, OsProcessIdentity right) {
    return left.pid != 0 && left.pid == right.pid &&
           left.generation != 0 && left.generation == right.generation;
}

static void clear_active(OsDisplayPresentTransaction* transaction) {
    transaction->active = 0;
    transaction->sender.pid = 0;
    transaction->sender.generation = 0;
    transaction->request_id = 0;
    transaction->frame_generation = 0;
    transaction->flags = 0;
    transaction->expected_rects = 0;
    transaction->expected_chunks = 0;
    transaction->next_chunk = 0;
    transaction->rect_count = 0;
    for (uint32_t i = 0; i < OS_DISPLAY_DAMAGE_MAX_RECTS; i++) {
        transaction->rects[i].x = 0;
        transaction->rects[i].y = 0;
        transaction->rects[i].width = 0;
        transaction->rects[i].height = 0;
    }
}

void os_display_server_protocol_init(OsDisplayPresentTransaction* transaction) {
    if (transaction == 0) {
        return;
    }
    transaction->last_accepted_generation = 0;
    clear_active(transaction);
}

void os_display_server_protocol_abort(OsDisplayPresentTransaction* transaction) {
    if (transaction != 0) {
        clear_active(transaction);
    }
}

int os_display_server_protocol_should_replace(
    const OsDisplayPresentTransaction* transaction,
    const OsDisplayPresentBegin* begin) {
    return transaction != 0 && transaction->active && begin != 0 &&
           begin->size == sizeof(*begin) &&
           begin->abi_version == OS64_DISPLAY_ABI_VERSION &&
           begin->command == OS_DISPLAY_PRESENT_BEGIN &&
           begin->flags == OS_DISPLAY_PRESENT_FLAG_FULL_FRAME &&
           begin->request_id != 0 &&
           begin->frame_generation > transaction->frame_generation &&
           begin->rect_count == 0 && begin->chunk_count == 0;
}

long os_display_server_protocol_begin(OsDisplayPresentTransaction* transaction,
                                      OsProcessIdentity sender,
                                      const OsDisplayPresentBegin* begin) {
    if (transaction == 0 || begin == 0 || sender.pid == 0 || sender.generation == 0 ||
        begin->size != sizeof(*begin) ||
        begin->abi_version != OS64_DISPLAY_ABI_VERSION ||
        begin->command != OS_DISPLAY_PRESENT_BEGIN ||
        (begin->flags & ~OS_DISPLAY_PRESENT_VALID_FLAGS) != 0 ||
        begin->request_id == 0 || begin->frame_generation == 0 ||
        begin->width == 0 || begin->height == 0 ||
        begin->stride_pixels < begin->width ||
        (begin->pixel_format != OS64_PIXEL_FORMAT_RGB &&
         begin->pixel_format != OS64_PIXEL_FORMAT_BGR) ||
        begin->rect_count > OS_DISPLAY_DAMAGE_MAX_RECTS ||
        begin->chunk_count > OS_DISPLAY_DAMAGE_MAX_CHUNKS) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    if (transaction->active) {
        return OS_ERR_NOT_READY;
    }
    if (begin->frame_generation <= transaction->last_accepted_generation) {
        return OS_ERR_ALREADY_EXISTS;
    }
    if (begin->flags & OS_DISPLAY_PRESENT_FLAG_FULL_FRAME) {
        if (begin->rect_count != 0 || begin->chunk_count != 0) {
            return OS_ERR_INVALID_ARGUMENT;
        }
    } else {
        uint32_t expected_chunks =
            (begin->rect_count + OS_DISPLAY_DAMAGE_RECTS_PER_CHUNK - 1u) /
                OS_DISPLAY_DAMAGE_RECTS_PER_CHUNK;
        if (begin->rect_count == 0 || begin->chunk_count != expected_chunks) {
            return OS_ERR_INVALID_ARGUMENT;
        }
    }

    transaction->active = 1;
    transaction->sender = sender;
    transaction->request_id = begin->request_id;
    transaction->frame_generation = begin->frame_generation;
    transaction->flags = begin->flags;
    transaction->expected_rects = begin->rect_count;
    transaction->expected_chunks = begin->chunk_count;
    transaction->next_chunk = 0;
    transaction->rect_count = 0;
    return OS_SUCCESS;
}

long os_display_server_protocol_damage(OsDisplayPresentTransaction* transaction,
                                       OsProcessIdentity sender,
                                       const OsDisplayPresentDamage* damage) {
    if (transaction == 0 || damage == 0 || !transaction->active) {
        return OS_ERR_NOT_READY;
    }
    if (!identity_equal(transaction->sender, sender)) {
        return OS_ERR_PERMISSION_DENIED;
    }
    if (damage->size != sizeof(*damage) ||
        damage->abi_version != OS64_DISPLAY_ABI_VERSION ||
        damage->command != OS_DISPLAY_PRESENT_DAMAGE || damage->flags != 0 ||
        damage->request_id != transaction->request_id ||
        damage->frame_generation != transaction->frame_generation ||
        damage->chunk_index != transaction->next_chunk ||
        damage->chunk_index >= transaction->expected_chunks) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    uint32_t remaining = transaction->expected_rects - transaction->rect_count;
    uint32_t expected = remaining > OS_DISPLAY_DAMAGE_RECTS_PER_CHUNK
        ? OS_DISPLAY_DAMAGE_RECTS_PER_CHUNK
        : remaining;
    if (damage->rect_count != expected || damage->rect_count == 0) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    for (uint32_t i = 0; i < damage->rect_count; i++) {
        if (damage->rects[i].width <= 0 || damage->rects[i].height <= 0 ||
            transaction->rect_count >= OS_DISPLAY_DAMAGE_MAX_RECTS) {
            return OS_ERR_INVALID_ARGUMENT;
        }
        transaction->rects[transaction->rect_count++] = damage->rects[i];
    }
    transaction->next_chunk++;
    return OS_SUCCESS;
}

long os_display_server_protocol_commit(const OsDisplayPresentTransaction* transaction,
                                       OsProcessIdentity sender,
                                       const OsDisplayPresentCommit* commit) {
    if (transaction == 0 || commit == 0 || !transaction->active) {
        return OS_ERR_NOT_READY;
    }
    if (!identity_equal(transaction->sender, sender)) {
        return OS_ERR_PERMISSION_DENIED;
    }
    if (commit->size != sizeof(*commit) ||
        commit->abi_version != OS64_DISPLAY_ABI_VERSION ||
        commit->command != OS_DISPLAY_PRESENT_COMMIT || commit->flags != 0 ||
        commit->request_id != transaction->request_id ||
        commit->frame_generation != transaction->frame_generation ||
        transaction->next_chunk != transaction->expected_chunks ||
        transaction->rect_count != transaction->expected_rects) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    return OS_SUCCESS;
}

void os_display_server_protocol_accept(OsDisplayPresentTransaction* transaction) {
    if (transaction == 0 || !transaction->active) {
        return;
    }
    transaction->last_accepted_generation = transaction->frame_generation;
    clear_active(transaction);
}
