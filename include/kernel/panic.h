#ifndef KERNEL_PANIC_H
#define KERNEL_PANIC_H

#include <stdint.h>

struct PanicInterruptInfo {
    const uint64_t* registers;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
    uint64_t error_code;
    uint64_t fault_address;
};

[[noreturn]] void kernel_panic(const char* reason, const PanicInterruptInfo* info);
[[noreturn]] void kernel_panic_message(const char* reason);

#endif
