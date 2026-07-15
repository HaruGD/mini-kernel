#ifndef OS64_WINDOWD_STATE_H
#define OS64_WINDOWD_STATE_H

#include <stdint.h>

#include <os64/process_types.h>

typedef struct WindowSingleState {
    uint32_t active;
    uint32_t window_id;
    uint32_t window_generation;
    uint32_t accepted_content_generation;
    uint32_t next_window_generation;
    OsProcessIdentity owner;
} WindowSingleState;

void window_state_init(WindowSingleState* state);
long window_state_can_create(const WindowSingleState* state,
                             OsProcessIdentity owner,
                             uint32_t content_generation);
void window_state_commit_create(WindowSingleState* state,
                                OsProcessIdentity owner,
                                uint32_t content_generation);
long window_state_validate_target(const WindowSingleState* state,
                                  OsProcessIdentity sender,
                                  uint32_t window_id,
                                  uint32_t window_generation);
long window_state_validate_content(const WindowSingleState* state,
                                   OsProcessIdentity sender,
                                   uint32_t window_id,
                                   uint32_t window_generation,
                                   uint32_t content_generation);
void window_state_commit_content(WindowSingleState* state,
                                 uint32_t content_generation);
void window_state_destroy(WindowSingleState* state);

#endif
