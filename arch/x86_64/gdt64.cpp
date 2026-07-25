#include "arch/x86_64/gdt64.h"
#include "kernel/cpu.h"
#include "kernel/cpu_local.h"

struct Gdt64CpuState {
    uint64_t table[8];
    struct gdtr64 gdtr;
    struct tss64 tss;
};

static Gdt64CpuState cpu_gdt[CPU_MAX_COUNT];

static Gdt64CpuState* current_state() {
    CpuLocal* local = cpu_local_current();
    if (!cpu_local_validate(local)) return 0;
    return &cpu_gdt[local->logical_id];
}

static void set_tss64_descriptor(uint64_t* table,
                                 int index,
                                 uint64_t base,
                                 uint32_t limit) {
    struct gdt64_system_descriptor* desc =
        (struct gdt64_system_descriptor*)&table[index];
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
    Gdt64CpuState* state = current_state();
    if (state != 0) state->tss.rsp0 = rsp0;
}

extern "C" uint64_t gdt64_get_kernel_stack() {
    Gdt64CpuState* state = current_state();
    return state != 0 ? state->tss.rsp0 : 0;
}

extern "C" uint16_t gdt64_get_tss_selector() {
    return GDT64_TSS_SEL;
}

extern "C" void gdt64_init() {
    CpuLocal* local = cpu_local_current();
    if (!cpu_local_validate(local)) return;
    Gdt64CpuState* state = &cpu_gdt[local->logical_id];
    for (int i = 0; i < 8; i++) {
        state->table[i] = 0;
    }

    state->table[1] = 0x00CF9A000000FFFFULL;
    state->table[2] = 0x00CF92000000FFFFULL;
    state->table[3] = 0x00AF9A000000FFFFULL;
    state->table[4] = 0x00CFF2000000FFFFULL;
    state->table[5] = 0x00AFFA000000FFFFULL;

    for (unsigned int i = 0; i < sizeof(state->tss); i++) {
        ((volatile uint8_t*)&state->tss)[i] = 0;
    }

    state->tss.rsp0 = local->kernel_stack_top;
    state->tss.ist1 = local->double_fault_stack_top;
    state->tss.ist2 = local->nmi_stack_top;
    state->tss.io_map_base = sizeof(state->tss);

    set_tss64_descriptor(state->table,
                         6,
                         (uint64_t)(uintptr_t)&state->tss,
                         sizeof(state->tss) - 1);

    state->gdtr.limit = sizeof(state->table) - 1;
    state->gdtr.base = (uint64_t)(uintptr_t)state->table;

    gdt64_load(&state->gdtr);
    tss64_load(GDT64_TSS_SEL);
}
