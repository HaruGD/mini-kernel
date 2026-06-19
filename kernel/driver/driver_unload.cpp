#include "kernel/driver/driver_manager.h"
#include "kernel/driver/drv_format.h"
#include "kernel/kutil64.h"
#include "arch/x86_64/paging64.h"
#include "arch/x86_64/pmm64.h"

typedef uint64_t (*DriverLifecycleFn)();

static void free_loaded_image(DriverLoadedImage* loaded) {
    if (loaded == 0) {
        return;
    }
    for (uint32_t i = 0; i < loaded->section_count && i < DRIVER_MAX_LOADED_SECTIONS; i++) {
        if (loaded->sections[i].base != 0) {
            if (loaded->sections[i].page_count == 0) {
                delete[] loaded->sections[i].base;
                loaded->sections[i].base = 0;
                continue;
            }
            uint64_t base = (uint64_t)(uintptr_t)loaded->sections[i].base;
            for (uint32_t page = 0; page < loaded->sections[i].page_count; page++) {
                uint64_t virt = base + ((uint64_t)page * PAGING64_PAGE_SIZE);
                uint64_t phys = paging64_get_phys(virt) & 0x000FFFFFFFFFF000ULL;
                paging64_unmap_page(virt);
                if (phys != 0) {
                    pmm64_free_block((void*)(uintptr_t)phys);
                }
            }
            loaded->sections[i].base = 0;
            loaded->sections[i].page_count = 0;
        }
    }
    delete loaded;
}

static int call_driver_exit(const DriverRecord* record, DriverLoadedImage* loaded) {
    if (record == 0 || loaded == 0 || loaded->exit == 0) {
        return DRIVER_LOAD_OK;
    }

    DriverLifecycleFn exit_fn = (DriverLifecycleFn)loaded->exit;
    driver_manager_set_lifecycle_driver(record->name);
    uint64_t exit_result = exit_fn();
    driver_manager_set_lifecycle_driver(0);
    if (exit_result != 0) {
        driver_manager_set_last_error(DRIVER_LOAD_EXIT_FAILED, "exit", record->name, "driver_exit", 0, exit_result);
        return DRIVER_LOAD_EXIT_FAILED;
    }
    return DRIVER_LOAD_OK;
}

static int loaded_image_imports_module(const DriverLoadedImage* loaded, const char* module) {
    if (loaded == 0 || module == 0) {
        return 0;
    }
    for (uint32_t i = 0; i < loaded->import_count && i < DRIVER_MAX_RESOLVED_IMPORTS; i++) {
        if (strcmp64(loaded->imports[i].module, module) == 0) {
            return 1;
        }
    }
    return 0;
}

static int loaded_image_depends_on_module(const DriverLoadedImage* loaded, const char* module) {
    if (loaded == 0 || module == 0) {
        return 0;
    }
    for (uint32_t i = 0; i < loaded->dependency_count && i < DRIVER_MAX_DEPENDENCIES; i++) {
        if (strcmp64(loaded->dependencies[i].name, module) == 0) {
            return 1;
        }
    }
    return 0;
}

static int driver_has_ready_dependents(const char* name) {
    uint32_t count = driver_manager_count();
    for (uint32_t i = 0; i < count; i++) {
        const DriverRecord* record = driver_manager_get(i);
        if (record == 0 || !record->active || strcmp64(record->name, name) == 0) {
            continue;
        }
        if (record->kind != DRIVER_KIND_MODULE || record->instance == 0) {
            continue;
        }
        if (record->state != DRIVER_STATE_READY && record->state != DRIVER_STATE_LINKED) {
            continue;
        }
        const DriverLoadedImage* loaded = (const DriverLoadedImage*)record->instance;
        if (loaded_image_imports_module(loaded, name) ||
            loaded_image_depends_on_module(loaded, name)) {
            driver_manager_set_last_error(DRIVER_LOAD_UNLOAD_DENIED, "unload", name, record->name, i, 0);
            return 1;
        }
    }
    return 0;
}

int driver_manager_unload(const char* name) {
    driver_manager_clear_last_error();
    if (name == 0 || name[0] == '\0') {
        driver_manager_set_last_error(DRIVER_LOAD_BAD_HEADER, "unload", 0, "name", 0, 0);
        return DRIVER_LOAD_BAD_HEADER;
    }

    uint32_t count = driver_manager_count();
    const DriverRecord* record = 0;
    for (uint32_t i = 0; i < count; i++) {
        const DriverRecord* candidate = driver_manager_get(i);
        if (candidate != 0 && candidate->active && strcmp64(candidate->name, name) == 0) {
            record = candidate;
            break;
        }
    }

    if (record == 0) {
        driver_manager_set_last_error(DRIVER_LOAD_BAD_HEADER, "unload", name, "not_found", 0, 0);
        return DRIVER_LOAD_BAD_HEADER;
    }
    if (record->kind != DRIVER_KIND_MODULE) {
        driver_manager_set_last_error(DRIVER_LOAD_UNLOAD_DENIED, "unload", name, "builtin", 0, record->kind);
        return DRIVER_LOAD_UNLOAD_DENIED;
    }
    if (record->state == DRIVER_STATE_LOADING) {
        driver_manager_set_last_error(DRIVER_LOAD_UNLOAD_DENIED, "unload", name, "loading", 0, 0);
        return DRIVER_LOAD_UNLOAD_DENIED;
    }
    if (driver_has_ready_dependents(name)) {
        return DRIVER_LOAD_UNLOAD_DENIED;
    }

    DriverRecord snapshot = *record;
    DriverLoadedImage* loaded = (DriverLoadedImage*)snapshot.instance;
    int exit_result = call_driver_exit(&snapshot, loaded);
    if (exit_result != DRIVER_LOAD_OK) {
        driver_manager_set_state(snapshot.name, DRIVER_STATE_FAILED);
        return exit_result;
    }

    driver_export_unregister_module(snapshot.name);
    driver_irq_unregister_module(snapshot.name);
    driver_manager_unbind_module(snapshot.name);
    free_loaded_image(loaded);
    driver_manager_unregister(snapshot.name);
    return DRIVER_LOAD_OK;
}

int driver_manager_reload_drv_image(const uint8_t* image, uint64_t size) {
    int validation = driver_manager_validate_drv_image(image, size);
    if (validation != DRIVER_LOAD_OK) {
        return validation;
    }

    const DrvHeader* header = (const DrvHeader*)image;
    const DrvManifest* manifest = (const DrvManifest*)(image + header->manifest_offset);
    const char* name = manifest->name;

    uint32_t count = driver_manager_count();
    for (uint32_t i = 0; i < count; i++) {
        const DriverRecord* record = driver_manager_get(i);
        if (record != 0 && record->active && strcmp64(record->name, name) == 0) {
            int unload_result = driver_manager_unload(name);
            if (unload_result != DRIVER_LOAD_OK) {
                return unload_result;
            }
            break;
        }
    }

    return driver_manager_load_drv_image(image, size);
}
