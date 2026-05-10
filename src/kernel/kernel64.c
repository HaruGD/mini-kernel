#include <stdint.h>
#include "kernel/boot_info.h"

#define COM1_PORT 0x3F8
#define PS2_STATUS_PORT 0x64
#define PS2_DATA_PORT 0x60
#define VGA_BUFFER ((volatile uint16_t*)0xB8000)
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_ATTR 0x0F00

#define INPUT_BUFFER_SIZE 256
#define NOTEBOOK_SIZE 512

static uint16_t vga_cursor = 0;
static uint8_t shift_pressed = 0;
static const BootInfo* current_boot_info = 0;

static char input_buffer[INPUT_BUFFER_SIZE];
static uint32_t input_length = 0;
static char notebook[NOTEBOOK_SIZE];
static uint8_t notebook_in_use = 0;

static const char keymap[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
  '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,  'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
  '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  '-', 0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const char keymap_shift[128] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
  '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,  'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,  '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
  '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  '-', 0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static int strcmp64(const char* a, const char* b) {
    while (*a && (*a == *b)) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static void strcpy64(char* dst, const char* src, uint32_t max_len) {
    uint32_t i = 0;
    while (src[i] && i + 1 < max_len) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static char* get_argument(char* input) {
    while (*input != ' ' && *input != '\0') {
        input++;
    }
    if (*input == ' ') {
        return input + 1;
    }
    return 0;
}

static void serial_init(void) {
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x80);
    outb(COM1_PORT + 0, 0x03);
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x03);
    outb(COM1_PORT + 2, 0xC7);
    outb(COM1_PORT + 4, 0x0B);
}

static void serial_write_raw_char(char c) {
    while ((inb(COM1_PORT + 5) & 0x20) == 0) {
    }
    outb(COM1_PORT, (uint8_t)c);
}

static void serial_write_char(char c) {
    if (c == '\n') {
        serial_write_raw_char('\r');
        serial_write_raw_char('\n');
        return;
    }

    if (c == '\b') {
        serial_write_raw_char('\b');
        serial_write_raw_char(' ');
        serial_write_raw_char('\b');
        return;
    }

    serial_write_raw_char(c);
}

static void serial_write_string(const char* s) {
    while (*s) {
        serial_write_char(*s++);
    }
}

static void serial_write_hex32(uint32_t value) {
    static const char hex[] = "0123456789ABCDEF";
    char buffer[10];
    buffer[0] = '0';
    buffer[1] = 'x';
    for (int i = 0; i < 8; i++) {
        buffer[9 - i] = hex[value & 0xF];
        value >>= 4;
    }
    for (int i = 0; i < 10; i++) {
        serial_write_char(buffer[i]);
    }
}

static void vga_scroll(void) {
    for (uint16_t row = 1; row < VGA_HEIGHT; row++) {
        for (uint16_t col = 0; col < VGA_WIDTH; col++) {
            VGA_BUFFER[(row - 1) * VGA_WIDTH + col] = VGA_BUFFER[row * VGA_WIDTH + col];
        }
    }

    for (uint16_t col = 0; col < VGA_WIDTH; col++) {
        VGA_BUFFER[(VGA_HEIGHT - 1) * VGA_WIDTH + col] = VGA_ATTR | ' ';
    }

    vga_cursor = (VGA_HEIGHT - 1) * VGA_WIDTH;
}

static void vga_clear(void) {
    for (uint16_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        VGA_BUFFER[i] = VGA_ATTR | ' ';
    }
    vga_cursor = 0;
}

static void vga_write_char(char c) {
    if (c == '\n') {
        vga_cursor = (vga_cursor / VGA_WIDTH + 1) * VGA_WIDTH;
        if (vga_cursor >= VGA_WIDTH * VGA_HEIGHT) {
            vga_scroll();
        }
        return;
    }

    if (c == '\b') {
        if (vga_cursor > 0) {
            vga_cursor--;
            VGA_BUFFER[vga_cursor] = VGA_ATTR | ' ';
        }
        return;
    }

    if (vga_cursor >= VGA_WIDTH * VGA_HEIGHT) {
        vga_scroll();
    }

    VGA_BUFFER[vga_cursor++] = VGA_ATTR | (uint8_t)c;
}

static void vga_write_string(const char* s) {
    while (*s) {
        vga_write_char(*s++);
    }
}

static void vga_write_hex32(uint32_t value) {
    static const char hex[] = "0123456789ABCDEF";
    char buffer[10];
    buffer[0] = '0';
    buffer[1] = 'x';
    for (int i = 0; i < 8; i++) {
        buffer[9 - i] = hex[value & 0xF];
        value >>= 4;
    }
    for (int i = 0; i < 10; i++) {
        vga_write_char(buffer[i]);
    }
}

static void write_char(char c) {
    serial_write_char(c);
    vga_write_char(c);
}

static void write_string(const char* s) {
    serial_write_string(s);
    vga_write_string(s);
}

static void write_hex32(uint32_t value) {
    serial_write_hex32(value);
    vga_write_hex32(value);
}

