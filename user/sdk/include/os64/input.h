#ifndef OS64_INPUT_H
#define OS64_INPUT_H

#include "os64/input_types.h"

long os_key_poll(OsKeyEvent* event);
long os_key_wait(OsKeyEvent* event);
long os_input_poll(OsInputEvent* event);
long os_input_wait(OsInputEvent* event);

#endif
