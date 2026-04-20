#ifndef IDT_H
#define IDT_H

#include <stdint.h>
#include "io.h"

struct idt_entry{
    uint16_t base_low;
    uint16_t sel;
    uint8_t zero;
    uint8_t flags;
    uint16_t base_high;
} __attribute__((packed));

struct idt_ptr{
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

void set_idt_gate(int n, uint32_t handler);
void set_idt();

extern void idt_load();
extern void keyboard_handler_asm();

#endif