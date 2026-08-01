#include "kernel/boot_info.h"
#include "kernel/cpu_local.h"
#include "kernel/klog.h"
#include "kernel/ksh64.h"
#include "kernel/kutil64.h"
#include "kernel/panic.h"
#include "kernel/thread.h"

static volatile uint32_t panic_active = 0;

static const BootReservedRange* kernel_stack_range() {
    const BootInfo* boot_info = kernel_boot_info();
    if (boot_info == 0 || boot_info->size < sizeof(BootInfo)) {
        return 0;
    }
    uint32_t count = boot_info->reserved_range_count;
    if (count > BOOT_RESERVED_RANGE_MAX) count = BOOT_RESERVED_RANGE_MAX;
    for (uint32_t i = 0; i < count; i++) {
        if (boot_info->reserved_ranges[i].type == BOOT_RESERVED_RANGE_KERNEL_STACK) {
            return &boot_info->reserved_ranges[i];
        }
    }
    return 0;
}

static void print_register(const char* name, uint64_t value) {
    print(name);
    print("=");
    print_hex64(value);
    print(" ");
}

static void print_registers(const PanicInterruptInfo* info) {
    if (info == 0 || info->registers == 0) {
        return;
    }

    const uint64_t* r = info->registers;
    print("\nRegisters:\n");
    print_register("RAX", r[14]);
    print_register("RBX", r[13]);
    print_register("RCX", r[12]);
    print_register("RDX", r[11]);
    print("\n");
    print_register("RSI", r[9]);
    print_register("RDI", r[8]);
    print_register("RBP", r[10]);
    print_register("R8", r[7]);
    print("\n");
    print_register("R9", r[6]);
    print_register("R10", r[5]);
    print_register("R11", r[4]);
    print_register("R12", r[3]);
    print("\n");
    print_register("R13", r[2]);
    print_register("R14", r[1]);
    print_register("R15", r[0]);
    print("\n");
    print_register("RIP", info->rip);
    print_register("CS", info->cs);
    print_register("RFLAGS", info->rflags);
    print("\n");
    print_register("RSP", info->rsp);
    print_register("SS", info->ss);
    print_register("ERROR", info->error_code);
    print("\n");
    print_register("CR2", info->fault_address);
    print("\n");
}

static int stack_contains(uint64_t base, uint64_t end, uint64_t address) {
    return end > base && address >= base && address <= end - 16u;
}

static int current_stack_bounds(uint64_t rbp, uint64_t* base,
                                uint64_t* end) {
    CpuLocal* local = cpu_local_current();
    if (cpu_local_validate(local)) {
        if (stack_contains(local->nmi_stack_base, local->nmi_stack_top, rbp)) {
            *base = local->nmi_stack_base;
            *end = local->nmi_stack_top;
            return 1;
        }
        if (stack_contains(local->double_fault_stack_base,
                           local->double_fault_stack_top, rbp)) {
            *base = local->double_fault_stack_base;
            *end = local->double_fault_stack_top;
            return 1;
        }
        Thread* thread = local->current_thread;
        if (thread != 0 && thread->context != 0 &&
            thread->context->kernel_stack_page_count != 0) {
            uint64_t thread_base = thread->context->kernel_stack_base;
            uint64_t thread_end = thread_base +
                (uint64_t)thread->context->kernel_stack_page_count * 4096u;
            if (stack_contains(thread_base, thread_end, rbp)) {
                *base = thread_base;
                *end = thread_end;
                return 1;
            }
        }
        if (stack_contains(local->kernel_stack_base, local->kernel_stack_top,
                           rbp)) {
            *base = local->kernel_stack_base;
            *end = local->kernel_stack_top;
            return 1;
        }
    }
    const BootReservedRange* fallback = kernel_stack_range();
    if (fallback != 0 && fallback->size >= 16u &&
        fallback->base + fallback->size >= fallback->base &&
        stack_contains(fallback->base, fallback->base + fallback->size, rbp)) {
        *base = fallback->base;
        *end = fallback->base + fallback->size;
        return 1;
    }
    return 0;
}

static void print_stack_trace(uint64_t rip, uint64_t rbp) {
    print("Stack trace:\n");
    print("  ");
    print_hex64(rip);
    print("\n");

    uint64_t stack_base = 0;
    uint64_t stack_end = 0;
    if (!current_stack_bounds(rbp, &stack_base, &stack_end)) {
        print("  (kernel stack range unavailable)\n");
        return;
    }

    for (uint32_t depth = 1; depth < 16; depth++) {
        if (rbp < stack_base || rbp > stack_end - 16 || (rbp & 7u) != 0) {
            break;
        }
        const uint64_t* frame = (const uint64_t*)(uintptr_t)rbp;
        uint64_t next_rbp = frame[0];
        uint64_t return_address = frame[1];
        if (return_address == 0) {
            break;
        }
        print("  ");
        print_hex64(return_address);
        print("\n");
        if (next_rbp <= rbp) {
            break;
        }
        rbp = next_rbp;
    }
}

[[noreturn]] void kernel_panic(const char* reason, const PanicInterruptInfo* info) {
    __asm__ volatile("cli");
    if (__atomic_exchange_n(&panic_active, 1u, __ATOMIC_ACQ_REL) != 0) {
        while (1) {
            __asm__ volatile("hlt");
        }
    }
    klog_set_capture_enabled(0);

    print("\n\n========================================\n");
    print("OS64 KERNEL PANIC\n");
    print("Reason: ");
    print(reason != 0 ? reason : "unknown");
    print("\n========================================\n");
    print_registers(info);
    if (info != 0 && info->registers != 0) {
        print_stack_trace(info->rip, info->registers[10]);
    }
    klog_write(KLOG_FATAL, "panic", reason != 0 ? reason : "unknown");
    print("Recent kernel log (last 4 KiB):\n");
    klog_dump_tail(4096);
    print("\nEnd recent kernel log.\n");
    print("System halted. Inspect serial output and klog.\n");

    while (1) {
        __asm__ volatile("hlt");
    }
}

[[noreturn]] void kernel_panic_message(const char* reason) {
    uint64_t registers[15] = {};
    registers[10] = (uint64_t)(uintptr_t)__builtin_frame_address(0);
    PanicInterruptInfo info = {};
    info.registers = registers;
    info.rip = (uint64_t)(uintptr_t)__builtin_return_address(0);
    __asm__ volatile("mov %%rsp, %0" : "=r"(info.rsp));
    kernel_panic(reason, &info);
}
