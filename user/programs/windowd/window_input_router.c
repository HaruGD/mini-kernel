#include "window_input_router.h"

void window_input_router_init(WindowInputRouter* router) {
    if (router == 0) {
        return;
    }
    router->focused_owner.pid = 0;
    router->focused_owner.generation = 0;
    router->focused_window_id = 0;
    router->event_sequence = 0;
}

void window_input_router_reset(WindowInputRouter* router) {
    window_input_router_init(router);
}
