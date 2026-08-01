#ifndef ARCH_X86_64_APIC_H
#define ARCH_X86_64_APIC_H

#include <stdint.h>

struct AcpiState;

enum InterruptControllerMode : uint32_t {
    INTERRUPT_CONTROLLER_PIC = 0,
    INTERRUPT_CONTROLLER_APIC = 1,
};

struct LocalApicTimerCalibration {
    uint64_t timer_hz;
    uint64_t sample_tsc_ticks;
    uint32_t reload;
};

#define INTERRUPT_EXTERNAL_IRQ_COUNT 16u
#define INTERRUPT_OWNER_NONE 0xFFFFFFFFu

struct InterruptOwnershipStats {
    uint32_t owner_cpu[INTERRUPT_EXTERNAL_IRQ_COUNT];
    uint64_t accepted[INTERRUPT_EXTERNAL_IRQ_COUNT];
    uint64_t violations;
    uint64_t handoffs;
};

int interrupt_controller_init(const AcpiState* acpi);
void interrupt_controller_eoi(uint8_t irq);
void interrupt_controller_set_mask(uint8_t irq, int masked);
int interrupt_controller_irq_masked(uint8_t irq);
uint32_t interrupt_controller_mode();
const char* interrupt_controller_name();
void interrupt_controller_print();
int interrupt_controller_init_local_cpu();
int interrupt_controller_send_ipi(uint32_t apic_id, uint8_t vector);
int interrupt_controller_send_nmi(uint32_t apic_id);
int interrupt_controller_start_ap(uint32_t apic_id, uint8_t startup_vector);
int interrupt_controller_calibrate_local_timer(
    uint64_t reference_tsc_hz,
    uint32_t target_hz,
    uint8_t vector,
    LocalApicTimerCalibration* calibration);
void interrupt_controller_stop_local_timer();
int interrupt_controller_claim_external_irq(uint8_t irq);
int interrupt_controller_route_external_irq(uint8_t irq, uint8_t vector);
void interrupt_controller_get_ownership_stats(InterruptOwnershipStats* stats);

#endif
