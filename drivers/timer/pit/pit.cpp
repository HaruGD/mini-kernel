#include "drivers/pit.h"
#include "drivers/terminal.h"

extern "C" {
    #include "arch/x86_64/io.h"
}

extern Terminal terminal;

PIT::PIT() : tick(0) {}

void PIT::init() {
    init(PIT_DEFAULT_HZ);
}

void PIT::init(uint32_t frequency) {
    if (frequency == 0) {
        frequency = PIT_DEFAULT_HZ;
    }
    uint32_t divisor = PIT_BASE_FREQUENCY / frequency;

    // PIT 설정
    outb(COMMAND, 0x36);  // 채널0, 로우/하이 바이트, 스퀘어웨이브
    outb(CHANNEL0, divisor & 0xFF);         // 하위 바이트
    outb(CHANNEL0, (divisor >> 8) & 0xFF);  // 상위 바이트
}

void PIT::handle() {
    tick++;
}

uint32_t PIT::get_tick() {
    return (uint32_t)tick;
}

uint64_t PIT::get_tick64() const {
    return tick;
}

uint32_t PIT::get_frequency() const {
    return PIT_DEFAULT_HZ;
}

uint32_t PIT::ticks_to_ms(uint32_t ticks) const {
    return (ticks * 1000u) / PIT_DEFAULT_HZ;
}
