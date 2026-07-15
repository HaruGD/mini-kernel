#ifndef OS64_WINDOWD_INPUT_ROUTER_H
#define OS64_WINDOWD_INPUT_ROUTER_H

#include <os64/process_types.h>

typedef struct WindowInputRouter {
    OsProcessIdentity focused_owner;
    uint32_t focused_window_id;
    uint32_t event_sequence;
} WindowInputRouter;

void window_input_router_init(WindowInputRouter* router);
void window_input_router_reset(WindowInputRouter* router);

#endif
