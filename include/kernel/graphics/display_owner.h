#ifndef KERNEL_GRAPHICS_DISPLAY_OWNER_H
#define KERNEL_GRAPHICS_DISPLAY_OWNER_H

#include <stdint.h>

#define DISPLAY_OWNER_NONE 0u
#define DISPLAY_OWNER_TERMINAL 1u
#define DISPLAY_OWNER_GOP 2u

typedef struct DisplayOwnerToken {
    uint64_t flags;
    uint32_t owner;
    uint32_t acquired;
} DisplayOwnerToken;

typedef struct DisplayOwnerStats {
    uint32_t current_owner;
    uint32_t depth;
    uint64_t acquire_count;
    uint64_t busy_count;
} DisplayOwnerStats;

#ifdef __cplusplus
extern "C" {
#endif

void display_owner_begin(uint32_t owner, DisplayOwnerToken* token);
void display_owner_end(DisplayOwnerToken* token);
uint32_t display_owner_current();
void display_owner_get_stats(DisplayOwnerStats* out_stats);

#ifdef __cplusplus
}
#endif

#endif
