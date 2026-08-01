#include "kernel/driver/driver_manager.h"
#include "kernel/driver/driver_alloc.h"
#include "kernel/driver/drv_format.h"
#include "kernel/kutil64.h"

static DriverIrqHookRecord g_irq_hooks[DRIVER_MAX_IRQ_HOOKS];

static void clear_irq_hook(DriverIrqHookRecord* hook) {
    if (hook == 0) {
        return;
    }
    hook->active = 0;
    hook->irq = 0;
    hook->flags = 0;
    hook->owner = driver_identity_invalid();
    hook->resource = driver_resource_invalid();
    hook->call_count = 0;
    hook->driver[0] = '\0';
    hook->handler = 0;
}

void driver_irq_init() {
    for (uint32_t i = 0; i < DRIVER_MAX_IRQ_HOOKS; i++) {
        clear_irq_hook(&g_irq_hooks[i]);
    }
}

int driver_irq_register_handler(const char* driver_name, uint32_t irq, DriverIrqHandler handler, uint32_t flags) {
    if (driver_name == 0 || driver_name[0] == '\0' || handler == 0 || irq > 15) {
        driver_manager_set_last_error(DRIVER_LOAD_BAD_HEADER, "irq", driver_name, "register", irq, (uint64_t)(uintptr_t)handler);
        return DRIVER_LOAD_BAD_HEADER;
    }

    const DriverRecord* driver = driver_manager_find(driver_name);
    if (driver == 0 || !driver->active || (driver->permissions & DRV_PERMISSION_INTERRUPT) == 0) {
        driver_manager_set_last_error(DRIVER_LOAD_IRQ_DENIED, "irq", driver_name, "permission", irq, 0);
        return DRIVER_LOAD_IRQ_DENIED;
    }
    DriverIdentity owner = {driver->slot, driver->generation};
    if (!driver_manager_identity_accepts_resources(owner)) {
        driver_manager_set_last_error(DRIVER_LOAD_STATE_DENIED, "irq",
                                      driver_name, "state", irq,
                                      driver->state);
        return DRIVER_LOAD_STATE_DENIED;
    }

    for (uint32_t i = 0; i < DRIVER_MAX_IRQ_HOOKS; i++) {
        DriverIrqHookRecord* hook = &g_irq_hooks[i];
        if (!hook->active) {
            continue;
        }
        if (hook->irq == irq && hook->handler == handler &&
            driver_identity_equal(hook->owner, owner)) {
            return DRIVER_LOAD_OK;
        }
    }

    for (uint32_t i = 0; i < DRIVER_MAX_IRQ_HOOKS; i++) {
        DriverIrqHookRecord* hook = &g_irq_hooks[i];
        if (hook->active) {
            continue;
        }
        clear_irq_hook(hook);
        hook->active = 1;
        hook->irq = (uint8_t)irq;
        hook->flags = (uint16_t)flags;
        hook->handler = handler;
        hook->owner = owner;
        copy_string64(hook->driver, sizeof(hook->driver), driver_name);
        int resource_result = driver_resource_register(owner,
                                                       driver_device_identity_invalid(),
                                                       DRIVER_RESOURCE_IRQ_HOOK,
                                                       flags,
                                                       irq,
                                                       0,
                                                       "irq_hook",
                                                       &hook->resource);
        if (resource_result != DRIVER_LOAD_OK) {
            clear_irq_hook(hook);
            return resource_result;
        }
        return DRIVER_LOAD_OK;
    }

    driver_manager_set_last_error(DRIVER_LOAD_NO_SLOT, "irq", driver_name, "slot", irq, 0);
    return DRIVER_LOAD_NO_SLOT;
}

int driver_irq_unregister_handler(const char* driver_name, uint32_t irq, DriverIrqHandler handler) {
    if (driver_name == 0 || handler == 0 || irq > 15) {
        return DRIVER_LOAD_BAD_HEADER;
    }

    for (uint32_t i = 0; i < DRIVER_MAX_IRQ_HOOKS; i++) {
        DriverIrqHookRecord* hook = &g_irq_hooks[i];
        if (!hook->active) {
            continue;
        }
        if (hook->irq == irq && hook->handler == handler && strcmp64(hook->driver, driver_name) == 0) {
            driver_resource_release(hook->owner,
                                    hook->resource,
                                    DRIVER_RESOURCE_IRQ_HOOK);
            clear_irq_hook(hook);
            return DRIVER_LOAD_OK;
        }
    }
    return DRIVER_LOAD_BAD_HEADER;
}

void driver_irq_unregister_module(const char* name) {
    if (name == 0) {
        return;
    }
    for (uint32_t i = 0; i < DRIVER_MAX_IRQ_HOOKS; i++) {
        if (g_irq_hooks[i].active && strcmp64(g_irq_hooks[i].driver, name) == 0) {
            driver_resource_release(g_irq_hooks[i].owner,
                                    g_irq_hooks[i].resource,
                                    DRIVER_RESOURCE_IRQ_HOOK);
            clear_irq_hook(&g_irq_hooks[i]);
        }
    }
}

void driver_irq_dispatch(uint32_t irq) {
    if (irq > 15) {
        return;
    }

    for (uint32_t i = 0; i < DRIVER_MAX_IRQ_HOOKS; i++) {
        DriverIrqHookRecord* hook = &g_irq_hooks[i];
        if (!hook->active || hook->irq != irq || hook->handler == 0) {
            continue;
        }
        DriverIdentity owner = hook->owner;
        DriverIrqHandler handler = hook->handler;
        if (!driver_manager_identity_is_live(owner)) {
            continue;
        }
        hook->call_count++;
        DriverExecutionToken context_token = {};
        if (driver_execution_enter(owner, DRIVER_CONTEXT_IRQ,
                                   &context_token) != DRIVER_LOAD_OK) {
            continue;
        }
        handler(irq);
        driver_execution_leave(&context_token);
    }
}

uint32_t driver_irq_hook_count() {
    uint32_t count = 0;
    for (uint32_t i = 0; i < DRIVER_MAX_IRQ_HOOKS; i++) {
        if (g_irq_hooks[i].active) {
            count++;
        }
    }
    return count;
}

const DriverIrqHookRecord* driver_irq_hook_get(uint32_t index) {
    uint32_t seen = 0;
    for (uint32_t i = 0; i < DRIVER_MAX_IRQ_HOOKS; i++) {
        if (!g_irq_hooks[i].active) {
            continue;
        }
        if (seen == index) {
            return &g_irq_hooks[i];
        }
        seen++;
    }
    return 0;
}
