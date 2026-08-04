#ifndef KERNEL_PROCESS_TERMINAL_H
#define KERNEL_PROCESS_TERMINAL_H

#include <stdint.h>

#include "kernel/process.h"

int process_terminal_bind(Process* process, ProcessIdentity peer);
int process_terminal_attached(const Process* process);
void process_terminal_inherit(Process* child, const Process* parent);
int process_terminal_write(Process* process, const uint8_t* bytes, uint32_t length);
int process_terminal_read_char(Process* process, uint8_t* character);
int process_terminal_clear(Process* process);
int process_terminal_send_exit(Process* process, int32_t status);
int process_terminal_read_output(Process* peer,
                                 ProcessIdentity owner_identity,
                                 OsTerminalPacket* packet);
int process_terminal_close(Process* peer, ProcessIdentity owner_identity);
void process_terminal_notify_message(Process* target);
void process_terminal_print(const char* text);
void process_terminal_print_hex32(uint32_t value);
void process_terminal_print_hex64(uint64_t value);

#endif
