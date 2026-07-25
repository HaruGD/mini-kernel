#ifndef ARCH_X86_64_APIC_H
#define ARCH_X86_64_APIC_H

#include <stdint.h>

struct AcpiState;

enum InterruptControllerMode : uint32_t {
    INTERRUPT_CONTROLLER_PIC = 0,
    INTERRUPT_CONTROLLER_APIC = 1,
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

#endif
