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

#endif
