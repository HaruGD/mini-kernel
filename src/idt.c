#include "idt.h"
#include "io.h"

void debug_print(const char* str);
void debug_print_hex(uint32_t val);

// 여기서 실제로 메모리에 표와 포인터를 "생성"
struct idt_entry idt[256];
struct idt_ptr idtp;

extern void idt_load();
extern void keyboard_handler_asm();
extern void page_fault_asm();
extern void timer_handler_asm();
extern void default_interrupt_handler_asm();

void set_idt_gate(int n, uint32_t handler) {
    idt[n].base_low = handler & 0xFFFF;
    idt[n].sel = 0x08;
    idt[n].zero = 0;
    idt[n].flags = 0x8E;
    idt[n].base_high = (handler >> 16) & 0xFFFF;
}

// set_idt에서 쓰기 전에 먼저 정의되어 있어야 에러가 안 납니다!
void pic_remap() {
    outb(0x20, 0x11); outb(0xA0, 0x11);
    outb(0x21, 0x20); outb(0xA1, 0x28);
    outb(0x21, 0x04); outb(0xA1, 0x02);
    outb(0x21, 0x01); outb(0xA1, 0x01);
    outb(0x21, 0xFC); 
    outb(0xA1, 0xFF); // 두 번째 칩셋(마우스 등)도 일단 다 막아둡니다.
}

void set_idt() {
    // 이제 여기서 idtp와 idt를 정상적으로 찾을 수 있습니다.
    idtp.limit = (sizeof(struct idt_entry) * 256) - 1;
    idtp.base = (uint32_t)&idt;

    for (int i = 0; i < 256; i++) {
        set_idt_gate(i, (uint32_t)default_interrupt_handler_asm);
    }

    set_idt_gate(14, (uint32_t)page_fault_asm);  // 14번 = 페이지 폴트
    set_idt_gate(33, (uint32_t)keyboard_handler_asm);
    set_idt_gate(32, (uint32_t)timer_handler_asm);

    pic_remap();
    idt_load();
}

void default_interrupt_handler() {
    debug_print("\n=== UNHANDLED INTERRUPT ===");
    while (1) {
        __asm__ volatile("cli; hlt");
    }
}

void page_fault_handler(uint32_t fault_addr, uint32_t error_code) {
    debug_print("\n=== PAGE FAULT ===");
    debug_print("\nFault addr: ");
    debug_print_hex(fault_addr);
    debug_print("\nError code: ");
    debug_print_hex(error_code);
    // error_code 비트:
    // bit0: 0=not present, 1=protection
    // bit1: 0=read, 1=write
    // bit2: 0=kernel, 1=user
    
    while (1) {
        __asm__ volatile("cli; hlt");
    }
}
