#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "driver.h"
#include <stdint.h>

class KeyboardDriver : public Driver {
    static const char kbd_US[128];
    static const char kbd_US_shift[128];
    static const char kbd_US_caps[128];
    int shift_pressed;
    int caps_lock_on = 0;
    bool is_extended;

public:
    KeyboardDriver();
    void init() override;
    void handle();
    char get_char(uint8_t scan_code);
};

#endif