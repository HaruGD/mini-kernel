#include "kernel/driver/driver_manager.h"
#include "kernel/driver/drv_format.h"
#include "kernel/driver/driver_va.h"
#include "kernel/kutil64.h"

static DriverRecord g_drivers[DRIVER_MAX_RECORDS];
static DriverLoadDiagnostics g_last_error;
static char g_lifecycle_driver[32];

static uint32_t next_generation(uint32_t generation) {
    generation++;
    return generation != 0 ? generation : 1u;
}

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

static void activate_driver_record(DriverRecord* record) {
    record->generation = next_generation(record->generation);
    record->active = 1;
    record->state = DRIVER_STATE_REGISTERED;
}

void driver_manager_init() {
    for (uint32_t i = 0; i < DRIVER_MAX_RECORDS; i++) {
        g_drivers[i].slot = i;
        g_drivers[i].generation = 0;
        clear_driver_record(&g_drivers[i]);
    }
    driver_resource_init();
    driver_image_va_init();
    driver_manager_binding_init();
    driver_irq_init();
    driver_export_init();
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
    if (record != 0) {
        if (!driver_record_can_be_reused(record)) {
            return DRIVER_LOAD_DUPLICATE;
        }
        DriverIdentity old_identity = {record->slot, record->generation};
        driver_resource_release_owner(old_identity);
        clear_driver_record(record);
    }
    if (record == 0) record = alloc_driver_record();
    if (record == 0) {
        return DRIVER_LOAD_NO_SLOT;
    }

    activate_driver_record(record);
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
        DriverIdentity old_identity = {existing->slot, existing->generation};
        driver_resource_release_owner(old_identity);
        clear_driver_record(existing);
    }

    DriverRecord* record = alloc_driver_record();
    if (record == 0) {
        return DRIVER_LOAD_NO_SLOT;
    }

    activate_driver_record(record);
    record->kind = DRIVER_KIND_MODULE;
    record->permissions = manifest->permissions;
    record->instance = instance;
    copy_string64(record->name, sizeof(record->name), manifest->name);
    copy_string64(record->version, sizeof(record->version), manifest->version);
    return DRIVER_LOAD_OK;
}

static int legal_state_transition(uint32_t from, uint32_t to) {
    if (from == to) return 1;
    if (from == DRIVER_STATE_REGISTERED) {
        return to == DRIVER_STATE_LOADING || to == DRIVER_STATE_LINKED ||
               to == DRIVER_STATE_READY || to == DRIVER_STATE_FAILED ||
               to == DRIVER_STATE_REJECTED;
    }
    if (from == DRIVER_STATE_LOADING) {
        return to == DRIVER_STATE_LINKED || to == DRIVER_STATE_FAILED ||
               to == DRIVER_STATE_REJECTED;
    }
    if (from == DRIVER_STATE_LINKED) {
        return to == DRIVER_STATE_READY || to == DRIVER_STATE_FAILED ||
               to == DRIVER_STATE_REJECTED || to == DRIVER_STATE_QUIESCING;
    }
    if (from == DRIVER_STATE_READY) {
        return to == DRIVER_STATE_QUIESCING || to == DRIVER_STATE_FAILED;
    }
    if (from == DRIVER_STATE_QUIESCING) {
        return to == DRIVER_STATE_FAILED;
    }
    return 0;
}

DriverIdentity driver_identity_invalid() {
    DriverIdentity identity = {DRIVER_IDENTITY_INVALID_SLOT, 0};
    return identity;
}

int driver_identity_is_valid(DriverIdentity identity) {
    return identity.slot < DRIVER_MAX_RECORDS && identity.generation != 0;
}

int driver_identity_equal(DriverIdentity left, DriverIdentity right) {
    return left.slot == right.slot && left.generation == right.generation;
}

DriverIdentity driver_manager_identity_from_name(const char* name) {
    DriverRecord* record = find_driver_by_name(name);
    if (record == 0) return driver_identity_invalid();
    DriverIdentity identity = {record->slot, record->generation};
    return identity;
}

int driver_manager_identity_is_live(DriverIdentity identity) {
    if (!driver_identity_is_valid(identity)) return 0;
    const DriverRecord* record = &g_drivers[identity.slot];
    return record->active && record->generation == identity.generation;
}

int driver_manager_identity_accepts_resources(DriverIdentity identity) {
    if (!driver_manager_identity_is_live(identity)) return 0;
    const uint32_t state = g_drivers[identity.slot].state;
    return state == DRIVER_STATE_REGISTERED || state == DRIVER_STATE_LOADING ||
           state == DRIVER_STATE_LINKED || state == DRIVER_STATE_READY;
}

int driver_manager_set_state_identity(DriverIdentity identity, uint32_t state) {
    if (!driver_manager_identity_is_live(identity)) {
        return DRIVER_LOAD_STALE_IDENTITY;
    }
    DriverRecord* record = &g_drivers[identity.slot];
    if (!legal_state_transition(record->state, state)) {
        driver_manager_set_last_error(DRIVER_LOAD_STATE_DENIED,
                                      "state",
                                      record->name,
                                      driver_state_name(state),
                                      record->state,
                                      state);
        return DRIVER_LOAD_STATE_DENIED;
    }
    record->state = (uint8_t)state;
    return DRIVER_LOAD_OK;
}

int driver_manager_set_state(const char* name, uint32_t state) {
    DriverIdentity identity = driver_manager_identity_from_name(name);
    if (!driver_identity_is_valid(identity)) return DRIVER_LOAD_BAD_HEADER;
    return driver_manager_set_state_identity(identity, state);
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
    DriverIdentity identity = {record->slot, record->generation};
    driver_resource_release_owner(identity);
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
    if (state == DRIVER_STATE_QUIESCING) return "quiescing";
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
