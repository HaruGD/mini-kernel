#ifndef KERNEL_KSH64_H
#define KERNEL_KSH64_H

#include <stdint.h>

#include "drivers/ata.h"
#include "drivers/pit.h"
#include "drivers/terminal.h"
#include "kernel/boot_info.h"

#define PROMPT "OS64> "

extern Terminal terminal;
extern ATADriver ata;
extern PIT pit;

const BootInfo* kernel_boot_info();
uint64_t kernel_boot_tsc();
uint32_t kernel_user_test_count();

const char* kernel_shell_prompt();

extern "C" void shell_recall_history(int direction);
extern "C" void shell_input(char ascii);

#endif
