#ifndef OS64_TERMINAL_H
#define OS64_TERMINAL_H

#include <stdint.h>

#include "os64/graphics_types.h"
#include "os64/process_types.h"
#include "os64/terminal_types.h"

void os_terminal_packet_init(OsTerminalPacket* packet, uint32_t command);
long os_terminal_packet_validate(const OsTerminalPacket* packet);
long os_terminal_send(OsProcessIdentity peer,
                      OsTerminalPacket* packet,
                      uint32_t retry_ticks);
long os_terminal_session_bind(OsProcessIdentity peer);
long os_terminal_session_exit(int32_t status);
long os_terminal_session_read(OsProcessIdentity owner,
                              OsTerminalPacket* packet);
long os_terminal_session_close(OsProcessIdentity owner);

long os_terminal_model_init(OsTerminalModel* model,
                            uint32_t columns,
                            uint32_t rows);
long os_terminal_model_resize(OsTerminalModel* model,
                              uint32_t columns,
                              uint32_t rows);
long os_terminal_model_write(OsTerminalModel* model,
                             const void* bytes,
                             uint32_t length);
long os_terminal_model_scroll(OsTerminalModel* model, int32_t rows);
long os_terminal_model_render(const OsTerminalModel* model,
                              OsSurfaceCanvas* canvas,
                              OsRect bounds);

#endif
