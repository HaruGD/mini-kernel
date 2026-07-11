#ifndef KERNEL_SERVICE_REGISTRY_H
#define KERNEL_SERVICE_REGISTRY_H

#include <stdint.h>

#include "kernel/process.h"
#include "os64/service_types.h"

#define SERVICE_REGISTRY_CAPACITY 16u

#define SERVICE_OK 0
#define SERVICE_ERR_NOT_READY (-1)
#define SERVICE_ERR_INVALID_ARGUMENT (-2)
#define SERVICE_ERR_NOT_FOUND (-3)
#define SERVICE_ERR_ALREADY_EXISTS (-6)
#define SERVICE_ERR_NO_RESOURCES (-7)
#define SERVICE_ERR_PERMISSION_DENIED (-15)
#define SERVICE_ERR_BAD_BUFFER (-16)

struct ServiceRegistrySnapshot {
    uint32_t capacity;
    uint32_t count;
    OsServiceInfo entries[SERVICE_REGISTRY_CAPACITY];
};

#ifdef __cplusplus
extern "C" {
#endif

void service_registry_init();
uint32_t service_registry_capacity();
uint32_t service_registry_count();
int service_name_valid(const char* name);
int service_register(Process* owner, const char* name, uint32_t flags);
int service_find(const char* name, OsServiceInfo* info);
int service_unregister(Process* owner, const char* name);
void service_unregister_owner(uint32_t owner_pid);
int service_registry_get_info(uint32_t index, OsServiceInfo* info);
void service_registry_get_snapshot(ServiceRegistrySnapshot* snapshot);
const char* service_state_name(uint32_t state);

#ifdef __cplusplus
}
#endif

#endif
