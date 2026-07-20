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
    kernel_spinlock_init(&queue->lock, KERNEL_LOCK_CLASS_IPC_SERVICE, "input_event_queue");
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
    KernelInputEventQueueStats stats;
    input_event_queue_get_stats(queue, &stats);
    return stats.count;
}

uint32_t input_event_queue_delivered_count(const KernelInputEventQueue* queue) {
    KernelInputEventQueueStats stats;
    input_event_queue_get_stats(queue, &stats);
    return stats.delivered_count;
}

uint32_t input_event_queue_dropped_count(const KernelInputEventQueue* queue) {
    KernelInputEventQueueStats stats;
    input_event_queue_get_stats(queue, &stats);
    return stats.dropped_count;
}

void input_event_queue_get_stats(const KernelInputEventQueue* queue,
                                 KernelInputEventQueueStats* stats) {
    if (stats == 0) {
        return;
    }
    stats->capacity = INPUT_EVENT_QUEUE_CAPACITY;
    stats->count = 0;
    stats->delivered_count = 0;
    stats->dropped_count = 0;
    if (queue == 0) {
        return;
    }
    KernelInputEventQueue* mutable_queue = (KernelInputEventQueue*)queue;
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&mutable_queue->lock, &token)) {
        return;
    }
    stats->count = queue->count;
    stats->delivered_count = queue->delivered_count;
    stats->dropped_count = queue->dropped_count;
    kernel_spinlock_release(&mutable_queue->lock, &token);
}

int input_event_queue_is_empty(const KernelInputEventQueue* queue) {
    return input_event_queue_count(queue) == 0;
}

int input_event_queue_is_full(const KernelInputEventQueue* queue) {
    return input_event_queue_count(queue) == INPUT_EVENT_QUEUE_CAPACITY;
}

static int input_event_queue_has_key_unlocked(const KernelInputEventQueue* queue,
                                              int character_only) {
    for (uint32_t i = 0; i < queue->count; i++) {
        uint32_t index = (queue->head + i) % INPUT_EVENT_QUEUE_CAPACITY;
        const OsInputEvent* event = &queue->events[index];
        if (event->type != OS_INPUT_EVENT_KEY) {
            continue;
        }
        if (!character_only ||
            (event->data.key.type == OS_KEY_EVENT_DOWN &&
             event->data.key.character != 0)) {
            return 1;
        }
    }
    return 0;
}

int input_event_queue_has_key(const KernelInputEventQueue* queue) {
    if (queue == 0) {
        return 0;
    }
    KernelInputEventQueue* mutable_queue = (KernelInputEventQueue*)queue;
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&mutable_queue->lock, &token)) {
        return 0;
    }
    int result = input_event_queue_has_key_unlocked(queue, 0);
    kernel_spinlock_release(&mutable_queue->lock, &token);
    return result;
}

int input_event_queue_has_character(const KernelInputEventQueue* queue) {
    if (queue == 0) {
        return 0;
    }
    KernelInputEventQueue* mutable_queue = (KernelInputEventQueue*)queue;
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&mutable_queue->lock, &token)) {
        return 0;
    }
    int result = input_event_queue_has_key_unlocked(queue, 1);
    kernel_spinlock_release(&mutable_queue->lock, &token);
    return result;
}

static int push_unlocked(KernelInputEventQueue* queue, const OsInputEvent* event) {
    if (queue->count == INPUT_EVENT_QUEUE_CAPACITY) {
        return 0;
    }
    queue->events[queue->tail] = *event;
    queue->tail = (queue->tail + 1u) % INPUT_EVENT_QUEUE_CAPACITY;
    queue->count++;
    return 1;
}

int input_event_queue_push(KernelInputEventQueue* queue, const OsInputEvent* event) {
    if (queue == 0 || event == 0) {
        return 0;
    }
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&queue->lock, &token)) {
        return 0;
    }
    int result = push_unlocked(queue, event);
    kernel_spinlock_release(&queue->lock, &token);
    return result;
}

int input_event_queue_push_drop_oldest(KernelInputEventQueue* queue, const OsInputEvent* event) {
    if (queue == 0 || event == 0) {
        return 0;
    }
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&queue->lock, &token)) {
        return 0;
    }
    if (queue->count == INPUT_EVENT_QUEUE_CAPACITY) {
        clear_event(&queue->events[queue->head]);
        queue->head = (queue->head + 1u) % INPUT_EVENT_QUEUE_CAPACITY;
        queue->count--;
        queue->dropped_count++;
    }
    int result = push_unlocked(queue, event);
    kernel_spinlock_release(&queue->lock, &token);
    return result;
}

int input_event_queue_pop(KernelInputEventQueue* queue, OsInputEvent* event) {
    if (queue == 0 || event == 0) {
        return 0;
    }
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&queue->lock, &token)) {
        return 0;
    }
    if (queue->count == 0) {
        kernel_spinlock_release(&queue->lock, &token);
        return 0;
    }

    *event = queue->events[queue->head];
    clear_event(&queue->events[queue->head]);
    queue->head = (queue->head + 1u) % INPUT_EVENT_QUEUE_CAPACITY;
    queue->count--;
    queue->delivered_count++;
    kernel_spinlock_release(&queue->lock, &token);
    return 1;
}
