#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "driver.h"
#include <stdint.h>
#include "os64/input_types.h"

enum KeyboardEventType : uint32_t {
    KEYBOARD_EVENT_NONE = OS_KEY_EVENT_NONE,
    KEYBOARD_EVENT_DOWN = OS_KEY_EVENT_DOWN,
    KEYBOARD_EVENT_UP = OS_KEY_EVENT_UP,
};

enum KeyboardModifier : uint32_t {
    KEYBOARD_MOD_SHIFT = OS_KEY_MOD_SHIFT,
    KEYBOARD_MOD_CTRL = OS_KEY_MOD_CTRL,
    KEYBOARD_MOD_ALT = OS_KEY_MOD_ALT,
    KEYBOARD_MOD_CAPS_LOCK = OS_KEY_MOD_CAPS_LOCK,
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
    bool process_scan_code(uint8_t raw_code, uint64_t timestamp_ticks, OsInputEvent* out_event);
    bool try_read_char(char* out_char);
    bool try_read_event(OsKeyEvent* out_event);
    char read_char_blocking();
};

#endif
