#ifndef KERNEL_PROCESS_SURFACE_H
#define KERNEL_PROCESS_SURFACE_H

#include <stdint.h>

#include "kernel/process.h"

void process_surface_mappings_reset(Process* process);
uint64_t process_surface_map(Process* process, uint64_t handle, uint32_t map_flags);
int process_surface_unmap(Process* process, uint64_t handle, uint64_t user_address);
int process_surface_unmap_object(Process* process, uint64_t object_id);
uint32_t process_surface_unmap_all(Process* process);

#endif
