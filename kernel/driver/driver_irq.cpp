#include "kernel/driver/driver_manager.h"
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
    hook->call_count = 0;
    hook->driver[0] = '\0';
    hook->handler = 0;
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

    for (uint32_t i = 0; i < DRIVER_MAX_IRQ_HOOKS; i++) {
        DriverIrqHookRecord* hook = &g_irq_hooks[i];
        if (!hook->active) {
            continue;
        }
        if (hook->irq == irq && hook->handler == handler && strcmp64(hook->driver, driver_name) == 0) {
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
        copy_string64(hook->driver, sizeof(hook->driver), driver_name);
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
        hook->call_count++;
        hook->handler(irq);
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
