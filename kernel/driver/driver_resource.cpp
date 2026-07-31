#include "kernel/driver/driver_manager.h"
#include "kernel/kutil64.h"
#include "kernel/spinlock.h"

static DriverResourceRecord g_driver_resources[DRIVER_MAX_RESOURCES];
static DriverResourceStats g_driver_resource_stats;
static KernelSpinlock g_driver_resource_lock =
    KERNEL_SPINLOCK_INITIALIZER(KERNEL_LOCK_CLASS_VFS_DEVICE,
                                "driver_resource");

static uint32_t next_generation(uint32_t generation) {
    generation++;
    return generation != 0 ? generation : 1u;
}

static void clear_record(DriverResourceRecord* record) {
    if (record == 0) return;
    record->active = 0;
    record->kind = DRIVER_RESOURCE_NONE;
    record->flags = 0;
    record->owner = driver_identity_invalid();
    record->device = driver_device_identity_invalid();
    record->value = 0;
    record->size = 0;
    record->tag[0] = '\0';
}

void driver_resource_init() {
    kernel_spinlock_init(&g_driver_resource_lock,
                         KERNEL_LOCK_CLASS_VFS_DEVICE,
                         "driver_resource");
    for (uint32_t i = 0; i < DRIVER_MAX_RESOURCES; i++) {
        g_driver_resources[i].slot = i;
        g_driver_resources[i].generation = 0;
        clear_record(&g_driver_resources[i]);
    }
    g_driver_resource_stats = {};
}

DriverResourceHandle driver_resource_invalid() {
    DriverResourceHandle handle = {DRIVER_IDENTITY_INVALID_SLOT, 0};
    return handle;
}

int driver_resource_handle_is_valid(DriverResourceHandle handle) {
    return handle.slot < DRIVER_MAX_RESOURCES && handle.generation != 0;
}

static int resource_kind_valid(uint32_t kind) {
    return kind > DRIVER_RESOURCE_NONE && kind <= DRIVER_RESOURCE_DMA;
}

static void update_high_water_locked() {
    if (g_driver_resource_stats.active > g_driver_resource_stats.high_water) {
        g_driver_resource_stats.high_water = g_driver_resource_stats.active;
    }
}

int driver_resource_register(DriverIdentity owner,
                             DriverDeviceIdentity device,
                             uint32_t kind,
                             uint32_t flags,
                             uint64_t value,
                             uint64_t size,
                             const char* tag,
                             DriverResourceHandle* out) {
    if (out != 0) *out = driver_resource_invalid();
    if (!driver_identity_is_valid(owner) || !resource_kind_valid(kind) ||
        out == 0) {
        return DRIVER_LOAD_BAD_HEADER;
    }
    if (!driver_manager_identity_is_live(owner)) {
        __atomic_add_fetch(&g_driver_resource_stats.owner_rejections, 1u,
                           __ATOMIC_RELAXED);
        return DRIVER_LOAD_STALE_IDENTITY;
    }
    if (!driver_manager_identity_accepts_resources(owner)) {
        __atomic_add_fetch(&g_driver_resource_stats.state_rejections, 1u,
                           __ATOMIC_RELAXED);
        return DRIVER_LOAD_STATE_DENIED;
    }

    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&g_driver_resource_lock, &token)) {
        return DRIVER_LOAD_RESOURCE_DENIED;
    }
    DriverResourceRecord* record = 0;
    for (uint32_t i = 0; i < DRIVER_MAX_RESOURCES; i++) {
        if (!g_driver_resources[i].active) {
            record = &g_driver_resources[i];
            break;
        }
    }
    if (record == 0) {
        g_driver_resource_stats.exhaustion_failures++;
        kernel_spinlock_release(&g_driver_resource_lock, &token);
        return DRIVER_LOAD_NO_SLOT;
    }

    record->generation = next_generation(record->generation);
    record->active = 1;
    record->kind = (uint8_t)kind;
    record->flags = (uint16_t)flags;
    record->owner = owner;
    record->device = device;
    record->value = value;
    record->size = size;
    copy_string64(record->tag, sizeof(record->tag), tag != 0 ? tag : "");
    g_driver_resource_stats.active++;
    g_driver_resource_stats.by_kind[kind]++;
    g_driver_resource_stats.registrations++;
    update_high_water_locked();
    out->slot = record->slot;
    out->generation = record->generation;
    kernel_spinlock_release(&g_driver_resource_lock, &token);

    if (!driver_manager_identity_is_live(owner) ||
        !driver_manager_identity_accepts_resources(owner)) {
        driver_resource_release(owner, *out, kind);
        *out = driver_resource_invalid();
        return DRIVER_LOAD_STALE_IDENTITY;
    }
    return DRIVER_LOAD_OK;
}

