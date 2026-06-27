#include "kernel/input/input_event_queue.h"

static void clear_event(OsInputEvent* event) {
    if (event == 0) {
        return;
    }
    event->type = OS_INPUT_EVENT_NONE;
    event->size = sizeof(OsInputEvent);
    event->timestamp_ticks = 0;
    for (uint32_t i = 0; i < sizeof(event->data); i++) {
        ((uint8_t*)&event->data)[i] = 0;
    }
}

void input_event_queue_init(KernelInputEventQueue* queue) {
    if (queue == 0) {
        return;
    }
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
    queue->delivered_count = 0;
    queue->dropped_count = 0;
    for (uint32_t i = 0; i < INPUT_EVENT_QUEUE_CAPACITY; i++) {
        clear_event(&queue->events[i]);
    }
}

uint32_t input_event_queue_capacity(const KernelInputEventQueue* queue) {
    return queue == 0 ? 0 : INPUT_EVENT_QUEUE_CAPACITY;
}

uint32_t input_event_queue_count(const KernelInputEventQueue* queue) {
    return queue == 0 ? 0 : queue->count;
}

uint32_t input_event_queue_delivered_count(const KernelInputEventQueue* queue) {
    return queue == 0 ? 0 : queue->delivered_count;
}

uint32_t input_event_queue_dropped_count(const KernelInputEventQueue* queue) {
    return queue == 0 ? 0 : queue->dropped_count;
}

int input_event_queue_is_empty(const KernelInputEventQueue* queue) {
    return queue == 0 || queue->count == 0;
}

int input_event_queue_is_full(const KernelInputEventQueue* queue) {
    return queue != 0 && queue->count == INPUT_EVENT_QUEUE_CAPACITY;
}

int input_event_queue_push(KernelInputEventQueue* queue, const OsInputEvent* event) {
    if (queue == 0 || event == 0 || input_event_queue_is_full(queue)) {
        return 0;
    }

    queue->events[queue->tail] = *event;
    queue->tail = (queue->tail + 1u) % INPUT_EVENT_QUEUE_CAPACITY;
    queue->count++;
    return 1;
}

int input_event_queue_push_drop_oldest(KernelInputEventQueue* queue, const OsInputEvent* event) {
    if (queue == 0 || event == 0) {
        return 0;
    }
    if (input_event_queue_is_full(queue)) {
        clear_event(&queue->events[queue->head]);
        queue->head = (queue->head + 1u) % INPUT_EVENT_QUEUE_CAPACITY;
        queue->count--;
        queue->dropped_count++;
    }
    return input_event_queue_push(queue, event);
}

int input_event_queue_pop(KernelInputEventQueue* queue, OsInputEvent* event) {
    if (queue == 0 || event == 0 || input_event_queue_is_empty(queue)) {
        return 0;
    }

    *event = queue->events[queue->head];
    clear_event(&queue->events[queue->head]);
    queue->head = (queue->head + 1u) % INPUT_EVENT_QUEUE_CAPACITY;
    queue->count--;
    queue->delivered_count++;
    return 1;
}
