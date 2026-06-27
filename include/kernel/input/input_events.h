#ifndef KERNEL_INPUT_EVENTS_H
#define KERNEL_INPUT_EVENTS_H

#include <stdint.h>
#include "kernel/input/input_event_queue.h"
#include "os64/input_types.h"

typedef struct KernelInputStats {
    uint32_t capacity;
    uint32_t count;
    uint32_t delivered_count;
    uint32_t dropped_count;
} KernelInputStats;

#ifdef __cplusplus
extern "C" {
#endif

void input_events_init();
int input_events_push(const OsInputEvent* event);
int input_events_pop(OsInputEvent* event);
void input_events_get_stats(KernelInputStats* stats);

#ifdef __cplusplus
}
#endif

#endif
