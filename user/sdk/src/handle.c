#include "os64/handle.h"

#include "internal.h"

long os_handle_close(OsHandle handle) {
    return handle != 0 ? os_syscall1(OS_SYS_HANDLE_CLOSE, (long)handle) : -2;
}
