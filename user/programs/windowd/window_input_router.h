#ifndef OS64_WINDOWD_INPUT_ROUTER_H
#define OS64_WINDOWD_INPUT_ROUTER_H

#include <os64/process_types.h>

typedef struct WindowInputRouter {
    OsProcessIdentity focused_owner;
    uint32_t focused_window_id;
    uint32_t focused_window_generation;
    uint64_t focused_since_ticks;
    uint32_t input_sequence;
    uint32_t event_sequence;
} WindowInputRouter;

typedef struct WindowFocusEndpoint {
    OsProcessIdentity owner;
    uint32_t window_id;
    uint32_t window_generation;
    uint32_t event_sequence;
} WindowFocusEndpoint;

typedef struct WindowFocusChange {
    WindowFocusEndpoint focus_out;
    WindowFocusEndpoint focus_in;
} WindowFocusChange;

void window_input_router_init(WindowInputRouter* router);
void window_input_router_reset(WindowInputRouter* router);
int window_input_router_is_focused(const WindowInputRouter* router,
                                   OsProcessIdentity owner,
                                   uint32_t window_id,
                                   uint32_t window_generation);
void window_input_router_focus(WindowInputRouter* router,
                               OsProcessIdentity owner,
                               uint32_t window_id,
                               uint32_t window_generation,
                               uint64_t focused_since_ticks,
                               WindowFocusChange* change);
void window_input_router_clear(WindowInputRouter* router,
                               WindowFocusChange* change);
long window_input_router_accept_input(WindowInputRouter* router,
                                      uint32_t input_sequence);
uint32_t window_input_router_next_event(WindowInputRouter* router);

#endif
