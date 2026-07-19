#include "kernel/input/input_events.h"
#include "kernel/process64.h"
#include "kernel/service/service_registry.h"
#include "kernel/graphics/display_owner.h"

extern "C" int display_session_gui_active() __attribute__((weak));

static int gui_session_active() {
    return display_session_gui_active != 0 && display_session_gui_active();
}

static KernelInputEventQueue input_queue;

void input_events_init() {
    input_event_queue_init(&input_queue);
}

int input_events_push(const OsInputEvent* event) {
    int result = input_event_queue_push_drop_oldest(&input_queue, event);
    int gui_active = gui_session_active();
    Process* focused = gui_active ? 0 : process_focused();
    if (focused != 0) {
        process_event_queue_push(focused, event);
        process_wait_signal(focused, PROCESS_WAIT_INPUT, PROCESS_WAIT_OK);
        if (event != 0 && event->type == OS_INPUT_EVENT_KEY) {
            process_wait_signal(focused, PROCESS_WAIT_KEY, PROCESS_WAIT_OK);
            if (event->data.key.type == OS_KEY_EVENT_DOWN &&
                event->data.key.character != 0) {
                process_wait_signal(focused, PROCESS_WAIT_CHAR, PROCESS_WAIT_OK);
            }
        }
    }
    OsProcessIdentity input_owner;
    if (gui_active &&
        service_find_owner_identity("input", &input_owner) == SERVICE_OK) {
        Process* input_service = find_process_by_identity_compat(input_owner.pid,
                                                                 input_owner.generation);
        if (input_service != 0 && input_service != focused) {
            process_wait_signal(input_service, PROCESS_WAIT_INPUT, PROCESS_WAIT_OK);
        }
    }
    return result;
}

int input_events_pop(OsInputEvent* event) {
    return input_event_queue_pop(&input_queue, event);
}

void input_events_discard_all() {
    OsInputEvent event;
    while (input_event_queue_pop(&input_queue, &event)) {
    }
}

void input_events_get_stats(KernelInputStats* stats) {
    if (stats == 0) {
        return;
    }
    stats->capacity = input_event_queue_capacity(&input_queue);
    stats->count = input_event_queue_count(&input_queue);
    stats->delivered_count = input_event_queue_delivered_count(&input_queue);
    stats->dropped_count = input_event_queue_dropped_count(&input_queue);
}
