#ifndef TERMINAL_H
#define TERMINAL_H

#include <stdint.h>

class Terminal {
    volatile char* vga;
    int cursor;
    uint8_t color;
    
    void update_cursor();
    void scroll();
    
public:
    Terminal();
    void clear();
    void putchar(char c);
    void print(const char* str);
    void print_hex(uint32_t val);
};

#endif