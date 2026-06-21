#include "kernel/boot_info.h"
#include "kernel/klog.h"
#include "kernel/ksh64.h"
#include "kernel/kutil64.h"
#include "kernel/panic.h"

static int panic_active = 0;

static const BootReservedRange* kernel_stack_range() {
    const BootInfo* boot_info = kernel_boot_info();
    if (boot_info == 0 || boot_info->size < sizeof(BootInfo)) {
        return 0;
    }
    for (uint32_t i = 0; i < boot_info->reserved_range_count; i++) {
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

static void print_stack_trace(uint64_t rip, uint64_t rbp) {
    print("Stack trace:\n");
    print("  ");
    print_hex64(rip);
    print("\n");

    const BootReservedRange* stack = kernel_stack_range();
    if (stack == 0 || stack->size < 16) {
        print("  (kernel stack range unavailable)\n");
        return;
    }

    uint64_t stack_end = stack->base + stack->size;
    for (uint32_t depth = 1; depth < 16; depth++) {
        if (rbp < stack->base || rbp > stack_end - 16 || (rbp & 7u) != 0) {
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
    if (panic_active) {
        while (1) {
            __asm__ volatile("hlt");
        }
    }
    panic_active = 1;
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
