#include "drivers/terminal.h"
#include "arch/x86/io.h"
#include "kernel/boot_info.h"

#define FB_FORMAT_RGB 0
#define FB_FORMAT_BGR 1
#define FONT_ROWS 7
#define FONT_SCALE 2

static const uint8_t FONT_DIGITS[10][FONT_ROWS] = {
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},
    {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E},
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},
    {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E},
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E},
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C},
};

static const uint8_t FONT_LETTERS[26][FONT_ROWS] = {
    {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
    {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E},
    {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E},
    {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E},
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F},
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10},
    {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F},
    {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
    {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E},
    {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C},
    {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11},
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F},
    {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11},
    {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11},
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
    {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10},
    {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D},
    {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11},
    {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E},
    {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04},
    {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11},
    {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11},
    {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04},
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F},
};

static const uint8_t FONT_LOWER[26][FONT_ROWS] = {
    {0x00, 0x00, 0x0E, 0x01, 0x0F, 0x11, 0x0F},
    {0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x1E},
    {0x00, 0x00, 0x0E, 0x10, 0x10, 0x11, 0x0E},
    {0x01, 0x01, 0x0D, 0x13, 0x11, 0x11, 0x0F},
    {0x00, 0x00, 0x0E, 0x11, 0x1F, 0x10, 0x0E},
    {0x06, 0x08, 0x08, 0x1E, 0x08, 0x08, 0x08},
    {0x00, 0x00, 0x0F, 0x11, 0x0F, 0x01, 0x0E},
    {0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x11},
    {0x04, 0x00, 0x0C, 0x04, 0x04, 0x04, 0x0E},
    {0x02, 0x00, 0x06, 0x02, 0x02, 0x12, 0x0C},
    {0x10, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12},
    {0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E},
    {0x00, 0x00, 0x1A, 0x15, 0x15, 0x15, 0x15},
    {0x00, 0x00, 0x16, 0x19, 0x11, 0x11, 0x11},
    {0x00, 0x00, 0x0E, 0x11, 0x11, 0x11, 0x0E},
    {0x00, 0x00, 0x1E, 0x11, 0x1E, 0x10, 0x10},
    {0x00, 0x00, 0x0D, 0x13, 0x0F, 0x01, 0x01},
    {0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10},
    {0x00, 0x00, 0x0F, 0x10, 0x0E, 0x01, 0x1E},
    {0x08, 0x08, 0x1E, 0x08, 0x08, 0x09, 0x06},
    {0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0D},
    {0x00, 0x00, 0x11, 0x11, 0x11, 0x0A, 0x04},
    {0x00, 0x00, 0x11, 0x15, 0x15, 0x15, 0x0A},
    {0x00, 0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11},
    {0x00, 0x00, 0x11, 0x11, 0x0F, 0x01, 0x0E},
    {0x00, 0x00, 0x1F, 0x02, 0x04, 0x08, 0x1F},
};

static const uint8_t FONT_QUESTION[FONT_ROWS] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04};

static uint8_t glyph_row(char c, int row) {
    if (c >= 'a' && c <= 'z') {
        return FONT_LOWER[c - 'a'][row];
    }
    if (c >= 'A' && c <= 'Z') {
        return FONT_LETTERS[c - 'A'][row];
    }
    if (c >= '0' && c <= '9') {
        return FONT_DIGITS[c - '0'][row];
    }

    switch (c) {
        case ' ': return 0x00;
        case '.': return row == 6 ? 0x04 : 0x00;
        case ',': return row >= 5 ? (row == 5 ? 0x04 : 0x08) : 0x00;
        case ':': return (row == 2 || row == 5) ? 0x04 : 0x00;
        case ';': return row == 2 ? 0x04 : (row >= 5 ? (row == 5 ? 0x04 : 0x08) : 0x00);
        case '-': return row == 3 ? 0x1F : 0x00;
        case '_': return row == 6 ? 0x1F : 0x00;
        case '+': return row == 3 ? 0x1F : ((row == 1 || row == 2 || row == 4 || row == 5) ? 0x04 : 0x00);
        case '=': return (row == 2 || row == 4) ? 0x1F : 0x00;
        case '\\': return (row >= 1 && row <= 5) ? (0x01 << (5 - row)) : 0x00;
        case '/': return (row >= 1 && row <= 5) ? (0x01 << (row - 1)) : 0x00;
        case '[': return (row == 0 || row == 6) ? 0x0E : 0x08;
        case ']': return (row == 0 || row == 6) ? 0x0E : 0x02;
        case '(': return (row == 0 || row == 6) ? 0x02 : ((row == 1 || row == 5) ? 0x04 : 0x08);
        case ')': return (row == 0 || row == 6) ? 0x08 : ((row == 1 || row == 5) ? 0x04 : 0x02);
        case '>': return row < 3 ? (0x01 << (3 - row)) : (0x01 << (row - 3));
        case '<': return row < 3 ? (0x01 << (row + 1)) : (0x01 << (7 - row));
        case '?': return FONT_QUESTION[row];
        case '!': return row < 5 ? 0x04 : (row == 6 ? 0x04 : 0x00);
        case '*': return row == 1 ? 0x15 : (row == 2 ? 0x0E : (row == 3 ? 0x1F : 0x00));
        case '\'': return row < 2 ? 0x04 : 0x00;
        case '"': return row < 2 ? 0x0A : 0x00;
        case '|': return 0x04;
        default: return FONT_QUESTION[row];
    }
}

