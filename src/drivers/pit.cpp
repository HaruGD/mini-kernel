#include "drivers/pit.h"
#include "drivers/terminal.h"

extern "C" {
    #include "arch/x86/io.h"
}

extern Terminal terminal;

PIT::PIT() : tick(0) {}

void PIT::init() {
    init(100);  // 기본 100Hz
}

void PIT::init(uint32_t frequency) {
    uint32_t divisor = 1193180 / frequency;

    // PIT 설정
    outb(COMMAND, 0x36);  // 채널0, 로우/하이 바이트, 스퀘어웨이브
    outb(CHANNEL0, divisor & 0xFF);         // 하위 바이트
    outb(CHANNEL0, (divisor >> 8) & 0xFF);  // 상위 바이트
}

void PIT::handle() {
    tick++;
    outb(0x20, 0x20);  // PIC EOI
}

uint32_t PIT::get_tick() {
    return tick;
}