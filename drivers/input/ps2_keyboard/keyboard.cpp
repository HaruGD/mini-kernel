#include "drivers/keyboard.h"
#include "arch/x86_64/io.h"
#include "drivers/pit.h"
#include "kernel/input/input_events.h"

#include "shell/shell.h"

extern "C" void shell_recall_history(int direction);
extern "C" void shell_input(char c);
extern "C" int user_input_active64();
extern "C" void keyboard_deliver_char64(char c);
extern PIT pit;

const char KeyboardDriver::kbd_US[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
  '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,  'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
  '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  '-', 0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

const char KeyboardDriver::kbd_US_shift[128] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
  '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,  'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,  '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
  '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  '-', 0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

KeyboardDriver::KeyboardDriver()
    : shift_pressed(0), ctrl_pressed(0), alt_pressed(0), caps_lock_on(0), is_extended(false) {}

void KeyboardDriver::init() {
    // 키보드는 IDT에서 이미 초기화됨
}

char KeyboardDriver::get_char(uint8_t scan_code) {
    char base_char = kbd_US[scan_code];
    bool is_alphabet = (base_char >= 'a' && base_char <= 'z') || (base_char >= 'A' && base_char <= 'Z');

    if (is_alphabet) {
        if (shift_pressed) {
            if (caps_lock_on) {
                return kbd_US[scan_code];
            }
            else {
                return kbd_US_shift[scan_code];
            }
        }
        else {
            if (caps_lock_on) {
                return kbd_US_shift[scan_code];
            }
            else {
                return kbd_US[scan_code];
            }
        }
    }
    else {
        if (shift_pressed) {
            return kbd_US_shift[scan_code];
        }
        else {
            return kbd_US[scan_code];
        }
    }
}

char KeyboardDriver::read_char_blocking() {
    char ascii = 0;
    while (1) {
        if (try_read_char(&ascii)) {
            return ascii;
        }
    }
}

bool KeyboardDriver::try_read_char(char* out_char) {
    if (out_char == 0) {
        return false;
    }

    OsKeyEvent event;
    if (!try_read_event(&event) || event.type != KEYBOARD_EVENT_DOWN || event.character == 0) {
        return false;
    }

    *out_char = (char)event.character;
    return true;
}

bool KeyboardDriver::try_read_event(OsKeyEvent* out_event) {
    if (out_event == 0 || (inb(0x64) & 0x01) == 0) {
        return false;
    }

    OsInputEvent input_event;
    if (!process_scan_code(inb(0x60), pit.get_tick64(), &input_event) ||
        input_event.type != OS_INPUT_EVENT_KEY) {
        return false;
    }

    *out_event = input_event.data.key;
    return true;
}

bool KeyboardDriver::process_scan_code(uint8_t raw_code,
                                       uint64_t timestamp_ticks,
                                       OsInputEvent* out_event) {
    if (out_event == 0) {
        return false;
    }
    if (raw_code == 0xE0) {
        is_extended = true;
        return false;
    }

    bool extended = is_extended;
    bool released = (raw_code & 0x80u) != 0;
    uint8_t scan_code = raw_code & 0x7Fu;
    is_extended = false;

    if (!extended && scan_code == 0x2A) {
        if (released) {
            shift_pressed &= ~1;
        } else {
            shift_pressed |= 1;
        }
    } else if (!extended && scan_code == 0x36) {
        if (released) {
            shift_pressed &= ~2;
        } else {
            shift_pressed |= 2;
        }
    } else if (scan_code == 0x1D) {
        ctrl_pressed = released ? 0 : 1;
    } else if (scan_code == 0x38) {
        alt_pressed = released ? 0 : 1;
    } else if (!extended && scan_code == 0x3A && !released) {
        caps_lock_on = !caps_lock_on;
    }

    uint32_t modifiers = 0;
    if (shift_pressed) {
        modifiers |= KEYBOARD_MOD_SHIFT;
    }
    if (ctrl_pressed) {
        modifiers |= KEYBOARD_MOD_CTRL;
    }
    if (alt_pressed) {
        modifiers |= KEYBOARD_MOD_ALT;
    }
    if (caps_lock_on) {
        modifiers |= KEYBOARD_MOD_CAPS_LOCK;
    }

    out_event->type = OS_INPUT_EVENT_KEY;
    out_event->size = sizeof(OsInputEvent);
    out_event->timestamp_ticks = timestamp_ticks;
    out_event->data.key.type = released ? KEYBOARD_EVENT_UP : KEYBOARD_EVENT_DOWN;
    out_event->data.key.keycode = (extended ? 0x100u : 0u) | scan_code;
    out_event->data.key.modifiers = modifiers;
    out_event->data.key.character =
        (!released && !extended) ? (uint32_t)(uint8_t)get_char(scan_code) : 0;
    return true;
}

void KeyboardDriver::handle() {
    OsInputEvent event;
    if (!process_scan_code(inb(0x60), pit.get_tick64(), &event)) {
        return;
    }

    input_events_push(&event);
    bool user_mode_input = user_input_active64() != 0;

    if ((event.data.key.keycode & 0x100u) != 0 &&
        event.data.key.type == KEYBOARD_EVENT_DOWN) {
        uint32_t scan_code = event.data.key.keycode & 0x7Fu;
        if (!user_mode_input && scan_code == 0x48) {
            shell_recall_history(-1);
        }
        else if (!user_mode_input && scan_code == 0x50) {
            shell_recall_history(1);
        }
        return;
    }

    if (event.data.key.type == KEYBOARD_EVENT_DOWN &&
        event.data.key.character != 0) {
        keyboard_deliver_char64((char)event.data.key.character);
    }
}