Terminal::Terminal() {
    vga = (volatile char*)0xB8000;
    framebuffer = 0;
    cursor = 0;
    columns = 80;
    rows = 25;
    char_width = 1;
    char_height = 1;
    color = 0x0F;
    fb_width = 0;
    fb_height = 0;
    fb_pixels_per_scanline = 0;
    fg_color = 0x00FFFFFF;
    bg_color = 0x00000000;
    use_framebuffer = 0;
    active = 0;
    clear_text_buffer();
}

void Terminal::init_from_boot_info(const BootInfo* boot_info) {
    if (boot_info == 0 ||
        boot_info->size < sizeof(BootInfo) ||
        !(boot_info->flags & BOOT_INFO_FLAG_FRAMEBUFFER) ||
        boot_info->framebuffer_addr == 0 ||
        boot_info->framebuffer_width == 0 ||
        boot_info->framebuffer_height == 0 ||
        boot_info->framebuffer_pixels_per_scanline == 0) {
        return;
    }

    framebuffer = (volatile uint32_t*)(uintptr_t)boot_info->framebuffer_addr;
    fb_width = boot_info->framebuffer_width;
    fb_height = boot_info->framebuffer_height;
    fb_pixels_per_scanline = boot_info->framebuffer_pixels_per_scanline;
    char_width = 6 * FONT_SCALE;
    char_height = 8 * FONT_SCALE;
    columns = (int)(fb_width / (uint32_t)char_width);
    rows = (int)(fb_height / (uint32_t)char_height);
    if (columns > TERMINAL_MAX_COLUMNS) {
        columns = TERMINAL_MAX_COLUMNS;
    }
    if (rows > TERMINAL_MAX_ROWS) {
        rows = TERMINAL_MAX_ROWS;
    }
    if (columns <= 0 || rows <= 0) {
        framebuffer = 0;
        columns = 80;
        rows = 25;
        char_width = 1;
        char_height = 1;
        return;
    }

    if (boot_info->framebuffer_format == FB_FORMAT_BGR) {
        fg_color = 0x00FFFFFF;
        bg_color = 0x00000000;
    } else {
        fg_color = 0x00FFFFFF;
        bg_color = 0x00000000;
    }
    use_framebuffer = 1;
    active = 1;
    cursor = 0;
    clear_text_buffer();
}

int Terminal::is_active() const {
    return active;
}

