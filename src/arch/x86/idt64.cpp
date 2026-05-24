#include "arch/x86/idt64.h"

extern "C" {
    #include "arch/x86/io.h"
}

extern "C" void debug_print64(const char* str);
extern "C" void debug_print_hex64(uint32_t value);
extern "C" void debug_print_hex64_u64(uint64_t value);
extern "C" void process_record_fault64(uint32_t reason, uint32_t status_code);
extern "C" uint64_t process_fault_returnable64();

#define PROCESS_TERM_PAGE_FAULT 6
#define PROCESS_TERM_GP_FAULT 7
#define PROCESS_TERM_DOUBLE_FAULT 8

static struct idt64_entry idt64[256];
static struct idtr64 idtr;

extern "C" void isr_default_asm();
extern "C" void isr_page_fault_asm();
extern "C" void isr_gp_fault_asm();
extern "C" void isr_double_fault_asm();
extern "C" void irq_keyboard_asm();
extern "C" void irq_timer_asm();
extern "C" void user_test_asm();
extern "C" void user_exit_asm();
extern "C" void syscall_asm();

static void halt_forever() {
    while (1) {
        __asm__ volatile("cli; hlt");
    }
}

static void set_idt64_gate(int n, uint64_t handler) {
    uint8_t type_attr = 0x8E;
    idt64[n].offset_low = handler & 0xFFFF;
    idt64[n].selector = 0x18;
    idt64[n].ist = 0;
    idt64[n].type_attr = type_attr;
    idt64[n].offset_mid = (handler >> 16) & 0xFFFF;
    idt64[n].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt64[n].zero = 0;
}

static void set_idt64_gate_dpl(int n, uint64_t handler, uint8_t dpl) {
    uint8_t type_attr = 0x8E | ((dpl & 0x3) << 5);
    idt64[n].offset_low = handler & 0xFFFF;
    idt64[n].selector = 0x18;
    idt64[n].ist = 0;
    idt64[n].type_attr = type_attr;
    idt64[n].offset_mid = (handler >> 16) & 0xFFFF;
    idt64[n].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt64[n].zero = 0;
}

static void pic_remap() {
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 0x20);
    outb(0xA1, 0x28);
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    outb(0x21, 0xFC);
    outb(0xA1, 0xFF);
}

extern "C" void idt64_init() {
    idtr.limit = sizeof(idt64) - 1;
    idtr.base = (uint64_t)&idt64;

    for (int i = 0; i < 256; i++) {
        set_idt64_gate(i, (uint64_t)isr_default_asm);
    }

    set_idt64_gate(8, (uint64_t)isr_double_fault_asm);
    set_idt64_gate(13, (uint64_t)isr_gp_fault_asm);
    set_idt64_gate(14, (uint64_t)isr_page_fault_asm);
    set_idt64_gate(32, (uint64_t)irq_timer_asm);
    set_idt64_gate(33, (uint64_t)irq_keyboard_asm);
    set_idt64_gate_dpl(0x81, (uint64_t)user_test_asm, 3);
    set_idt64_gate_dpl(0x82, (uint64_t)user_exit_asm, 3);
    set_idt64_gate_dpl(0x80, (uint64_t)syscall_asm, 3);

    pic_remap();
    idt64_load(&idtr);
}

extern "C" void default_interrupt_handler64() {
    debug_print64("\n=== UNHANDLED 64-BIT INTERRUPT ===");
    halt_forever();
}

extern "C" uint64_t page_fault_handler64(uint64_t fault_addr, uint64_t error_code) {
    process_record_fault64(PROCESS_TERM_PAGE_FAULT, (uint32_t)error_code);
    debug_print64("\n=== PAGE FAULT ===");
    debug_print64("\nFault addr: ");
    debug_print_hex64_u64(fault_addr);
    debug_print64("\nError code: ");
    debug_print_hex64_u64(error_code);
    return process_fault_returnable64();
}

extern "C" uint64_t gp_fault_handler64(uint64_t error_code) {
    process_record_fault64(PROCESS_TERM_GP_FAULT, (uint32_t)error_code);
    debug_print64("\n=== GENERAL PROTECTION FAULT ===");
    debug_print64("\nError code: ");
    debug_print_hex64_u64(error_code);
    return process_fault_returnable64();
}

extern "C" uint64_t double_fault_handler64(uint64_t error_code) {
    process_record_fault64(PROCESS_TERM_DOUBLE_FAULT, (uint32_t)error_code);
    debug_print64("\n=== DOUBLE FAULT ===");
    debug_print64("\nError code: ");
    debug_print_hex64_u64(error_code);
    return process_fault_returnable64();
}
