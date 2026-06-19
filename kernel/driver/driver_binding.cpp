#include "kernel/driver/driver_manager.h"
#include "kernel/driver/drv_format.h"
#include "kernel/kutil64.h"
#include "kernel/pci.h"

static DriverBindingRecord g_bindings[DRIVER_MAX_BINDINGS];

typedef uint64_t (*DriverPciProbeFn)(const PCIDeviceInfo* device);

static void clear_binding(DriverBindingRecord* binding) {
    if (binding == 0) {
        return;
    }
    binding->active = 0;
    binding->kind = 0;
    binding->flags = 0;
    binding->driver[0] = '\0';
    binding->vendor_id = 0;
    binding->device_id = 0;
    binding->bus = 0;
    binding->device = 0;
    binding->function = 0;
    binding->class_code = 0;
    binding->subclass = 0;
    binding->prog_if = 0;
}

static int same_pci_device(const DriverBindingRecord* binding, const PCIDeviceInfo* device) {
    return binding != 0 &&
           device != 0 &&
           binding->active &&
           binding->kind == DRIVER_BIND_KIND_PCI &&
           binding->bus == device->bus &&
           binding->device == device->device &&
           binding->function == device->function;
}

static DriverBindingRecord* find_pci_binding(const PCIDeviceInfo* device) {
    for (uint32_t i = 0; i < DRIVER_MAX_BINDINGS; i++) {
        if (same_pci_device(&g_bindings[i], device)) {
            return &g_bindings[i];
        }
    }
    return 0;
}

static DriverBindingRecord* alloc_binding() {
    for (uint32_t i = 0; i < DRIVER_MAX_BINDINGS; i++) {
        if (!g_bindings[i].active) {
            return &g_bindings[i];
        }
    }
    return 0;
}

int driver_manager_bind_pci(const char* driver_name, const PCIDeviceInfo* device, uint32_t flags) {
    if (driver_name == 0 || driver_name[0] == '\0' || device == 0) {
        driver_manager_set_last_error(DRIVER_LOAD_BAD_HEADER, "bind", driver_name, "pci", 0, 0);
        return DRIVER_LOAD_BAD_HEADER;
    }

    const DriverRecord* driver = driver_manager_find(driver_name);
    if (driver == 0 || !driver->active) {
        driver_manager_set_last_error(DRIVER_LOAD_BIND_DENIED, "bind", driver_name, "not_found", 0, 0);
        return DRIVER_LOAD_BIND_DENIED;
    }
    if ((driver->permissions & DRV_PERMISSION_PCI) == 0) {
        driver_manager_set_last_error(DRIVER_LOAD_BIND_DENIED, "bind", driver_name, "permission", 0, driver->permissions);
        return DRIVER_LOAD_BIND_DENIED;
    }

    DriverBindingRecord* existing = find_pci_binding(device);
    if (existing != 0) {
        if (strcmp64(existing->driver, driver_name) == 0) {
            return DRIVER_LOAD_OK;
        }
        driver_manager_set_last_error(DRIVER_LOAD_BIND_DENIED, "bind", driver_name, existing->driver, 0, 0);
        return DRIVER_LOAD_BIND_DENIED;
    }

    DriverBindingRecord* binding = alloc_binding();
    if (binding == 0) {
        driver_manager_set_last_error(DRIVER_LOAD_NO_SLOT, "bind", driver_name, "slot", 0, 0);
        return DRIVER_LOAD_NO_SLOT;
    }

    clear_binding(binding);
    binding->active = 1;
    binding->kind = DRIVER_BIND_KIND_PCI;
    binding->flags = (uint16_t)flags;
    copy_string64(binding->driver, sizeof(binding->driver), driver_name);
    binding->vendor_id = device->vendor_id;
    binding->device_id = device->device_id;
    binding->bus = device->bus;
    binding->device = device->device;
    binding->function = device->function;
    binding->class_code = device->class_code;
    binding->subclass = device->subclass;
    binding->prog_if = device->prog_if;
    return DRIVER_LOAD_OK;
}

void driver_manager_unbind_module(const char* name) {
    if (name == 0) {
        return;
    }
    for (uint32_t i = 0; i < DRIVER_MAX_BINDINGS; i++) {
        if (g_bindings[i].active && strcmp64(g_bindings[i].driver, name) == 0) {
            clear_binding(&g_bindings[i]);
        }
    }
}

uint32_t driver_manager_binding_count() {
    uint32_t count = 0;
    for (uint32_t i = 0; i < DRIVER_MAX_BINDINGS; i++) {
        if (g_bindings[i].active) {
            count++;
        }
    }
    return count;
}

const DriverBindingRecord* driver_manager_binding_get(uint32_t index) {
    uint32_t seen = 0;
    for (uint32_t i = 0; i < DRIVER_MAX_BINDINGS; i++) {
        if (!g_bindings[i].active) {
            continue;
        }
        if (seen == index) {
            return &g_bindings[i];
        }
        seen++;
    }
    return 0;
}

int driver_manager_probe_loaded_driver(const char* name, DriverLoadedImage* loaded) {
    if (name == 0 || loaded == 0 || loaded->probe_pci == 0) {
        return DRIVER_LOAD_OK;
    }

    const DriverRecord* driver = driver_manager_find(name);
    if (driver == 0 || !driver->active || (driver->permissions & DRV_PERMISSION_PCI) == 0) {
        return DRIVER_LOAD_OK;
    }

    DriverPciProbeFn probe = (DriverPciProbeFn)loaded->probe_pci;
    uint32_t count = pci_get_device_count();
    for (uint32_t i = 0; i < count; i++) {
        const PCIDeviceInfo* device = pci_get_device(i);
        if (device == 0 || find_pci_binding(device) != 0) {
            continue;
        }
        driver_manager_set_lifecycle_driver(name);
        uint64_t matched = probe(device);
        driver_manager_set_lifecycle_driver(0);
        if (matched != 0) {
            int bind_result = driver_manager_bind_pci(name, device, 0);
            if (bind_result != DRIVER_LOAD_OK) {
                return bind_result;
            }
        }
    }

    return DRIVER_LOAD_OK;
}