static void write_prompt(void) {
    write_string("OS64> ");
}

static void print_boot_info(void) {
    if (!current_boot_info) {
        write_string("BootInfo: null\n");
        return;
    }

    write_string("BootInfo magic: ");
    write_hex32(current_boot_info->magic);
    write_string("\nKernel load: ");
    write_hex32(current_boot_info->kernel_load_addr);
    write_string("\nKernel sectors: ");
    write_hex32(current_boot_info->kernel_sector_count);
    write_string("\nKernel bytes: ");
    write_hex32(current_boot_info->kernel_file_size);
    write_string("\nE820 entries: ");
    write_hex32(current_boot_info->memory_map_entry_count);
    write_string("\n");
}

static void print_memmap(void) {
    if (!current_boot_info) {
        write_string("No BootInfo\n");
        return;
    }

    const E820Entry* entries = (const E820Entry*)(uintptr_t)current_boot_info->memory_map_addr;
    for (uint32_t i = 0; i < current_boot_info->memory_map_entry_count; i++) {
        write_string("E820[");
        write_hex32(i);
        write_string("] base=");
        write_hex32(entries[i].base_high);
        write_hex32(entries[i].base_low);
        write_string(" len=");
        write_hex32(entries[i].length_high);
        write_hex32(entries[i].length_low);
        write_string(" type=");
        write_hex32(entries[i].type);
        write_string("\n");
    }
}

static void execute_command(void) {
    input_buffer[input_length] = '\0';

    char cmd[32];
    uint32_t i = 0;
    while (input_buffer[i] != ' ' && input_buffer[i] != '\0' && i < 31) {
        cmd[i] = input_buffer[i];
        i++;
    }
    cmd[i] = '\0';

    char* arg = get_argument(input_buffer);

    if (strcmp64(cmd, "") == 0) {
    } else if (strcmp64(cmd, "help") == 0) {
        write_string("\nhelp clear version bootinfo memmap echo write read free");
    } else if (strcmp64(cmd, "clear") == 0) {
        vga_clear();
    } else if (strcmp64(cmd, "version") == 0) {
        write_string("\n[mini-kernel] x86_64 long mode shell");
    } else if (strcmp64(cmd, "bootinfo") == 0) {
        write_string("\n");
        print_boot_info();
    } else if (strcmp64(cmd, "memmap") == 0) {
        write_string("\n");
        print_memmap();
    } else if (strcmp64(cmd, "echo") == 0) {
        write_string("\n");
        if (arg) {
            write_string(arg);
        }
    } else if (strcmp64(cmd, "write") == 0) {
        write_string("\n");
        if (!arg) {
            write_string("Usage: write [message]");
        } else {
            strcpy64(notebook, arg, NOTEBOOK_SIZE);
            notebook_in_use = 1;
        }
    } else if (strcmp64(cmd, "read") == 0) {
        write_string("\n");
        if (!notebook_in_use) {
            write_string("Notebook is empty.");
        } else {
            write_string("Content: ");
            write_string(notebook);
        }
    } else if (strcmp64(cmd, "free") == 0) {
        write_string("\n");
        notebook[0] = '\0';
        notebook_in_use = 0;
        write_string("Notebook cleared.");
    } else {
        write_string("\nUnknown command: ");
        write_string(cmd);
    }

    input_length = 0;
    input_buffer[0] = '\0';
    write_string("\n");
    write_prompt();
}

static void shell_input(char c) {
    if (c == '\r') {
        c = '\n';
    }

    if (c == '\n') {
        execute_command();
        return;
    }

    if (c == '\b') {
        if (input_length > 0) {
            input_length--;
            input_buffer[input_length] = '\0';
            write_char('\b');
        }
        return;
    }

    if (c < 32 || c > 126) {
        return;
    }

    if (input_length + 1 >= INPUT_BUFFER_SIZE) {
        return;
    }

    input_buffer[input_length++] = c;
    input_buffer[input_length] = '\0';
    write_char(c);
}

static void handle_scancode(uint8_t scancode) {
    if (scancode == 0x2A || scancode == 0x36) {
        shift_pressed = 1;
        return;
    }

    if (scancode == 0xAA || scancode == 0xB6) {
        shift_pressed = 0;
        return;
    }

    if (scancode & 0x80) {
        return;
    }

    char c = shift_pressed ? keymap_shift[scancode] : keymap[scancode];
    if (c != 0) {
        shell_input(c);
    }
}

static void poll_keyboard(void) {
    if ((inb(PS2_STATUS_PORT) & 0x01) == 0) {
        return;
    }

    handle_scancode(inb(PS2_DATA_PORT));
}

void kernel64_main(const BootInfo* boot_info) {
    current_boot_info = boot_info;
    serial_init();
    vga_clear();

    write_string("Long mode OK\n");
    print_boot_info();
    write_string("Keyboard polling ready\n");
    write_prompt();

    for (;;) {
        poll_keyboard();
        __asm__ volatile ("pause");
    }
}
