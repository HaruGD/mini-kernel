#include "kernel/driver/driver_manager.h"
#include "kernel/driver/driver_alloc.h"
#include "kernel/driver/drv_format.h"
#include "kernel/kutil64.h"
#include "kernel/spinlock.h"
#ifndef OS64_HOST_TEST
#include "arch/x86_64/apic.h"
#endif

static DriverIrqHookRecord g_irq_hooks[DRIVER_MAX_IRQ_HOOKS];
static uint8_t g_irq_routed[16];
static KernelSpinlock g_irq_lock =
    KERNEL_SPINLOCK_INITIALIZER(KERNEL_LOCK_CLASS_VFS_DEVICE, "driver_irq");

static int activate_irq_line_locked(uint32_t irq) {
#ifdef OS64_HOST_TEST
    (void)irq;
    return 1;
#else
    if (!g_irq_routed[irq]) {
        if (!interrupt_controller_route_external_irq((uint8_t)irq,
                                                      (uint8_t)(32u + irq)))
            return 0;
        g_irq_routed[irq] = 1;
    }
    interrupt_controller_set_mask((uint8_t)irq, 0);
    return 1;
#endif
}

static void deactivate_irq_line_if_unused(uint32_t irq) {
#ifndef OS64_HOST_TEST
    /* PCI INTx redirection remains stable across driver generations. The
     * device shutdown path must deassert its source before hook removal;
     * masking the shared line here loses the next level assertion in QEMU and
     * can also hide another function sharing the link on hardware. */
    (void)irq;
#else
    (void)irq;
#endif
}

static void clear_irq_hook(DriverIrqHookRecord* hook) {
    if (hook == 0) return;
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
    kernel_spinlock_init(&g_irq_lock, KERNEL_LOCK_CLASS_VFS_DEVICE,
                         "driver_irq");
    for (uint32_t irq = 0; irq < 16; irq++) g_irq_routed[irq] = 0;
    for (uint32_t i = 0; i < DRIVER_MAX_IRQ_HOOKS; i++)
        clear_irq_hook(&g_irq_hooks[i]);
}

int driver_irq_register_handler(const char* driver_name, uint32_t irq,
                                DriverIrqHandler handler, uint32_t flags) {
    if (driver_name == 0 || driver_name[0] == '\0' || handler == 0 ||
        irq > 15) {
        driver_manager_set_last_error(DRIVER_LOAD_BAD_HEADER, "irq",
                                      driver_name, "register", irq,
                                      (uint64_t)(uintptr_t)handler);
        return DRIVER_LOAD_BAD_HEADER;
    }

    const DriverRecord* driver = driver_manager_find(driver_name);
    if (driver == 0 || !driver->active ||
        (driver->permissions & DRV_PERMISSION_INTERRUPT) == 0) {
        driver_manager_set_last_error(DRIVER_LOAD_IRQ_DENIED, "irq",
                                      driver_name, "permission", irq, 0);
        return DRIVER_LOAD_IRQ_DENIED;
    }
    DriverIdentity owner = {driver->slot, driver->generation};
    if (!driver_manager_identity_accepts_resources(owner)) {
        driver_manager_set_last_error(DRIVER_LOAD_STATE_DENIED, "irq",
                                      driver_name, "state", irq,
                                      driver->state);
        return DRIVER_LOAD_STATE_DENIED;
    }

    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&g_irq_lock, &token))
        return DRIVER_LOAD_IRQ_DENIED;
    for (uint32_t i = 0; i < DRIVER_MAX_IRQ_HOOKS; i++) {
        DriverIrqHookRecord* hook = &g_irq_hooks[i];
        if (hook->active == 1 && hook->irq == irq &&
            hook->handler == handler && driver_identity_equal(hook->owner,
                                                               owner)) {
            kernel_spinlock_release(&g_irq_lock, &token);
            return DRIVER_LOAD_OK;
        }
    }

    DriverIrqHookRecord* reserved = 0;
    for (uint32_t i = 0; i < DRIVER_MAX_IRQ_HOOKS; i++) {
        if (g_irq_hooks[i].active != 0) continue;
        clear_irq_hook(&g_irq_hooks[i]);
        g_irq_hooks[i].active = 2; /* Reserved; dispatch accepts only 1. */
        reserved = &g_irq_hooks[i];
        break;
    }
    kernel_spinlock_release(&g_irq_lock, &token);
    if (reserved == 0) {
        driver_manager_set_last_error(DRIVER_LOAD_NO_SLOT, "irq", driver_name,
                                      "slot", irq, 0);
        return DRIVER_LOAD_NO_SLOT;
    }

    DriverResourceHandle resource = driver_resource_invalid();
    int result = driver_resource_register(
        owner, driver_device_identity_invalid(), DRIVER_RESOURCE_IRQ_HOOK,
        flags, irq, 0, "irq_hook", &resource);

    if (!kernel_spinlock_acquire(&g_irq_lock, &token)) {
        if (result == DRIVER_LOAD_OK)
            driver_resource_release(owner, resource, DRIVER_RESOURCE_IRQ_HOOK);
        return DRIVER_LOAD_IRQ_DENIED;
    }
    if (result == DRIVER_LOAD_OK &&
        !driver_manager_identity_accepts_resources(owner))
        result = DRIVER_LOAD_STATE_DENIED;
    if (result == DRIVER_LOAD_OK && reserved->active == 2) {
        reserved->irq = (uint8_t)irq;
        reserved->flags = (uint16_t)flags;
        reserved->handler = handler;
        reserved->owner = owner;
        reserved->resource = resource;
        copy_string64(reserved->driver, sizeof(reserved->driver), driver_name);
        if (activate_irq_line_locked(irq)) {
            __atomic_store_n(&reserved->active, 1u, __ATOMIC_RELEASE);
            kernel_spinlock_release(&g_irq_lock, &token);
            return DRIVER_LOAD_OK;
        }
        result = DRIVER_LOAD_IRQ_DENIED;
    }
    clear_irq_hook(reserved);
    kernel_spinlock_release(&g_irq_lock, &token);
    if (resource.generation != 0)
        driver_resource_release(owner, resource, DRIVER_RESOURCE_IRQ_HOOK);
    return result;
}

