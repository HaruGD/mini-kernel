#include "drivers/keyboard.h"
#include "io.h"

#include "shell.h"

extern "C" void shell_recall_history(int direction);
extern "C" void shell_input(char c);

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

KeyboardDriver::KeyboardDriver() : shift_pressed(0) {}

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

void KeyboardDriver::handle() {
    uint8_t scan_code = inb(0x60);

    if (scan_code == 0xE0) {
        is_extended = true;
        outb(0x20, 0x20);
        return;
    }

    if (is_extended) {
        is_extended = false;
        if (scan_code == 0x48) {
            shell_recall_history(-1);
        }
        else if (scan_code == 0x50) {
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
            // 셸로 전달
            //extern void shell_input(char c);
            shell_input(ascii);
        }
    }
    outb(0x20, 0x20);
}