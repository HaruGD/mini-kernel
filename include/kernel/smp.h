#ifndef KERNEL_SMP_H
#define KERNEL_SMP_H

#include <stdint.h>

#define SMP_TRAMPOLINE_ADDRESS 0x00007000u
#define SMP_TRAMPOLINE_VECTOR (SMP_TRAMPOLINE_ADDRESS >> 12)
#define SMP_MAILBOX_ADDRESS 0x00007800u
#define SMP_STARTUP_PING_VECTOR 0xF1u
#define SMP_STARTUP_PING_ROUNDS 3u

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

int smp_start_application_processors();
void smp_startup_ping_handler();
int smp_debug_send_nmi(uint32_t logical_id);
const SmpStartupStats* smp_startup_stats();
const SmpTimeReference* smp_time_reference();
void smp_print_summary();

#ifdef OS64_HOST_TEST
int smp_host_prepare_start(uint32_t logical_id);
int smp_host_timeout_start(uint32_t logical_id);
#endif

#endif