void Terminal::update_cursor() {
    if (use_framebuffer) {
        return;
    }
    outb(0x3D4, 0x0F);
    outb(0x3D5, (unsigned char)(cursor & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (unsigned char)((cursor >> 8) & 0xFF));
}

void Terminal::putpixel(uint32_t x, uint32_t y, uint32_t color_value) {
    if (framebuffer == 0 || x >= fb_width || y >= fb_height) {
        return;
    }
    framebuffer[(uint64_t)y * fb_pixels_per_scanline + x] = color_value;
}

void Terminal::clear_text_buffer() {
    for (int i = 0; i < TERMINAL_MAX_CELLS; i++) {
        text_buffer[i] = ' ';
    }
}

void Terminal::scroll_text_buffer() {
    if (columns <= 0 || rows <= 0) {
        return;
    }

    for (int y = 1; y < rows; y++) {
        for (int x = 0; x < columns; x++) {
            text_buffer[(y - 1) * columns + x] = text_buffer[y * columns + x];
        }
    }

    int last_row = rows - 1;
    for (int x = 0; x < columns; x++) {
        text_buffer[last_row * columns + x] = ' ';
    }
}

void Terminal::put_text_cell(int cell, char c) {
    if (cell < 0 || cell >= TERMINAL_MAX_CELLS || cell >= columns * rows) {
        return;
    }
    text_buffer[cell] = c;
}

void Terminal::clear_framebuffer_cell(int cell) {
    int cell_x = cell % columns;
    int cell_y = cell / columns;
    uint32_t x0 = (uint32_t)(cell_x * char_width);
    uint32_t y0 = (uint32_t)(cell_y * char_height);

    for (int y = 0; y < char_height; y++) {
        for (int x = 0; x < char_width; x++) {
            putpixel(x0 + (uint32_t)x, y0 + (uint32_t)y, bg_color);
        }
    }
}

void Terminal::draw_framebuffer_char(int cell, char c) {
    int cell_x = cell % columns;
    int cell_y = cell / columns;
    uint32_t x0 = (uint32_t)(cell_x * char_width);
    uint32_t y0 = (uint32_t)(cell_y * char_height);

    clear_framebuffer_cell(cell);
    for (int row = 0; row < FONT_ROWS; row++) {
        uint8_t bits = glyph_row(c, row);
        for (int col = 0; col < 5; col++) {
            uint32_t color_value = (bits & (1 << (4 - col))) ? fg_color : bg_color;
            for (int sy = 0; sy < FONT_SCALE; sy++) {
                for (int sx = 0; sx < FONT_SCALE; sx++) {
                    putpixel(x0 + (uint32_t)(col * FONT_SCALE + sx),
                             y0 + (uint32_t)(row * FONT_SCALE + sy),
                             color_value);
                }
            }
        }
    }
}

void Terminal::vga_scroll() {
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

void Terminal::framebuffer_scroll() {
    uint32_t pixel_rows = (uint32_t)(rows * char_height);
    uint32_t scroll_rows = (uint32_t)char_height;
    uint32_t copy_width = (uint32_t)(columns * char_width);
    uint32_t* fb = (uint32_t*)(uintptr_t)framebuffer;

    if (copy_width > fb_width) {
        copy_width = fb_width;
    }

    for (uint32_t y = 0; y + scroll_rows < pixel_rows; y++) {
        uint32_t* dst = fb + (uint64_t)y * fb_pixels_per_scanline;
        uint32_t* src = fb + (uint64_t)(y + scroll_rows) * fb_pixels_per_scanline;
        for (uint32_t x = 0; x < copy_width; x++) {
            dst[x] = src[x];
        }
    }
    for (uint32_t y = pixel_rows - scroll_rows; y < pixel_rows; y++) {
        uint32_t* row = fb + (uint64_t)y * fb_pixels_per_scanline;
        for (uint32_t x = 0; x < copy_width; x++) {
            row[x] = bg_color;
        }
    }
    cursor = columns * (rows - 1);
}

void Terminal::scroll() {
    scroll_text_buffer();
    if (use_framebuffer) {
        framebuffer_scroll();
    } else {
        vga_scroll();
    }
}

void Terminal::clear() {
    if (!active) {
        return;
    }
    if (use_framebuffer) {
        clear_text_buffer();
        for (uint32_t y = 0; y < fb_height; y++) {
            for (uint32_t x = 0; x < fb_width; x++) {
                putpixel(x, y, bg_color);
            }
        }
        cursor = 0;
        return;
    }

    clear_text_buffer();
    for (int i = 0; i < 80 * 25; i++) {
        vga[i * 2]     = ' ';
        vga[i * 2 + 1] = color;
    }
    cursor = 0;
    update_cursor();
}

void Terminal::putchar(char c) {
    if (!active) {
        return;
    }
    if (cursor >= columns * rows) {
        scroll();
    }

    if (c == '\n') {
        cursor = (cursor / columns + 1) * columns;
    } else if (c == '\b') {
        if (cursor > 0) {
            cursor--;
            put_text_cell(cursor, ' ');
            if (use_framebuffer) {
                clear_framebuffer_cell(cursor);
            } else {
                vga[cursor * 2]     = ' ';
                vga[cursor * 2 + 1] = color;
            }
        }
    } else {
        put_text_cell(cursor, c);
        if (use_framebuffer) {
            draw_framebuffer_char(cursor, c);
        } else {
            vga[cursor * 2]     = c;
            vga[cursor * 2 + 1] = color;
        }
        cursor++;
    }

    if (cursor >= columns * rows) {
        scroll();
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
