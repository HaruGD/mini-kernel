#ifndef GDT64_H
#define GDT64_H

#include <stdint.h>

#define GDT64_KERNEL32_CODE_SEL 0x08
#define GDT64_KERNEL_DATA_SEL   0x10
#define GDT64_KERNEL64_CODE_SEL 0x18
#define GDT64_USER_DATA_SEL     0x20
#define GDT64_USER_CODE_SEL     0x28
#define GDT64_TSS_SEL           0x30

struct gdt64_system_descriptor {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
    uint32_t base_upper;
    uint32_t reserved;
} __attribute__((packed));

struct gdtr64 {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct tss64 {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t io_map_base;
} __attribute__((packed));

#ifdef __cplusplus
extern "C" {
#endif

void gdt64_init();
void gdt64_set_kernel_stack(uint64_t rsp0);
uint64_t gdt64_get_kernel_stack();
uint16_t gdt64_get_tss_selector();

void gdt64_load(struct gdtr64* gdtr);
void tss64_load(uint16_t selector);

#ifdef __cplusplus
}
#endif

#endif
