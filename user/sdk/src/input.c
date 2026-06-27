#include <os64/os64.h>
#include "internal.h"

_Static_assert(sizeof(OsKeyEvent) == 16, "OsKeyEvent ABI changed");
_Static_assert(sizeof(OsPointerEvent) == 32, "OsPointerEvent ABI changed");
_Static_assert(sizeof(OsInputEvent) == 48, "OsInputEvent ABI changed");

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

long os_input_poll(OsInputEvent* event) {
    if (event == 0) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    return os_syscall1(OS_SYS_INPUT_EVENT_POLL, (long)event);
}

long os_input_wait(OsInputEvent* event) {
    if (event == 0) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    return os_syscall1(OS_SYS_INPUT_EVENT_WAIT, (long)event);
}
