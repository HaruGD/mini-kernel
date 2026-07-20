#ifndef KERNEL_INPUT_EVENT_QUEUE_H
#define KERNEL_INPUT_EVENT_QUEUE_H

#include <stdint.h>
#include "kernel/spinlock.h"
#include "os64/input_types.h"

#define INPUT_EVENT_QUEUE_CAPACITY 64u

typedef struct KernelInputEventQueue {
    KernelSpinlock lock;
    OsInputEvent events[INPUT_EVENT_QUEUE_CAPACITY];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    uint32_t delivered_count;
    uint32_t dropped_count;
} KernelInputEventQueue;

typedef struct KernelInputEventQueueStats {
    uint32_t capacity;
    uint32_t count;
    uint32_t delivered_count;
    uint32_t dropped_count;
} KernelInputEventQueueStats;

#ifdef __cplusplus
extern "C" {
#endif

void input_event_queue_init(KernelInputEventQueue* queue);
uint32_t input_event_queue_capacity(const KernelInputEventQueue* queue);
uint32_t input_event_queue_count(const KernelInputEventQueue* queue);
uint32_t input_event_queue_delivered_count(const KernelInputEventQueue* queue);
uint32_t input_event_queue_dropped_count(const KernelInputEventQueue* queue);
void input_event_queue_get_stats(const KernelInputEventQueue* queue,
                                 KernelInputEventQueueStats* stats);
int input_event_queue_is_empty(const KernelInputEventQueue* queue);
int input_event_queue_is_full(const KernelInputEventQueue* queue);
int input_event_queue_has_key(const KernelInputEventQueue* queue);
int input_event_queue_has_character(const KernelInputEventQueue* queue);
int input_event_queue_push(KernelInputEventQueue* queue, const OsInputEvent* event);
int input_event_queue_push_drop_oldest(KernelInputEventQueue* queue, const OsInputEvent* event);
int input_event_queue_pop(KernelInputEventQueue* queue, OsInputEvent* event);

#ifdef __cplusplus
}
#endif

#endif
