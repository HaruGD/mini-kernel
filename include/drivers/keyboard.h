#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "driver.h"
#include <stdint.h>

enum KeyboardEventType : uint32_t {
    KEYBOARD_EVENT_NONE = 0,
    KEYBOARD_EVENT_DOWN = 1,
    KEYBOARD_EVENT_UP = 2,
};

enum KeyboardModifier : uint32_t {
    KEYBOARD_MOD_SHIFT = 1u << 0,
    KEYBOARD_MOD_CTRL = 1u << 1,
    KEYBOARD_MOD_ALT = 1u << 2,
    KEYBOARD_MOD_CAPS_LOCK = 1u << 3,
};

struct KeyboardEvent {
    uint32_t type;
    uint32_t keycode;
    uint32_t modifiers;
    uint32_t character;
};

class KeyboardDriver : public Driver {
    static const char kbd_US[128];
    static const char kbd_US_shift[128];
    static const char kbd_US_caps[128];
    int shift_pressed;
    int ctrl_pressed;
    int alt_pressed;
    int caps_lock_on = 0;
    bool is_extended = false;

public:
    KeyboardDriver();
    void init() override;
    void handle();
    char get_char(uint8_t scan_code);
    bool try_read_char(char* out_char);
    bool try_read_event(KeyboardEvent* out_event);
    char read_char_blocking();
};

#endif