static DriverResourceRecord* resolve_locked(DriverIdentity owner,
                                            DriverResourceHandle handle,
                                            uint32_t expected_kind) {
    if (!driver_resource_handle_is_valid(handle)) return 0;
    DriverResourceRecord* record = &g_driver_resources[handle.slot];
    if (!record->active || record->generation != handle.generation) return 0;
    if (!driver_identity_equal(record->owner, owner)) return 0;
    if (expected_kind != DRIVER_RESOURCE_NONE &&
        record->kind != expected_kind) return 0;
    return record;
}

int driver_resource_release(DriverIdentity owner,
                            DriverResourceHandle handle,
                            uint32_t expected_kind) {
    if (!driver_identity_is_valid(owner) ||
        !driver_resource_handle_is_valid(handle)) {
        return DRIVER_LOAD_BAD_HEADER;
    }
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&g_driver_resource_lock, &token)) {
        return DRIVER_LOAD_RESOURCE_DENIED;
    }
    DriverResourceRecord* record = resolve_locked(owner, handle, expected_kind);
    if (record == 0) {
        DriverResourceRecord* candidate =
            handle.slot < DRIVER_MAX_RESOURCES
                ? &g_driver_resources[handle.slot] : 0;
        if (candidate != 0 && candidate->active &&
            candidate->generation == handle.generation &&
            !driver_identity_equal(candidate->owner, owner)) {
            g_driver_resource_stats.owner_rejections++;
        } else {
            g_driver_resource_stats.stale_rejections++;
        }
        kernel_spinlock_release(&g_driver_resource_lock, &token);
        return DRIVER_LOAD_RESOURCE_DENIED;
    }
    const uint32_t kind = record->kind;
    clear_record(record);
    if (g_driver_resource_stats.active != 0) g_driver_resource_stats.active--;
    if (kind < 8 && g_driver_resource_stats.by_kind[kind] != 0) {
        g_driver_resource_stats.by_kind[kind]--;
    }
    g_driver_resource_stats.releases++;
    kernel_spinlock_release(&g_driver_resource_lock, &token);
    return DRIVER_LOAD_OK;
}

uint32_t driver_resource_release_owner(DriverIdentity owner) {
    if (!driver_identity_is_valid(owner)) return 0;
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&g_driver_resource_lock, &token)) return 0;
    uint32_t released = 0;
    for (uint32_t i = 0; i < DRIVER_MAX_RESOURCES; i++) {
        DriverResourceRecord* record = &g_driver_resources[i];
        if (!record->active || !driver_identity_equal(record->owner, owner)) {
            continue;
        }
        const uint32_t kind = record->kind;
        clear_record(record);
        if (g_driver_resource_stats.active != 0) g_driver_resource_stats.active--;
        if (kind < 8 && g_driver_resource_stats.by_kind[kind] != 0) {
            g_driver_resource_stats.by_kind[kind]--;
        }
        g_driver_resource_stats.releases++;
        released++;
    }
    kernel_spinlock_release(&g_driver_resource_lock, &token);
    return released;
}

const DriverResourceRecord* driver_resource_resolve(DriverIdentity owner,
                                                    DriverResourceHandle handle,
                                                    uint32_t expected_kind) {
    if (!driver_manager_identity_is_live(owner)) return 0;
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&g_driver_resource_lock, &token)) return 0;
    DriverResourceRecord* record = resolve_locked(owner, handle, expected_kind);
    kernel_spinlock_release(&g_driver_resource_lock, &token);
    return record;
}

void driver_resource_get_stats(DriverResourceStats* out) {
    if (out == 0) return;
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&g_driver_resource_lock, &token)) {
        *out = {};
        return;
    }
    *out = g_driver_resource_stats;
    kernel_spinlock_release(&g_driver_resource_lock, &token);
}
