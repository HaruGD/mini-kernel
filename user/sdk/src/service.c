#include <os64/os64.h>
#include "internal.h"

long os_service_register(const char* name, uint32_t flags) {
    return os_syscall2(OS_SYS_SERVICE_REGISTER, (long)name, (long)flags);
}

long os_service_find(const char* name, OsServiceInfo* info) {
    return os_syscall2(OS_SYS_SERVICE_FIND, (long)name, (long)info);
}

long os_service_find_owner_identity(const char* name, OsProcessIdentity* identity) {
    return os_syscall2(OS_SYS_SERVICE_FIND_OWNER_IDENTITY,
                       (long)name,
                       (long)identity);
}

long os_service_unregister(const char* name) {
    return os_syscall1(OS_SYS_SERVICE_UNREGISTER, (long)name);
}
