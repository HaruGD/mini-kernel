#ifndef OS64_TOOLS_KERNEL_MM_HOST_STUBS_H
#define OS64_TOOLS_KERNEL_MM_HOST_STUBS_H

#include <stdint.h>

void host_mm_reset();
uint32_t host_mm_allocated_pages();
uint32_t host_mm_mapped_pages();
void host_mm_fail_map_after(int32_t successes_before_failure);
void host_mm_fail_unmap_after(int32_t successes_before_failure);

#endif
