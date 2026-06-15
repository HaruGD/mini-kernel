#include "kernel/driver/driver_manager.h"
#include "kernel/kutil64.h"

static DriverExportRecord g_exports[DRIVER_MAX_EXPORTS];

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
    for (uint32_t i = 0; i < DRIVER_MAX_EXPORTS; i++) {
        if (export_matches(&g_exports[i], module_name, name)) {
            g_exports[i].address = address;
            g_exports[i].required_permission = required_permission;
            return DRIVER_LOAD_OK;
        }
    }

    for (uint32_t i = 0; i < DRIVER_MAX_EXPORTS; i++) {
        if (!g_exports[i].active) {
            g_exports[i].active = 1;
            g_exports[i].required_permission = required_permission;
            g_exports[i].address = address;
            copy_string64(g_exports[i].module, sizeof(g_exports[i].module), module_name);
            copy_string64(g_exports[i].name, sizeof(g_exports[i].name), name);
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
            g_exports[i].active = 0;
            g_exports[i].required_permission = 0;
            g_exports[i].address = 0;
            g_exports[i].module[0] = '\0';
            g_exports[i].name[0] = '\0';
        }
    }
}

void* driver_export_resolve(const char* module, const char* name, uint32_t granted_permissions) {
    for (uint32_t i = 0; i < DRIVER_MAX_EXPORTS; i++) {
        if (!export_matches(&g_exports[i], module, name)) {
            continue;
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
