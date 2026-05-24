#ifndef IDT64_H
#define IDT64_H

#include <stdint.h>

struct idt64_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

struct idtr64 {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

#ifdef __cplusplus
extern "C" {
#endif

void idt64_init();
void idt64_load(struct idtr64* idtr);

void default_interrupt_handler64();
void page_fault_handler64(uint64_t fault_addr, uint64_t error_code);
void gp_fault_handler64(uint64_t error_code);
void double_fault_handler64(uint64_t error_code);

void keyboard_handler64();
void timer_handler64();
void user_test_interrupt_handler64();
void user_exit_interrupt_handler64();
uint64_t syscall_dispatch64(uint64_t syscall_no, uint64_t arg1, uint64_t arg2, uint64_t arg3);

#ifdef __cplusplus
}
#endif

#endif
