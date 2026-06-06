#ifndef KERNEL_KUTIL64_H
#define KERNEL_KUTIL64_H

#include <stdint.h>

int strlen64(const char* str);
int strcmp64(const char* a, const char* b);
void copy_string64(char* dest, uint32_t capacity, const char* src);
char to_lower_ascii(char c);
int is_space64(char c);

void serial_init();
int serial_ready();
void serial_putchar(char c);
void putchar_both(char c);
void print(const char* str);
void print_n(const char* str, uint64_t len);
void print_hex32(uint32_t value);
void print_hex64(uint64_t value);
uint64_t read_tsc();

extern "C" void debug_print64(const char* str);
extern "C" void debug_print_hex64(uint32_t value);
extern "C" void debug_print_hex64_u64(uint64_t value);

#endif
