#include "kernel/driver/driver_manager.h"
#include "kernel/kutil64.h"

static DriverExportRecord g_exports[DRIVER_MAX_EXPORTS];

static void clear_export(DriverExportRecord* record) {
    if (record == 0) return;
    record->active = 0;
    record->required_permission = 0;
    record->owner = driver_identity_invalid();
    record->resource = driver_resource_invalid();
    record->address = 0;
    record->module[0] = '\0';
    record->name[0] = '\0';
}

void driver_export_init() {
    for (uint32_t i = 0; i < DRIVER_MAX_EXPORTS; i++) {
        clear_export(&g_exports[i]);
    }
}

static int export_matches(const DriverExportRecord* record, const char* module, const char* name) {
    if (record == 0 || !record->active || name == 0) {
        return 0;
    }
    const char* module_name = module != 0 ? module : "kernel";
    return strcmp64(record->module, module_name) == 0 && strcmp64(record->name, name) == 0;
}

int driver_export_register(const char* module,
                           const char* name,
                           void* address,
                           uint32_t required_permission) {
    if (name == 0 || name[0] == '\0' || address == 0) {
        return DRIVER_LOAD_BAD_HEADER;
    }

    const char* module_name = module != 0 ? module : "kernel";
    DriverIdentity owner = driver_identity_invalid();
    if (strcmp64(module_name, "kernel") != 0) {
        owner = driver_manager_identity_from_name(module_name);
        if (!driver_manager_identity_accepts_resources(owner)) {
            return DRIVER_LOAD_STATE_DENIED;
        }
    }
    for (uint32_t i = 0; i < DRIVER_MAX_EXPORTS; i++) {
        if (export_matches(&g_exports[i], module_name, name)) {
            if (driver_identity_is_valid(owner) &&
                !driver_identity_equal(g_exports[i].owner, owner)) {
                return DRIVER_LOAD_STALE_IDENTITY;
            }
            g_exports[i].address = address;
            g_exports[i].required_permission = required_permission;
            return DRIVER_LOAD_OK;
        }
    }

    for (uint32_t i = 0; i < DRIVER_MAX_EXPORTS; i++) {
        if (!g_exports[i].active) {
            g_exports[i].active = 1;
            g_exports[i].required_permission = required_permission;
            g_exports[i].owner = owner;
            g_exports[i].address = address;
            copy_string64(g_exports[i].module, sizeof(g_exports[i].module), module_name);
            copy_string64(g_exports[i].name, sizeof(g_exports[i].name), name);
            if (driver_identity_is_valid(owner)) {
                int resource_result = driver_resource_register(
                    owner,
                    driver_device_identity_invalid(),
                    DRIVER_RESOURCE_EXPORT,
                    required_permission,
                    (uint64_t)(uintptr_t)address,
                    0,
                    name,
                    &g_exports[i].resource);
                if (resource_result != DRIVER_LOAD_OK) {
                    clear_export(&g_exports[i]);
                    return resource_result;
                }
            }
            return DRIVER_LOAD_OK;
        }
    }
    return DRIVER_LOAD_NO_SLOT;
}

void driver_export_unregister_module(const char* module) {
    const char* module_name = module != 0 ? module : "kernel";
    if (strcmp64(module_name, "kernel") == 0) {
        return;
    }

    for (uint32_t i = 0; i < DRIVER_MAX_EXPORTS; i++) {
        if (!g_exports[i].active) {
            continue;
        }
        if (strcmp64(g_exports[i].module, module_name) == 0) {
            if (driver_identity_is_valid(g_exports[i].owner)) {
                driver_resource_release(g_exports[i].owner,
                                        g_exports[i].resource,
                                        DRIVER_RESOURCE_EXPORT);
            }
            clear_export(&g_exports[i]);
        }
    }
}

void* driver_export_resolve(const char* module, const char* name, uint32_t granted_permissions) {
    for (uint32_t i = 0; i < DRIVER_MAX_EXPORTS; i++) {
        if (!export_matches(&g_exports[i], module, name)) {
            continue;
        }
        if (driver_identity_is_valid(g_exports[i].owner) &&
            (!driver_manager_identity_is_live(g_exports[i].owner) ||
             !driver_manager_identity_accepts_resources(g_exports[i].owner))) {
            return 0;
        }
        uint32_t required = g_exports[i].required_permission;
        if ((required & granted_permissions) != required) {
            return 0;
        }
        return g_exports[i].address;
    }
    return 0;
}

uint32_t driver_export_count() {
    uint32_t count = 0;
    for (uint32_t i = 0; i < DRIVER_MAX_EXPORTS; i++) {
        if (g_exports[i].active) {
            count++;
        }
    }
    return count;
}

const DriverExportRecord* driver_export_get(uint32_t index) {
    uint32_t seen = 0;
    for (uint32_t i = 0; i < DRIVER_MAX_EXPORTS; i++) {
        if (!g_exports[i].active) {
            continue;
        }
        if (seen == index) {
            return &g_exports[i];
        }
        seen++;
    }
    return 0;
}
