#ifndef KERNEL_H
#define KERNEL_H

#ifdef __cplusplus
extern "C" {
#endif

int strlen(const char* str);
int strcmp(const char* s1, const char* s2);
void kernel_main();
void keyboard_handler_c();

#ifdef __cplusplus
}
#endif

#endif