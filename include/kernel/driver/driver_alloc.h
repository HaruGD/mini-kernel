#ifndef KERNEL_DRIVER_DRIVER_ALLOC_H
#define KERNEL_DRIVER_DRIVER_ALLOC_H

#include <stdint.h>

#include "kernel/driver/driver_manager.h"

#define DRIVER_MAX_ALLOCATIONS 128u
#define DRIVER_ALLOCATION_BUDGET_BYTES (1024u * 1024u)
#define DRIVER_ATOMIC_SLOT_COUNT 8u
#define DRIVER_ATOMIC_SLOT_SIZE 256u

#define DRIVER_ALLOC_ZERO   0x01u
#define DRIVER_ALLOC_PAGES  0x02u
#define DRIVER_ALLOC_ATOMIC 0x04u

#define DRIVER_CONTEXT_NONE             0u
#define DRIVER_CONTEXT_THREAD_SLEEPABLE 1u
#define DRIVER_CONTEXT_THREAD_ATOMIC    2u
#define DRIVER_CONTEXT_IRQ              3u
#define DRIVER_CONTEXT_EMERGENCY        4u

struct DriverAllocationHandle {
    uint32_t slot;
    uint32_t generation;
};

struct DriverAllocationResult {
    DriverAllocationHandle handle;
    void* address;
    uint64_t size;
};

struct DriverExecutionContext {
    DriverIdentity owner;
    uint32_t kind;
    uint32_t depth;
};

struct DriverExecutionToken {
    DriverExecutionContext previous;
    uint32_t cpu_slot;
    uint8_t active;
    uint8_t reserved[3];
};

struct DriverAllocationStats {
    uint32_t active;
    uint32_t high_water;
    uint32_t quarantined;
    uint32_t atomic_active;
    uint64_t active_bytes;
    uint64_t peak_bytes;
    uint64_t allocations;
    uint64_t releases;
    uint64_t automatic_releases;
    uint64_t budget_rejections;
    uint64_t context_rejections;
    uint64_t atomic_exhaustion;
    uint64_t stale_rejections;
    uint64_t owner_rejections;
    uint64_t backing_failures;
    uint64_t exhaustion_failures;
};

void driver_allocation_init();
DriverAllocationHandle driver_allocation_invalid();
int driver_allocation_handle_is_valid(DriverAllocationHandle handle);
int driver_execution_enter(DriverIdentity owner, uint32_t kind,
                           DriverExecutionToken* token);
void driver_execution_leave(DriverExecutionToken* token);
int driver_execution_current(DriverExecutionContext* out);
int driver_execution_require_sleepable();
int driver_execution_runtime_allowed();
int driver_allocation_create(DriverIdentity owner, uint32_t context,
                             uint64_t size, uint64_t alignment,
                             uint32_t flags, const char* tag,
                             DriverAllocationResult* out);
int driver_allocation_release(DriverIdentity owner, uint32_t context,
                              DriverAllocationHandle handle);
int driver_allocation_create_current(uint64_t size, uint64_t alignment,
                                     uint32_t flags, const char* tag,
                                     DriverAllocationResult* out);
int driver_allocation_release_current(DriverAllocationHandle handle);
uint32_t driver_allocation_release_owner(DriverIdentity owner);
uint32_t driver_allocation_owner_count(DriverIdentity owner);
void driver_allocation_get_stats(DriverAllocationStats* out);

#endif
