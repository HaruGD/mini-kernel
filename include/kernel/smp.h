#ifndef KERNEL_SMP_H
#define KERNEL_SMP_H

#include <stdint.h>

#define SMP_TRAMPOLINE_ADDRESS 0x00007000u
#define SMP_TRAMPOLINE_VECTOR (SMP_TRAMPOLINE_ADDRESS >> 12)
#define SMP_MAILBOX_ADDRESS 0x00007800u
#define SMP_STARTUP_PING_VECTOR 0xF1u
#define SMP_STARTUP_PING_ROUNDS 3u
#define SMP_LOCAL_TIMER_VECTOR 0xF2u
#define SMP_RESCHEDULE_VECTOR 0xF3u
#define SMP_TLB_SHOOTDOWN_VECTOR 0xF4u

struct AddressSpace;

struct SmpStartupStats {
    uint32_t attempted;
    uint32_t online;
    uint32_t failed;
    uint32_t ping_sent;
    uint32_t ping_acknowledged;
    uint32_t last_failed_logical_id;
};

struct SmpTimeReference {
    uint64_t tsc_hz;
    uint64_t pit_epoch;
    uint32_t source;
    uint8_t invariant_tsc;
    uint8_t reserved[3];
};

struct SmpExecutionStats {
    uint32_t release_generation;
    uint32_t scheduler_cpus;
    uint32_t calibration_failed;
    uint32_t notify_cursor;
    uint64_t runnable_notifications;
    uint64_t reschedule_ipis;
    uint64_t reschedule_coalesced;
};

int smp_start_application_processors();
void smp_startup_ping_handler();
int smp_debug_send_nmi(uint32_t logical_id);
uint64_t smp_debug_reschedule_burst(uint32_t logical_id, uint32_t count);
int smp_release_scheduler_execution();
int smp_scheduler_execution_released();
void smp_notify_runnable(uint32_t affinity_mask);
int smp_request_reschedule(uint32_t logical_id);
int smp_reschedule_handler();
int smp_tlb_shootdown(AddressSpace* space,
                      uint64_t address,
                      uint32_t page_count,
                      int full_flush,
                      uint64_t generation,
                      uint64_t operation_token,
                      uint32_t target_mask,
                      uint32_t* acknowledged_mask);
int smp_kernel_tlb_shootdown(uint64_t address, uint32_t page_count);
void smp_tlb_shootdown_handler();
const SmpStartupStats* smp_startup_stats();
const SmpTimeReference* smp_time_reference();
const SmpExecutionStats* smp_execution_stats();
void smp_print_summary();

#ifdef OS64_HOST_TEST
int smp_host_prepare_start(uint32_t logical_id);
int smp_host_timeout_start(uint32_t logical_id);
#endif

#endif