int driver_irq_unregister_handler(const char* driver_name, uint32_t irq,
                                  DriverIrqHandler handler) {
    if (driver_name == 0 || handler == 0 || irq > 15)
        return DRIVER_LOAD_BAD_HEADER;

    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&g_irq_lock, &token))
        return DRIVER_LOAD_IRQ_DENIED;
    DriverIdentity owner = driver_identity_invalid();
    DriverResourceHandle resource = driver_resource_invalid();
    int found = 0;
    for (uint32_t i = 0; i < DRIVER_MAX_IRQ_HOOKS; i++) {
        DriverIrqHookRecord* hook = &g_irq_hooks[i];
        if (hook->active != 1) continue;
        if (hook->irq == irq && hook->handler == handler &&
            strcmp64(hook->driver, driver_name) == 0) {
            owner = hook->owner;
            resource = hook->resource;
            clear_irq_hook(hook);
            found = 1;
            break;
        }
    }
    kernel_spinlock_release(&g_irq_lock, &token);
    if (!found) return DRIVER_LOAD_BAD_HEADER;
    driver_resource_release(owner, resource, DRIVER_RESOURCE_IRQ_HOOK);
    deactivate_irq_line_if_unused(irq);
    return DRIVER_LOAD_OK;
}

void driver_irq_unregister_module(const char* name) {
    if (name == 0) return;
    DriverIdentity owners[DRIVER_MAX_IRQ_HOOKS];
    DriverResourceHandle resources[DRIVER_MAX_IRQ_HOOKS];
    uint32_t release_count = 0;
    uint16_t touched = 0;
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&g_irq_lock, &token)) return;
    for (uint32_t i = 0; i < DRIVER_MAX_IRQ_HOOKS; i++) {
        DriverIrqHookRecord* hook = &g_irq_hooks[i];
        if (hook->active != 1 || strcmp64(hook->driver, name) != 0) continue;
        touched |= (uint16_t)(1u << hook->irq);
        owners[release_count] = hook->owner;
        resources[release_count] = hook->resource;
        release_count++;
        clear_irq_hook(hook);
    }
    kernel_spinlock_release(&g_irq_lock, &token);
    for (uint32_t i = 0; i < release_count; i++)
        driver_resource_release(owners[i], resources[i],
                                DRIVER_RESOURCE_IRQ_HOOK);
    for (uint32_t irq = 2; irq < 16; irq++)
        if ((touched & (uint16_t)(1u << irq)) != 0)
            deactivate_irq_line_if_unused(irq);
}

void driver_irq_dispatch(uint32_t irq) {
    if (irq > 15) return;
    struct AdmittedIrq {
        DriverIrqHandler handler;
        DriverExecutionToken token;
    } admitted[DRIVER_MAX_IRQ_HOOKS];
    uint32_t admitted_count = 0;

    KernelSpinlockToken lock_token;
    if (!kernel_spinlock_acquire(&g_irq_lock, &lock_token)) return;
    for (uint32_t i = 0; i < DRIVER_MAX_IRQ_HOOKS; i++) {
        DriverIrqHookRecord* hook = &g_irq_hooks[i];
        if (hook->active != 1 || hook->irq != irq || hook->handler == 0)
            continue;
        DriverExecutionToken context_token = {};
        if (!driver_manager_identity_is_live(hook->owner) ||
            driver_execution_enter(hook->owner, DRIVER_CONTEXT_IRQ,
                                   &context_token) != DRIVER_LOAD_OK)
            continue;
        hook->call_count++;
        admitted[admitted_count].handler = hook->handler;
        admitted[admitted_count].token = context_token;
        admitted_count++;
    }
    kernel_spinlock_release(&g_irq_lock, &lock_token);

    for (uint32_t i = 0; i < admitted_count; i++) {
        admitted[i].handler(irq);
        driver_execution_leave(&admitted[i].token);
    }
}

uint32_t driver_irq_hook_count() {
    uint32_t count = 0;
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&g_irq_lock, &token)) return 0;
    for (uint32_t i = 0; i < DRIVER_MAX_IRQ_HOOKS; i++)
        if (g_irq_hooks[i].active == 1) count++;
    kernel_spinlock_release(&g_irq_lock, &token);
    return count;
}

int driver_irq_hook_get(uint32_t index, DriverIrqHookRecord* out) {
    if (out == 0) return 0;
    *out = {};
    uint32_t seen = 0;
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&g_irq_lock, &token)) return 0;
    for (uint32_t i = 0; i < DRIVER_MAX_IRQ_HOOKS; i++) {
        if (g_irq_hooks[i].active != 1) continue;
        if (seen == index) {
            *out = g_irq_hooks[i];
            kernel_spinlock_release(&g_irq_lock, &token);
            return 1;
        }
        seen++;
    }
    kernel_spinlock_release(&g_irq_lock, &token);
    return 0;
}
