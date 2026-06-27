#include "kernel/input/input_events.h"

static KernelInputEventQueue input_queue;

void input_events_init() {
    input_event_queue_init(&input_queue);
}

int input_events_push(const OsInputEvent* event) {
    return input_event_queue_push_drop_oldest(&input_queue, event);
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
