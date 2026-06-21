#include <os64/os64.h>
#include "internal.h"

_Static_assert(sizeof(OsKeyEvent) == 16, "OsKeyEvent ABI changed");

static long read_key_event(OsKeyEvent* event, long blocking) {
    if (event == 0) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    return os_syscall2(OS_SYS_KEYBOARD_EVENT, (long)event, blocking);
}

long os_key_poll(OsKeyEvent* event) {
    return read_key_event(event, 0);
}

long os_key_wait(OsKeyEvent* event) {
    return read_key_event(event, 1);
}
