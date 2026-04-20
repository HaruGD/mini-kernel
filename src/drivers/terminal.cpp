#include "drivers/terminal.h"
#include "arch/x86/io.h"

Terminal::Terminal() {
    vga = (volatile char*)0xB8000;
    cursor = 0;
    color = 0x0F;

    vga[0] = 'T';
    vga[1] = 0x0F;
}

void Terminal::update_cursor() {
    outb(0x3D4, 0x0F);
    outb(0x3D5, (unsigned char)(cursor & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (unsigned char)((cursor >> 8) & 0xFF));
}

void Terminal::scroll() {
    for (int i = 0; i < 80 * 24; i++) {
        vga[i * 2]     = vga[(i + 80) * 2];
        vga[i * 2 + 1] = vga[(i + 80) * 2 + 1];
    }
    for (int i = 80 * 24; i < 80 * 25; i++) {
        vga[i * 2]     = ' ';
        vga[i * 2 + 1] = color;
    }
    cursor = 80 * 24;
}

void Terminal::clear() {
    for (int i = 0; i < 80 * 25; i++) {
        vga[i * 2]     = ' ';
        vga[i * 2 + 1] = color;
    }
    cursor = 0;
    update_cursor();
}

void Terminal::putchar(char c) {
    if (cursor >= 80 * 25) scroll();

    if (c == '\n') {
        cursor = (cursor / 80 + 1) * 80;
    } else if (c == '\b') {
        if (cursor > 0) {
            cursor--;
            vga[cursor * 2]     = ' ';
            vga[cursor * 2 + 1] = color;
        }
    } else {
        vga[cursor * 2]     = c;
        vga[cursor * 2 + 1] = color;
        cursor++;
    }
    update_cursor();
}

void Terminal::print(const char* str) {
    for (int i = 0; str[i] != '\0'; i++) {
        putchar(str[i]);
    }
}

void Terminal::print_hex(uint32_t n) {
    char hex_chars[] = "0123456789ABCDEF";
    char buffer[11];
    buffer[0] = '0';
    buffer[1] = 'x';
    for (int i = 9; i >= 2; i--) {
        buffer[i] = hex_chars[n & 0xF];
        n >>= 4;
    }
    buffer[10] = '\0';
    print(buffer);
}
