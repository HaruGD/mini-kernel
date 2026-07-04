#ifndef OS64_SERVICE_H
#define OS64_SERVICE_H

#include <stdint.h>
#include "os64/service_types.h"

long os_service_register(const char* name, uint32_t flags);
long os_service_find(const char* name, OsServiceInfo* info);
long os_service_unregister(const char* name);

#endif
