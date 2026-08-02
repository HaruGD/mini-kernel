#include "drivers/mouse.h"

#include "arch/x86_64/apic.h"
#include "arch/x86_64/io.h"
#include "drivers/pit.h"
#include "kernel/input/input_events.h"

extern PIT pit;

static int wait_input_clear() {
    for (uint32_t i = 0; i < 100000u; i++) {
        if ((inb(0x64) & 0x02u) == 0) return 1;
        __asm__ volatile("pause");
    }
    return 0;
}

static int wait_output_full() {
    for (uint32_t i = 0; i < 100000u; i++) {
        if ((inb(0x64) & 0x01u) != 0) return 1;
        __asm__ volatile("pause");
    }
    return 0;
}

static int controller_command(uint8_t command) {
    if (!wait_input_clear()) return 0;
    outb(0x64, command);
    return 1;
}

static int mouse_command(uint8_t command, uint8_t* response) {
    if (!controller_command(0xD4) || !wait_input_clear()) return 0;
    outb(0x60, command);
    if (!wait_output_full()) return 0;
    uint8_t ack = inb(0x60);
    if (ack != 0xFAu) return 0;
    if (response != 0) {
        if (!wait_output_full()) return 0;
        *response = inb(0x60);
    }
    return 1;
}

static int set_sample_rate(uint8_t rate) {
    return mouse_command(0xF3u, 0) && mouse_command(rate, 0);
}

Ps2MouseDriver::Ps2MouseDriver()
    : packet{}, initialized(0), dropped_bytes(0) {
    ps2_mouse_packet_init(&packet, 0);
}

void Ps2MouseDriver::init() {
    initialized = 0;
    interrupt_controller_set_mask(12, 1);
    if (!controller_command(0xA8u) || !controller_command(0x20u) ||
        !wait_output_full()) return;
    uint8_t config = inb(0x60);
    config |= 0x02u;
    config &= (uint8_t)~0x20u;
    if (!controller_command(0x60u) || !wait_input_clear()) return;
    outb(0x60, config);
    if (!mouse_command(0xF6u, 0)) return;

    uint8_t id = 0;
    int wheel = set_sample_rate(200u) && set_sample_rate(100u) &&
                set_sample_rate(80u) && mouse_command(0xF2u, &id) &&
                (id == 3u || id == 4u);
    ps2_mouse_packet_init(&packet, wheel);
    if (!mouse_command(0xF4u, 0) ||
        !interrupt_controller_route_external_irq(12, 44)) return;
    interrupt_controller_set_mask(2, 0);
    interrupt_controller_set_mask(12, 0);
    initialized = 1;
}

void Ps2MouseDriver::handle() {
    uint8_t status = inb(0x64);
    if ((status & 0x01u) == 0 || (status & 0x20u) == 0) {
        dropped_bytes++;
        return;
    }
    OsInputEvent event;
    if (ps2_mouse_packet_push(&packet, inb(0x60), pit.get_tick64(), &event)) {
        input_events_push(&event);
    }
}

int Ps2MouseDriver::ready() const { return initialized != 0; }
uint32_t Ps2MouseDriver::dropped() const { return dropped_bytes; }
