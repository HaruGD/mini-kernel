#include "arch/x86/gdt64.h"

static uint64_t gdt64_table[8];
static struct gdtr64 gdtr64_desc;
static struct tss64 kernel_tss64;
static uint8_t kernel_rsp0_stack[16384] __attribute__((aligned(16)));

static void set_tss64_descriptor(int index, uint64_t base, uint32_t limit) {
    struct gdt64_system_descriptor* desc = (struct gdt64_system_descriptor*)&gdt64_table[index];
    desc->limit_low = limit & 0xFFFF;
    desc->base_low = base & 0xFFFF;
    desc->base_mid = (base >> 16) & 0xFF;
    desc->access = 0x89;
    desc->granularity = (limit >> 16) & 0x0F;
    desc->base_high = (base >> 24) & 0xFF;
    desc->base_upper = (base >> 32) & 0xFFFFFFFF;
    desc->reserved = 0;
}

extern "C" void gdt64_set_kernel_stack(uint64_t rsp0) {
    kernel_tss64.rsp0 = rsp0;
}

extern "C" uint64_t gdt64_get_kernel_stack() {
    return kernel_tss64.rsp0;
}

extern "C" uint16_t gdt64_get_tss_selector() {
    return GDT64_TSS_SEL;
}

extern "C" void gdt64_init() {
    for (int i = 0; i < 8; i++) {
        gdt64_table[i] = 0;
    }

    gdt64_table[1] = 0x00CF9A000000FFFFULL;
    gdt64_table[2] = 0x00CF92000000FFFFULL;
    gdt64_table[3] = 0x00AF9A000000FFFFULL;
    gdt64_table[4] = 0x00CFF2000000FFFFULL;
    gdt64_table[5] = 0x00AFFA000000FFFFULL;

    for (unsigned int i = 0; i < sizeof(kernel_tss64); i++) {
        ((volatile uint8_t*)&kernel_tss64)[i] = 0;
    }

    kernel_tss64.rsp0 = (uint64_t)(uintptr_t)(kernel_rsp0_stack + sizeof(kernel_rsp0_stack));
    kernel_tss64.io_map_base = sizeof(kernel_tss64);

    set_tss64_descriptor(6, (uint64_t)(uintptr_t)&kernel_tss64, sizeof(kernel_tss64) - 1);

    gdtr64_desc.limit = sizeof(gdt64_table) - 1;
    gdtr64_desc.base = (uint64_t)(uintptr_t)gdt64_table;

    gdt64_load(&gdtr64_desc);
    tss64_load(GDT64_TSS_SEL);
}
