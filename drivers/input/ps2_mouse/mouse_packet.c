#include "drivers/mouse.h"

#include <stddef.h>

void ps2_mouse_packet_init(Ps2MousePacketState* state, int wheel_enabled) {
    if (state == NULL) return;
    state->bytes[0] = state->bytes[1] = state->bytes[2] = state->bytes[3] = 0;
    state->index = 0;
    state->packet_size = wheel_enabled ? 4u : 3u;
    state->buttons = 0;
    state->reserved = 0;
}

static int32_t signed_byte(uint8_t byte) {
    return (byte & 0x80u) != 0 ? (int32_t)byte - 256 : (int32_t)byte;
}

int ps2_mouse_packet_push(Ps2MousePacketState* state,
                          uint8_t byte,
                          uint64_t timestamp_ticks,
                          OsInputEvent* event) {
    if (state == NULL || event == NULL ||
        (state->packet_size != 3u && state->packet_size != 4u)) return 0;
    if (state->index == 0 && (byte & 0x08u) == 0) return 0;
    state->bytes[state->index++] = byte;
    if (state->index != state->packet_size) return 0;
    state->index = 0;

    uint8_t status = state->bytes[0];
    if ((status & 0xC0u) != 0) return 0;
    int32_t dx = signed_byte(state->bytes[1]);
    int32_t dy = -signed_byte(state->bytes[2]);
    int32_t wheel = 0;
    if (state->packet_size == 4u) {
        uint8_t nibble = state->bytes[3] & 0x0Fu;
        wheel = (nibble & 0x08u) != 0 ? (int32_t)nibble - 16 : nibble;
    }
    uint32_t buttons = 0;
    if ((status & 0x01u) != 0) buttons |= OS_POINTER_BUTTON_LEFT;
    if ((status & 0x02u) != 0) buttons |= OS_POINTER_BUTTON_RIGHT;
    if ((status & 0x04u) != 0) buttons |= OS_POINTER_BUTTON_MIDDLE;
    if (state->packet_size == 4u) {
        if ((state->bytes[3] & 0x10u) != 0) buttons |= OS_POINTER_BUTTON_X1;
        if ((state->bytes[3] & 0x20u) != 0) buttons |= OS_POINTER_BUTTON_X2;
    }
    uint32_t changed = buttons ^ state->buttons;

    event->type = OS_INPUT_EVENT_POINTER;
    event->size = sizeof(*event);
    event->timestamp_ticks = timestamp_ticks;
    event->data.pointer.x = OS_POINTER_POSITION_UNKNOWN;
    event->data.pointer.y = OS_POINTER_POSITION_UNKNOWN;
    event->data.pointer.delta_x = dx;
    event->data.pointer.delta_y = dy;
    event->data.pointer.wheel_delta = wheel;
    event->data.pointer.buttons = buttons;
    event->data.pointer.changed_buttons = changed;
    if (changed != 0) {
        event->data.pointer.type = (buttons & changed) != 0
            ? OS_POINTER_EVENT_BUTTON_DOWN : OS_POINTER_EVENT_BUTTON_UP;
    } else if (wheel != 0) {
        event->data.pointer.type = OS_POINTER_EVENT_WHEEL;
    } else {
        event->data.pointer.type = OS_POINTER_EVENT_MOVE;
    }
    state->buttons = (uint8_t)buttons;
    return changed != 0 || wheel != 0 || dx != 0 || dy != 0;
}
