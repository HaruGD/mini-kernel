#include "kernel/graphics/display_owner.h"

static uint32_t owner_state = DISPLAY_OWNER_NONE;
static uint32_t owner_depth = 0;
static uint64_t acquire_count = 0;
static uint64_t busy_count = 0;
static uint32_t session_state = OS_DISPLAY_SESSION_CONSOLE_ACTIVE;
static uint32_t session_generation = 0;
static uint32_t session_window_pid = 0;
static uint32_t session_window_generation = 0;
static uint32_t session_display_pid = 0;
static uint32_t session_display_generation = 0;
static uint32_t session_width = 0;
static uint32_t session_height = 0;

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

    uint64_t flags = irq_save();
    token->flags = 0;
    token->owner = owner;
    token->acquired = 0;

    if (owner == DISPLAY_OWNER_NONE) {
        irq_restore(flags);
        return;
    }
    if ((owner == DISPLAY_OWNER_TERMINAL &&
         session_state != OS_DISPLAY_SESSION_CONSOLE_ACTIVE &&
         session_state != OS_DISPLAY_SESSION_CONSOLE_RESTORING &&
         session_state != OS_DISPLAY_SESSION_FALLBACK) ||
        (owner == DISPLAY_OWNER_GOP &&
         session_state != OS_DISPLAY_SESSION_CONSOLE_ACTIVE &&
         session_state != OS_DISPLAY_SESSION_GUI_ACTIVE &&
         session_state != OS_DISPLAY_SESSION_FALLBACK)) {
        busy_count++;
        irq_restore(flags);
        return;
    }
    if (owner_state != DISPLAY_OWNER_NONE && owner_state != owner) {
        busy_count++;
        irq_restore(flags);
        return;
    }

    owner_state = owner;
    owner_depth++;
    acquire_count++;
    token->acquired = 1;
    // Ownership persists across drawing, but interrupts are disabled only for
    // the state transition. A competing interrupt-side renderer observes the
    // busy owner and skips its draw instead of extending IRQ latency.
    irq_restore(flags);
}

void display_owner_end(DisplayOwnerToken* token) {
    if (token == 0 || !token->acquired) {
        return;
    }

    uint64_t flags = irq_save();
    if (owner_state == token->owner && owner_depth > 0) {
        owner_depth--;
        if (owner_depth == 0) {
            owner_state = DISPLAY_OWNER_NONE;
        }
    }
    token->acquired = 0;
    irq_restore(flags);
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

static void clear_session_owners() {
    session_window_pid = 0;
    session_window_generation = 0;
    session_display_pid = 0;
    session_display_generation = 0;
    session_width = 0;
    session_height = 0;
}

int display_session_begin(uint32_t window_pid,
                          uint32_t window_generation,
                          uint32_t display_pid,
                          uint32_t display_generation,
                          uint32_t width,
                          uint32_t height,
                          uint32_t* generation_out) {
    if (window_pid == 0 || window_generation == 0 || display_pid == 0 ||
        display_generation == 0 || width == 0 || height == 0 ||
        generation_out == 0) {
        return 0;
    }
    uint64_t flags = irq_save();
    if ((session_state != OS_DISPLAY_SESSION_CONSOLE_ACTIVE &&
         session_state != OS_DISPLAY_SESSION_FALLBACK) ||
        owner_state != DISPLAY_OWNER_NONE) {
        irq_restore(flags);
        return 0;
    }
    session_generation++;
    if (session_generation == 0) {
        session_generation = 1;
    }
    session_window_pid = window_pid;
    session_window_generation = window_generation;
    session_display_pid = display_pid;
    session_display_generation = display_generation;
    session_width = width;
    session_height = height;
    session_state = OS_DISPLAY_SESSION_GUI_ACQUIRING;
    *generation_out = session_generation;
    irq_restore(flags);
    return 1;
}

int display_session_commit(uint32_t generation) {
    uint64_t flags = irq_save();
    int valid = session_state == OS_DISPLAY_SESSION_GUI_ACQUIRING &&
                generation != 0 && generation == session_generation;
    if (valid) {
        session_state = OS_DISPLAY_SESSION_GUI_ACTIVE;
    }
    irq_restore(flags);
    return valid;
}

void display_session_abort_acquire(uint32_t generation) {
    uint64_t flags = irq_save();
    if (session_state == OS_DISPLAY_SESSION_GUI_ACQUIRING &&
        generation == session_generation) {
        session_state = OS_DISPLAY_SESSION_FALLBACK;
        clear_session_owners();
    }
    irq_restore(flags);
}

int display_session_begin_release(uint32_t window_pid,
                                  uint32_t window_generation,
                                  uint32_t generation) {
    uint64_t flags = irq_save();
    int valid = session_state == OS_DISPLAY_SESSION_GUI_ACTIVE &&
                generation != 0 && generation == session_generation &&
                window_pid == session_window_pid &&
                window_generation == session_window_generation;
    if (valid) {
        session_state = OS_DISPLAY_SESSION_CONSOLE_RESTORING;
    }
    irq_restore(flags);
    return valid;
}

int display_session_begin_recovery(uint32_t pid, uint32_t generation) {
    if (pid == 0 || generation == 0) {
        return 0;
    }
    uint64_t flags = irq_save();
    int owner = (pid == session_window_pid &&
                 generation == session_window_generation) ||
                (pid == session_display_pid &&
                 generation == session_display_generation);
    int recover = owner &&
                  (session_state == OS_DISPLAY_SESSION_GUI_ACQUIRING ||
                   session_state == OS_DISPLAY_SESSION_GUI_ACTIVE);
    if (recover) {
        session_state = OS_DISPLAY_SESSION_CONSOLE_RESTORING;
    }
    irq_restore(flags);
    return recover;
}

void display_session_finish_restore() {
    uint64_t flags = irq_save();
    if (session_state == OS_DISPLAY_SESSION_CONSOLE_RESTORING ||
        session_state == OS_DISPLAY_SESSION_FALLBACK) {
        clear_session_owners();
        session_state = OS_DISPLAY_SESSION_CONSOLE_ACTIVE;
    }
    irq_restore(flags);
}

int display_session_gui_active() {
    return session_state == OS_DISPLAY_SESSION_GUI_ACTIVE;
}

int display_session_terminal_scanout_allowed() {
    return session_state == OS_DISPLAY_SESSION_CONSOLE_ACTIVE ||
           session_state == OS_DISPLAY_SESSION_CONSOLE_RESTORING ||
           session_state == OS_DISPLAY_SESSION_FALLBACK;
}

int display_session_present_allowed(uint32_t display_pid,
                                    uint32_t display_generation) {
    return session_state == OS_DISPLAY_SESSION_GUI_ACTIVE &&
           display_pid != 0 && display_pid == session_display_pid &&
           display_generation != 0 &&
           display_generation == session_display_generation;
}

void display_session_get_info(OsDisplaySessionInfo* info) {
    if (info == 0) {
        return;
    }
    uint64_t flags = irq_save();
    info->size = sizeof(*info);
    info->abi_version = OS64_DISPLAY_ABI_VERSION;
    info->state = session_state;
    info->generation = session_generation;
    info->window_pid = session_window_pid;
    info->window_generation = session_window_generation;
    info->display_pid = session_display_pid;
    info->display_generation = session_display_generation;
    info->width = session_width;
    info->height = session_height;
    irq_restore(flags);
}
