#ifndef OS64_INPUT_H
#define OS64_INPUT_H

#include "os64/input_types.h"

long os_key_poll(OsKeyEvent* event);
long os_key_wait(OsKeyEvent* event);
long os_key_wait_timeout(OsKeyEvent* event, uint32_t timeout_ticks);
long os_input_poll(OsInputEvent* event);
long os_input_wait(OsInputEvent* event);
long os_input_wait_timeout(OsInputEvent* event, uint32_t timeout_ticks);

#endif
