#include "drivers/keyboard.h"
#include "arch/x86/io.h"

#include "shell/shell.h"

extern "C" void shell_recall_history(int direction);
extern "C" void shell_input(char c);
extern "C" int user_input_active64();
extern "C" void keyboard_deliver_char64(char c);

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
    : shift_pressed(0), caps_lock_on(0), is_extended(false) {}

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
    while (1) {
        if ((inb(0x64) & 0x01) == 0) {
            continue;
        }

        uint8_t scan_code = inb(0x60);

        if (scan_code == 0xE0) {
            is_extended = true;
            continue;
        }

        if (is_extended) {
            is_extended = false;
            continue;
        }

        if (scan_code == 0x2A || scan_code == 0x36) {
            shift_pressed = 1;
            continue;
        }

        if (scan_code == 0xAA || scan_code == 0xB6) {
            shift_pressed = 0;
            continue;
        }

        if (scan_code == 0x3A) {
            caps_lock_on = !caps_lock_on;
            continue;
        }

        if (scan_code & 0x80) {
            continue;
        }

        char ascii = get_char(scan_code);
        if (ascii != 0) {
            return ascii;
        }
    }
}

void KeyboardDriver::handle() {
    uint8_t scan_code = inb(0x60);
    bool user_mode_input = user_input_active64() != 0;

    if (scan_code == 0xE0) {
        is_extended = true;
        outb(0x20, 0x20);
        return;
    }

    if (is_extended) {
        is_extended = false;
        if (!user_mode_input && scan_code == 0x48) {
            shell_recall_history(-1);
        }
        else if (!user_mode_input && scan_code == 0x50) {
            shell_recall_history(1);
        }
        outb(0x20, 0x20);
        return;
    }

    if (scan_code == 0x2A || scan_code == 0x36) {
        shift_pressed = 1;
        outb(0x20, 0x20);
        return;
    }
    if (scan_code == 0xAA || scan_code == 0xB6) {
        shift_pressed = 0;
        outb(0x20, 0x20);
        return;
    }
    if (scan_code == 0x3A) {
        caps_lock_on = !caps_lock_on;
        outb(0x20, 0x20);
        return;
    }
    if (!(scan_code & 0x80)) {
        char ascii = get_char(scan_code);
        if (ascii != 0) {
            keyboard_deliver_char64(ascii);
        }
    }
    outb(0x20, 0x20);
}
