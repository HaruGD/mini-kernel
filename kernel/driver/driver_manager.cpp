#include "kernel/driver/driver_manager.h"
#include "kernel/driver/drv_format.h"
#include "kernel/kutil64.h"

static DriverRecord g_drivers[DRIVER_MAX_RECORDS];
static DriverLoadDiagnostics g_last_error;
static char g_lifecycle_driver[32];

static void clear_driver_record(DriverRecord* record) {
    if (record == 0) {
        return;
    }
    record->active = 0;
    record->state = DRIVER_STATE_EMPTY;
    record->kind = 0;
    record->permissions = 0;
    record->name[0] = '\0';
    record->version[0] = '\0';
    record->instance = 0;
}

void driver_manager_init() {
    for (uint32_t i = 0; i < DRIVER_MAX_RECORDS; i++) {
        clear_driver_record(&g_drivers[i]);
    }
    g_lifecycle_driver[0] = '\0';
    driver_manager_clear_last_error();
}

static int driver_name_matches(const DriverRecord* record, const char* name) {
    return record != 0 && record->active && strcmp64(record->name, name) == 0;
}

static DriverRecord* find_driver_by_name(const char* name) {
    if (name == 0) {
        return 0;
    }
    for (uint32_t i = 0; i < DRIVER_MAX_RECORDS; i++) {
        if (driver_name_matches(&g_drivers[i], name)) {
            return &g_drivers[i];
        }
    }
    return 0;
}

static DriverRecord* alloc_driver_record() {
    for (uint32_t i = 0; i < DRIVER_MAX_RECORDS; i++) {
        if (!g_drivers[i].active) {
            return &g_drivers[i];
        }
    }
    return 0;
}

static int driver_record_can_be_reused(const DriverRecord* record) {
    if (record == 0 || !record->active) {
        return 1;
    }
    return record->state == DRIVER_STATE_FAILED || record->state == DRIVER_STATE_REJECTED;
}

int driver_manager_register_builtin(const char* name,
                                    const char* version,
                                    uint32_t kind,
                                    uint32_t permissions,
                                    void* instance) {
    if (name == 0 || name[0] == '\0') {
        return DRIVER_LOAD_BAD_HEADER;
    }

    DriverRecord* record = find_driver_by_name(name);
    if (record == 0) {
        record = alloc_driver_record();
    }
    if (record == 0) {
        return DRIVER_LOAD_NO_SLOT;
    }

    record->active = 1;
    record->state = DRIVER_STATE_REGISTERED;
    record->kind = (uint16_t)kind;
    record->permissions = permissions;
    record->instance = instance;
    copy_string64(record->name, sizeof(record->name), name);
    copy_string64(record->version, sizeof(record->version), version != 0 ? version : "builtin");
    return DRIVER_LOAD_OK;
}

int driver_manager_register_package_manifest(const DrvManifest* manifest, void* instance) {
    if (manifest == 0 || manifest->name[0] == '\0') {
        return DRIVER_LOAD_BAD_HEADER;
    }

    DriverRecord* existing = find_driver_by_name(manifest->name);
    if (existing != 0) {
        if (!driver_record_can_be_reused(existing)) {
            return DRIVER_LOAD_DUPLICATE;
        }
        clear_driver_record(existing);
    }

    DriverRecord* record = alloc_driver_record();
    if (record == 0) {
        return DRIVER_LOAD_NO_SLOT;
    }

    record->active = 1;
    record->state = DRIVER_STATE_REGISTERED;
    record->kind = DRIVER_KIND_MODULE;
    record->permissions = manifest->permissions;
    record->instance = instance;
    copy_string64(record->name, sizeof(record->name), manifest->name);
    copy_string64(record->version, sizeof(record->version), manifest->version);
    return DRIVER_LOAD_OK;
}

int driver_manager_set_state(const char* name, uint32_t state) {
    DriverRecord* record = find_driver_by_name(name);
    if (record == 0) {
        return DRIVER_LOAD_BAD_HEADER;
    }
    record->state = (uint8_t)state;
    return DRIVER_LOAD_OK;
}

int driver_manager_set_instance(const char* name, void* instance) {
    DriverRecord* record = find_driver_by_name(name);
    if (record == 0) {
        return DRIVER_LOAD_BAD_HEADER;
    }
    record->instance = instance;
    return DRIVER_LOAD_OK;
}

int driver_manager_unregister(const char* name) {
    DriverRecord* record = find_driver_by_name(name);
    if (record == 0) {
        return DRIVER_LOAD_BAD_HEADER;
    }
    clear_driver_record(record);
    return DRIVER_LOAD_OK;
}

uint32_t driver_manager_count() {
    uint32_t count = 0;
    for (uint32_t i = 0; i < DRIVER_MAX_RECORDS; i++) {
        if (g_drivers[i].active) {
            count++;
        }
    }
    return count;
}

const DriverRecord* driver_manager_get(uint32_t index) {
    uint32_t seen = 0;
    for (uint32_t i = 0; i < DRIVER_MAX_RECORDS; i++) {
        if (!g_drivers[i].active) {
            continue;
        }
        if (seen == index) {
            return &g_drivers[i];
        }
        seen++;
    }
    return 0;
}

const DriverRecord* driver_manager_find(const char* name) {
    return find_driver_by_name(name);
}

const char* driver_state_name(uint32_t state) {
    if (state == DRIVER_STATE_REGISTERED) return "registered";
    if (state == DRIVER_STATE_READY) return "ready";
    if (state == DRIVER_STATE_FAILED) return "failed";
    if (state == DRIVER_STATE_REJECTED) return "rejected";
    if (state == DRIVER_STATE_LOADING) return "loading";
    if (state == DRIVER_STATE_LINKED) return "linked";
    return "empty";
}

const char* driver_kind_name(uint32_t kind) {
    if (kind == DRIVER_KIND_CORE) return "core";
    if (kind == DRIVER_KIND_BUS) return "bus";
    if (kind == DRIVER_KIND_BLOCK) return "block";
    if (kind == DRIVER_KIND_INPUT) return "input";
    if (kind == DRIVER_KIND_TIMER) return "timer";
    if (kind == DRIVER_KIND_FS) return "fs";
    if (kind == DRIVER_KIND_DISPLAY) return "display";
    if (kind == DRIVER_KIND_MODULE) return "module";
    return "unknown";
}

void driver_manager_set_lifecycle_driver(const char* name) {
    copy_string64(g_lifecycle_driver, sizeof(g_lifecycle_driver), name != 0 ? name : "");
}

const char* driver_manager_current_lifecycle_driver() {
    return g_lifecycle_driver[0] != '\0' ? g_lifecycle_driver : 0;
}

void driver_manager_clear_last_error() {
    g_last_error.result = DRIVER_LOAD_OK;
    g_last_error.index = 0;
    g_last_error.detail = 0;
    g_last_error.stage[0] = '\0';
    g_last_error.module[0] = '\0';
    g_last_error.name[0] = '\0';
}

void driver_manager_set_last_error(int result,
                                   const char* stage,
                                   const char* module,
                                   const char* name,
                                   uint32_t index,
                                   uint64_t detail) {
    g_last_error.result = result;
    g_last_error.index = index;
    g_last_error.detail = detail;
    copy_string64(g_last_error.stage, sizeof(g_last_error.stage), stage != 0 ? stage : "");
    copy_string64(g_last_error.module, sizeof(g_last_error.module), module != 0 ? module : "");
    copy_string64(g_last_error.name, sizeof(g_last_error.name), name != 0 ? name : "");
}

const DriverLoadDiagnostics* driver_manager_last_error() {
    return &g_last_error;
}
