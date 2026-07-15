#include "window_state.h"

#include <os64/result.h>
#include <os64/window_types.h>

static int identity_equal(OsProcessIdentity left, OsProcessIdentity right) {
    return left.pid != 0 && left.pid == right.pid && left.generation != 0 &&
           left.generation == right.generation;
}

void window_state_init(WindowSingleState* state) {
    if (state == 0) {
        return;
    }
    state->active = 0;
    state->window_id = 0;
    state->window_generation = 0;
    state->accepted_content_generation = 0;
    state->next_window_generation = 1;
    state->owner.pid = 0;
    state->owner.generation = 0;
}

long window_state_can_create(const WindowSingleState* state,
                             OsProcessIdentity owner,
                             uint32_t content_generation) {
    if (state == 0 || owner.pid == 0 || owner.generation == 0 ||
        content_generation == 0) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    return state->active ? OS_ERR_NO_RESOURCES : OS_SUCCESS;
}

void window_state_commit_create(WindowSingleState* state,
                                OsProcessIdentity owner,
                                uint32_t content_generation) {
    if (state == 0) {
        return;
    }
    uint32_t generation = state->next_window_generation++;
    if (generation == 0) {
        generation = state->next_window_generation++;
    }
    state->active = 1;
    state->window_id = OS_WINDOW_ID_FULLSCREEN;
    state->window_generation = generation;
    state->accepted_content_generation = content_generation;
    state->owner = owner;
}

long window_state_validate_target(const WindowSingleState* state,
                                  OsProcessIdentity sender,
                                  uint32_t window_id,
                                  uint32_t window_generation) {
    if (state == 0 || !state->active || window_id != state->window_id ||
        window_generation != state->window_generation) {
        return OS_ERR_NOT_FOUND;
    }
    return identity_equal(state->owner, sender)
        ? OS_SUCCESS
        : OS_ERR_PERMISSION_DENIED;
}

long window_state_validate_content(const WindowSingleState* state,
                                   OsProcessIdentity sender,
                                   uint32_t window_id,
                                   uint32_t window_generation,
                                   uint32_t content_generation) {
    long result = window_state_validate_target(state,
                                               sender,
                                               window_id,
                                               window_generation);
    if (result < 0) {
        return result;
    }
    if (content_generation <= state->accepted_content_generation) {
        return OS_ERR_ALREADY_EXISTS;
    }
    return OS_SUCCESS;
}

void window_state_commit_content(WindowSingleState* state,
                                 uint32_t content_generation) {
    if (state != 0 && state->active) {
        state->accepted_content_generation = content_generation;
    }
}

void window_state_destroy(WindowSingleState* state) {
    if (state == 0) {
        return;
    }
    state->active = 0;
    state->window_id = 0;
    state->window_generation = 0;
    state->accepted_content_generation = 0;
    state->owner.pid = 0;
    state->owner.generation = 0;
}
