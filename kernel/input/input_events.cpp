#include "kernel/input/input_events.h"
#include "kernel/process64.h"

static KernelInputEventQueue input_queue;

void input_events_init() {
    input_event_queue_init(&input_queue);
}

int input_events_push(const OsInputEvent* event) {
    int result = input_event_queue_push_drop_oldest(&input_queue, event);
    Process* focused = process_focused();
    if (focused != 0) {
        process_event_queue_push(focused, event);
    }
    return result;
}

int input_events_pop(OsInputEvent* event) {
    return input_event_queue_pop(&input_queue, event);
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
