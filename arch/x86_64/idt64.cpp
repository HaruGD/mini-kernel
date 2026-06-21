#include "arch/x86_64/idt64.h"
#include "kernel/klog.h"
#include "kernel/panic.h"
#include "kernel/driver/driver_manager.h"

extern "C" {
    #include "arch/x86_64/io.h"
}

extern "C" void debug_print64(const char* str);
extern "C" void debug_print_hex64(uint32_t value);
extern "C" void debug_print_hex64_u64(uint64_t value);
extern "C" void process_record_fault64(uint32_t reason, uint32_t status_code);
extern "C" uint64_t process_fault_returnable64();
extern "C" uint64_t timer_preempt_requested64();

#define PROCESS_TERM_PAGE_FAULT 6
#define PROCESS_TERM_GP_FAULT 7
#define PROCESS_TERM_DOUBLE_FAULT 8

static struct idt64_entry idt64[256];
static struct idtr64 idtr;
static uint32_t pic_spurious7 = 0;
static uint32_t pic_spurious15 = 0;

extern "C" void isr_default_asm();
extern "C" void isr_page_fault_asm();
extern "C" void isr_gp_fault_asm();
extern "C" void isr_double_fault_asm();
extern "C" void irq_keyboard_asm();
extern "C" void irq_timer_asm();
extern "C" void irq_spurious_asm();
extern "C" void irq_pic_spurious7_asm();
extern "C" void irq_pic_spurious15_asm();
extern "C" void user_test_asm();
extern "C" void user_exit_asm();
extern "C" void syscall_asm();

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
    idt64[8].ist = 1;
    set_idt64_gate(13, (uint64_t)isr_gp_fault_asm);
    set_idt64_gate(14, (uint64_t)isr_page_fault_asm);
    set_idt64_gate(32, (uint64_t)irq_timer_asm);
    set_idt64_gate(33, (uint64_t)irq_keyboard_asm);
    set_idt64_gate(39, (uint64_t)irq_pic_spurious7_asm);
    set_idt64_gate(47, (uint64_t)irq_pic_spurious15_asm);
    set_idt64_gate(255, (uint64_t)irq_spurious_asm);
    set_idt64_gate_dpl(0x81, (uint64_t)user_test_asm, 3);
    set_idt64_gate_dpl(0x82, (uint64_t)user_exit_asm, 3);
    set_idt64_gate_dpl(0x80, (uint64_t)syscall_asm, 3);

    pic_remap();
    idt64_load(&idtr);
}

extern "C" void default_interrupt_handler64(uint64_t* frame) {
    PanicInterruptInfo info = {};
    info.registers = frame;
    if (frame != 0) {
        info.rip = frame[15];
        info.cs = frame[16];
        info.rflags = frame[17];
        if ((info.cs & 3u) != 0) {
            info.rsp = frame[18];
            info.ss = frame[19];
        }
    }
    kernel_panic("unhandled interrupt", &info);
}

static PanicInterruptInfo fault_info(uint64_t* frame,
                                     uint64_t error_code,
                                     uint64_t fault_address) {
    PanicInterruptInfo info = {};
    info.registers = frame;
    info.error_code = error_code;
    info.fault_address = fault_address;
    if (frame != 0) {
        info.rip = frame[16];
        info.cs = frame[17];
        info.rflags = frame[18];
        if ((info.cs & 3u) != 0) {
            info.rsp = frame[19];
            info.ss = frame[20];
        }
    }
    return info;
}

extern "C" uint64_t page_fault_handler64(uint64_t fault_addr,
                                           uint64_t error_code,
                                           uint64_t* frame) {
    PanicInterruptInfo info = fault_info(frame, error_code, fault_addr);
    if ((info.cs & 3u) == 0) {
        kernel_panic("kernel page fault", &info);
    }
    process_record_fault64(PROCESS_TERM_PAGE_FAULT, (uint32_t)error_code);
    debug_print64("\n=== PAGE FAULT ===");
    debug_print64("\nFault addr: ");
    debug_print_hex64_u64(fault_addr);
    debug_print64("\nError code: ");
    debug_print_hex64_u64(error_code);
    return process_fault_returnable64();
}

extern "C" uint64_t gp_fault_handler64(uint64_t error_code, uint64_t* frame) {
    PanicInterruptInfo info = fault_info(frame, error_code, 0);
    if ((info.cs & 3u) == 0) {
        kernel_panic("kernel general protection fault", &info);
    }
    process_record_fault64(PROCESS_TERM_GP_FAULT, (uint32_t)error_code);
    debug_print64("\n=== GENERAL PROTECTION FAULT ===");
    debug_print64("\nError code: ");
    debug_print_hex64_u64(error_code);
    return process_fault_returnable64();
}

extern "C" uint64_t double_fault_handler64(uint64_t error_code, uint64_t* frame) {
    PanicInterruptInfo info = fault_info(frame, error_code, 0);
    process_record_fault64(PROCESS_TERM_DOUBLE_FAULT, (uint32_t)error_code);
    kernel_panic("double fault", &info);
}

extern "C" void spurious_interrupt_handler64() {
    static uint32_t spurious_count = 0;
    spurious_count++;
    if (spurious_count == 1) {
        klog_write(KLOG_WARN, "interrupt", "spurious APIC interrupt");
    }
}

static uint8_t pic_read_isr(uint16_t command_port) {
    outb(command_port, 0x0B);
    uint8_t isr = inb(command_port);
    outb(command_port, 0x0A);
    return isr;
}

extern "C" void pic_spurious_interrupt_handler64(uint64_t irq) {
    if (irq == 7) {
        if (pic_read_isr(0x20) & 0x80u) {
            driver_irq_dispatch(7);
            outb(0x20, 0x20);
            return;
        }
        pic_spurious7++;
        if (pic_spurious7 == 1) {
            klog_write(KLOG_WARN, "interrupt", "spurious PIC IRQ7");
        }
        return;
    }

    if (irq == 15) {
        if (pic_read_isr(0xA0) & 0x80u) {
            driver_irq_dispatch(15);
            outb(0xA0, 0x20);
            outb(0x20, 0x20);
            return;
        }
        pic_spurious15++;
        if (pic_spurious15 == 1) {
            klog_write(KLOG_WARN, "interrupt", "spurious PIC IRQ15");
        }
        outb(0x20, 0x20);
    }
}

uint32_t pic_spurious_irq7_count() {
    return pic_spurious7;
}

uint32_t pic_spurious_irq15_count() {
    return pic_spurious15;
}
