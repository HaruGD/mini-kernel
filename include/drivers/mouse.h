#ifndef OS64_DRIVERS_MOUSE_H
#define OS64_DRIVERS_MOUSE_H

#include <stdint.h>

#include "os64/input_types.h"

#ifdef __cplusplus
#include "driver.h"
#endif

typedef struct Ps2MousePacketState {
    uint8_t bytes[4];
    uint8_t index;
    uint8_t packet_size;
    uint8_t buttons;
    uint8_t reserved;
} Ps2MousePacketState;

#ifdef __cplusplus
extern "C" {
#endif

void ps2_mouse_packet_init(Ps2MousePacketState* state, int wheel_enabled);
int ps2_mouse_packet_push(Ps2MousePacketState* state,
                          uint8_t byte,
                          uint64_t timestamp_ticks,
                          OsInputEvent* event);

#ifdef __cplusplus
}

class Ps2MouseDriver : public Driver {
    Ps2MousePacketState packet;
    uint32_t initialized;
    uint32_t dropped_bytes;

public:
    Ps2MouseDriver();
    void init() override;
    void handle();
    int ready() const;
    uint32_t dropped() const;
};
#endif

#endif
