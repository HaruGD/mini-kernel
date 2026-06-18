#ifndef TERMINAL_H
#define TERMINAL_H

#include <stdint.h>

struct BootInfo;

#define TERMINAL_MAX_COLUMNS 160
#define TERMINAL_MAX_ROWS 80
#define TERMINAL_MAX_CELLS (TERMINAL_MAX_COLUMNS * TERMINAL_MAX_ROWS)

class Terminal {
    volatile char* vga;
    volatile uint32_t* framebuffer;
    char text_buffer[TERMINAL_MAX_CELLS];
    int cursor;
    int columns;
    int rows;
    int char_width;
    int char_height;
    uint8_t color;
    uint32_t fb_width;
    uint32_t fb_height;
    uint32_t fb_pixels_per_scanline;
    uint32_t fg_color;
    uint32_t bg_color;
    uint8_t use_framebuffer;
    uint8_t active;

    void update_cursor();
    void scroll();
    void clear_text_buffer();
    void scroll_text_buffer();
    void put_text_cell(int cell, char c);
    void vga_scroll();
    void framebuffer_scroll();
    void putpixel(uint32_t x, uint32_t y, uint32_t color_value);
    void draw_framebuffer_char(int cell, char c);
    void clear_framebuffer_cell(int cell);

public:
    Terminal();
    void init_from_boot_info(const BootInfo* boot_info);
    int is_active() const;
    void clear();
    void putchar(char c);
    void print(const char* str);
    void print_hex(uint32_t val);
};

#endif
