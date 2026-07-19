#ifndef KERNEL_GRAPHICS_DISPLAY_OWNER_H
#define KERNEL_GRAPHICS_DISPLAY_OWNER_H

#include <stdint.h>

#include "os64/display_types.h"

#define DISPLAY_OWNER_NONE 0u
#define DISPLAY_OWNER_TERMINAL 1u
#define DISPLAY_OWNER_GOP 2u

typedef struct DisplayOwnerToken {
    uint64_t flags;
    uint32_t owner;
    uint32_t acquired;
} DisplayOwnerToken;

typedef struct DisplayOwnerStats {
    uint32_t current_owner;
    uint32_t depth;
    uint64_t acquire_count;
    uint64_t busy_count;
} DisplayOwnerStats;

#ifdef __cplusplus
extern "C" {
#endif

void display_owner_begin(uint32_t owner, DisplayOwnerToken* token);
void display_owner_end(DisplayOwnerToken* token);
uint32_t display_owner_current();
void display_owner_get_stats(DisplayOwnerStats* out_stats);
int display_session_begin(uint32_t window_pid,
                          uint32_t window_generation,
                          uint32_t display_pid,
                          uint32_t display_generation,
                          uint32_t width,
                          uint32_t height,
                          uint32_t* generation_out);
int display_session_commit(uint32_t generation);
int display_session_begin_release(uint32_t window_pid,
                                  uint32_t window_generation,
                                  uint32_t generation);
int display_session_begin_recovery(uint32_t pid, uint32_t generation);
void display_session_finish_restore();
void display_session_abort_acquire(uint32_t generation);
int display_session_gui_active();
int display_session_terminal_scanout_allowed();
int display_session_present_allowed(uint32_t display_pid,
                                    uint32_t display_generation);
void display_session_get_info(OsDisplaySessionInfo* info);

#ifdef __cplusplus
}
#endif

#endif
