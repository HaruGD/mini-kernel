#include "window_input_router.h"

#include <os64/result.h>

static void clear_endpoint(WindowFocusEndpoint* endpoint) {
    if (endpoint != 0) {
        endpoint->owner.pid = 0;
        endpoint->owner.generation = 0;
        endpoint->window_id = 0;
        endpoint->window_generation = 0;
        endpoint->event_sequence = 0;
    }
}

static int identity_equal(OsProcessIdentity left, OsProcessIdentity right) {
    return left.pid != 0 && left.pid == right.pid &&
           left.generation != 0 && left.generation == right.generation;
}

uint32_t window_input_router_next_event(WindowInputRouter* router) {
    if (router == 0) {
        return 0;
    }
    router->event_sequence++;
    if (router->event_sequence == 0) {
        router->event_sequence++;
    }
    return router->event_sequence;
}

void window_input_router_init(WindowInputRouter* router) {
    if (router == 0) {
        return;
    }
    router->focused_owner.pid = 0;
    router->focused_owner.generation = 0;
    router->focused_window_id = 0;
    router->focused_window_generation = 0;
    router->focused_since_ticks = 0;
    router->input_sequence = 0;
    router->event_sequence = 0;
}

void window_input_router_reset(WindowInputRouter* router) {
    window_input_router_init(router);
}

int window_input_router_is_focused(const WindowInputRouter* router,
                                   OsProcessIdentity owner,
                                   uint32_t window_id,
                                   uint32_t window_generation) {
    return router != 0 && identity_equal(router->focused_owner, owner) &&
           router->focused_window_id == window_id &&
           router->focused_window_generation == window_generation;
}

void window_input_router_focus(WindowInputRouter* router,
                               OsProcessIdentity owner,
                               uint32_t window_id,
                               uint32_t window_generation,
                               uint64_t focused_since_ticks,
                               WindowFocusChange* change) {
    if (change != 0) {
        clear_endpoint(&change->focus_out);
        clear_endpoint(&change->focus_in);
    }
    if (router == 0 || owner.pid == 0 || owner.generation == 0 ||
        window_id == 0 || window_generation == 0 ||
        window_input_router_is_focused(router, owner, window_id,
                                       window_generation)) {
        return;
    }
    if (router->focused_window_id != 0 && change != 0) {
        change->focus_out.owner = router->focused_owner;
        change->focus_out.window_id = router->focused_window_id;
        change->focus_out.window_generation = router->focused_window_generation;
        change->focus_out.event_sequence = window_input_router_next_event(router);
    }
    router->focused_owner = owner;
    router->focused_window_id = window_id;
    router->focused_window_generation = window_generation;
    router->focused_since_ticks = focused_since_ticks;
    if (change != 0) {
        change->focus_in.owner = owner;
        change->focus_in.window_id = window_id;
        change->focus_in.window_generation = window_generation;
        change->focus_in.event_sequence = window_input_router_next_event(router);
    }
}

void window_input_router_clear(WindowInputRouter* router,
                               WindowFocusChange* change) {
    if (change != 0) {
        clear_endpoint(&change->focus_out);
        clear_endpoint(&change->focus_in);
    }
    if (router == 0 || router->focused_window_id == 0) {
        return;
    }
    if (change != 0) {
        change->focus_out.owner = router->focused_owner;
        change->focus_out.window_id = router->focused_window_id;
        change->focus_out.window_generation = router->focused_window_generation;
        change->focus_out.event_sequence = window_input_router_next_event(router);
    }
    router->focused_owner.pid = 0;
    router->focused_owner.generation = 0;
    router->focused_window_id = 0;
    router->focused_window_generation = 0;
    router->focused_since_ticks = 0;
}

long window_input_router_accept_input(WindowInputRouter* router,
                                      uint32_t input_sequence) {
    if (router == 0 || input_sequence == 0 ||
        (router->input_sequence != 0 && input_sequence <= router->input_sequence)) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    router->input_sequence = input_sequence;
    return OS_SUCCESS;
}
