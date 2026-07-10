#include "kernel/service/service_registry.h"

#include "kernel/process64.h"

struct ServiceEntry {
    uint32_t owner_pid;
    uint32_t owner_generation;
    uint32_t state;
    uint32_t flags;
    uint32_t generation;
    char name[OS_SERVICE_NAME_MAX];
};

static ServiceEntry service_table[SERVICE_REGISTRY_CAPACITY];
static uint32_t service_generation_next = 1;

static int is_name_char(char ch) {
    if (ch >= 'a' && ch <= 'z') {
        return 1;
    }
    if (ch >= '0' && ch <= '9') {
        return 1;
    }
    return ch == '_' || ch == '-';
}

static int service_process_alive(uint32_t pid, uint32_t generation) {
    Process* process = find_process_by_identity_compat(pid, generation);
    if (process == 0 || process->pid == 0 || !process->active) {
        return 0;
    }
    return process->state != PROCESS_STATE_EMPTY &&
           process->state != PROCESS_STATE_RETURNED &&
           process->state != PROCESS_STATE_FAILED;
}

static void service_copy_name(char* dest, const char* src) {
    uint32_t i = 0;
    while (i + 1 < OS_SERVICE_NAME_MAX && src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

static int service_name_equal(const char* left, const char* right) {
    for (uint32_t i = 0; i < OS_SERVICE_NAME_MAX; i++) {
        if (left[i] != right[i]) {
            return 0;
        }
        if (left[i] == '\0') {
            return 1;
        }
    }
    return 1;
}

static uint32_t service_next_generation() {
    uint32_t generation = service_generation_next++;
    if (service_generation_next == 0) {
        service_generation_next = 1;
    }
    return generation == 0 ? service_next_generation() : generation;
}

static void service_clear_entry(ServiceEntry* entry) {
    if (entry == 0) {
        return;
    }
    entry->owner_pid = 0;
    entry->owner_generation = 0;
    entry->state = OS_SERVICE_STATE_EMPTY;
    entry->flags = OS_SERVICE_FLAG_NONE;
    entry->name[0] = '\0';
}

static void service_fill_info(const ServiceEntry* entry, OsServiceInfo* info) {
    info->size = sizeof(OsServiceInfo);
    info->owner_pid = entry->owner_pid;
    info->state = entry->state;
    info->flags = entry->flags;
    info->generation = entry->generation;
    service_copy_name(info->name, entry->name);
}

static void service_prune_stale() {
    for (uint32_t i = 0; i < SERVICE_REGISTRY_CAPACITY; i++) {
        ServiceEntry* entry = &service_table[i];
        if (entry->state == OS_SERVICE_STATE_REGISTERED &&
            !service_process_alive(entry->owner_pid, entry->owner_generation)) {
            service_clear_entry(entry);
        }
    }
}

void service_registry_init() {
    for (uint32_t i = 0; i < SERVICE_REGISTRY_CAPACITY; i++) {
        service_table[i].generation = 0;
        service_clear_entry(&service_table[i]);
    }
    service_generation_next = 1;
}

uint32_t service_registry_capacity() {
    return SERVICE_REGISTRY_CAPACITY;
}

uint32_t service_registry_count() {
    service_prune_stale();
    uint32_t count = 0;
    for (uint32_t i = 0; i < SERVICE_REGISTRY_CAPACITY; i++) {
        if (service_table[i].state == OS_SERVICE_STATE_REGISTERED) {
            count++;
        }
    }
    return count;
}

int service_name_valid(const char* name) {
    if (name == 0 || name[0] == '\0') {
        return 0;
    }
    if (!(name[0] >= 'a' && name[0] <= 'z')) {
        return 0;
    }

    for (uint32_t i = 0; i < OS_SERVICE_NAME_MAX; i++) {
        char ch = name[i];
        if (ch == '\0') {
            return 1;
        }
        if (!is_name_char(ch)) {
            return 0;
        }
    }
    return 0;
}

int service_register(Process* owner, const char* name, uint32_t flags) {
    service_prune_stale();
    if (owner == 0 || owner->pid == 0 || !owner->active) {
        return SERVICE_ERR_NOT_READY;
    }
    if (!service_name_valid(name)) {
        return SERVICE_ERR_INVALID_ARGUMENT;
    }
    if ((flags & ~OS_SERVICE_FLAG_SYSTEM) != 0) {
        return SERVICE_ERR_INVALID_ARGUMENT;
    }

    ServiceEntry* empty = 0;
    for (uint32_t i = 0; i < SERVICE_REGISTRY_CAPACITY; i++) {
        ServiceEntry* entry = &service_table[i];
        if (entry->state == OS_SERVICE_STATE_REGISTERED) {
            if (service_name_equal(entry->name, name)) {
                return SERVICE_ERR_ALREADY_EXISTS;
            }
            continue;
        }
        if (empty == 0) {
            empty = entry;
        }
    }
    if (empty == 0) {
        return SERVICE_ERR_NO_RESOURCES;
    }

    empty->owner_pid = owner->pid;
    empty->owner_generation = owner->generation;
    empty->state = OS_SERVICE_STATE_REGISTERED;
    empty->flags = flags;
    empty->generation = service_next_generation();
    service_copy_name(empty->name, name);
    return SERVICE_OK;
}

int service_find(const char* name, OsServiceInfo* info) {
    service_prune_stale();
    if (info == 0) {
        return SERVICE_ERR_BAD_BUFFER;
    }
    if (!service_name_valid(name)) {
        return SERVICE_ERR_INVALID_ARGUMENT;
    }

    for (uint32_t i = 0; i < SERVICE_REGISTRY_CAPACITY; i++) {
        ServiceEntry* entry = &service_table[i];
        if (entry->state == OS_SERVICE_STATE_REGISTERED &&
            service_name_equal(entry->name, name)) {
            service_fill_info(entry, info);
            return SERVICE_OK;
        }
    }
    return SERVICE_ERR_NOT_FOUND;
}

int service_unregister(Process* owner, const char* name) {
    service_prune_stale();
    if (owner == 0 || owner->pid == 0) {
        return SERVICE_ERR_NOT_READY;
    }
    if (!service_name_valid(name)) {
        return SERVICE_ERR_INVALID_ARGUMENT;
    }

    for (uint32_t i = 0; i < SERVICE_REGISTRY_CAPACITY; i++) {
        ServiceEntry* entry = &service_table[i];
        if (entry->state != OS_SERVICE_STATE_REGISTERED ||
            !service_name_equal(entry->name, name)) {
            continue;
        }
        if (entry->owner_pid != owner->pid ||
            entry->owner_generation != owner->generation) {
            return SERVICE_ERR_PERMISSION_DENIED;
        }
        service_clear_entry(entry);
        return SERVICE_OK;
    }
    return SERVICE_ERR_NOT_FOUND;
}

void service_unregister_owner(uint32_t owner_pid) {
    if (owner_pid == 0) {
        return;
    }
    for (uint32_t i = 0; i < SERVICE_REGISTRY_CAPACITY; i++) {
        ServiceEntry* entry = &service_table[i];
        if (entry->state == OS_SERVICE_STATE_REGISTERED &&
            entry->owner_pid == owner_pid) {
            service_clear_entry(entry);
        }
    }
}

int service_registry_get_info(uint32_t index, OsServiceInfo* info) {
    service_prune_stale();
    if (info == 0) {
        return SERVICE_ERR_BAD_BUFFER;
    }
    if (index >= SERVICE_REGISTRY_CAPACITY) {
        return SERVICE_ERR_INVALID_ARGUMENT;
    }
    if (service_table[index].state != OS_SERVICE_STATE_REGISTERED) {
        return SERVICE_ERR_NOT_FOUND;
    }
    service_fill_info(&service_table[index], info);
    return SERVICE_OK;
}

const char* service_state_name(uint32_t state) {
    if (state == OS_SERVICE_STATE_REGISTERED) {
        return "registered";
    }
    return "empty";
}
