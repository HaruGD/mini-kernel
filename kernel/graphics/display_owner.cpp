#include "kernel/graphics/display_owner.h"

static uint32_t owner_state = DISPLAY_OWNER_NONE;
static uint32_t owner_depth = 0;
static uint64_t acquire_count = 0;
static uint64_t busy_count = 0;

static uint64_t irq_save() {
    uint64_t flags;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static void irq_restore(uint64_t flags) {
    if (flags & (1ull << 9)) {
        __asm__ volatile("sti" : : : "memory");
    }
}

void display_owner_begin(uint32_t owner, DisplayOwnerToken* token) {
    if (token == 0) {
        return;
    }

    token->flags = irq_save();
    token->owner = owner;
    token->acquired = 0;

    if (owner == DISPLAY_OWNER_NONE) {
        irq_restore(token->flags);
        return;
    }
    if (owner_state != DISPLAY_OWNER_NONE && owner_state != owner) {
        busy_count++;
        irq_restore(token->flags);
        return;
    }

    owner_state = owner;
    owner_depth++;
    acquire_count++;
    token->acquired = 1;
}

void display_owner_end(DisplayOwnerToken* token) {
    if (token == 0 || !token->acquired) {
        return;
    }

    if (owner_state == token->owner && owner_depth > 0) {
        owner_depth--;
        if (owner_depth == 0) {
            owner_state = DISPLAY_OWNER_NONE;
        }
    }
    token->acquired = 0;
    irq_restore(token->flags);
}

uint32_t display_owner_current() {
    return owner_state;
}

void display_owner_get_stats(DisplayOwnerStats* out_stats) {
    if (out_stats == 0) {
        return;
    }
    out_stats->current_owner = owner_state;
    out_stats->depth = owner_depth;
    out_stats->acquire_count = acquire_count;
    out_stats->busy_count = busy_count;
}
