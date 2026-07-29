#include "kernel/handle/kernel_objects.h"
#include "kernel/fault_injection.h"

#include <stddef.h>

extern "C" {
    #include "kernel/mm/heap.h"
}

#include "kernel/mm/vm.h"
#include "kernel/spinlock.h"
#include "kernel/sync/thread_sync.h"
#include "os64/graphics_types.h"

void kernel_sync_init() __attribute__((weak));
int kernel_sync_release_handle_object(const KernelHandle* handle)
    __attribute__((weak));

struct SharedMemoryObject {
    uint8_t active;
    uint8_t reserved0;
    uint16_t reserved1;
    uint32_t generation;
    uint32_t owner_pid;
    uint32_t ref_count;
    uint32_t size;
    uint32_t page_count;
    uint32_t rights;
    uint8_t* bytes;
};

struct GraphicsSurfaceObject {
    uint8_t active;
    uint8_t reserved0;
    uint16_t reserved1;
    uint32_t generation;
    uint32_t owner_pid;
    uint32_t ref_count;
    uint32_t byte_size;
    uint32_t page_count;
    uint32_t backing_slot;
    GraphicsSurface surface;
};

static SharedMemoryObject shared_objects[KERNEL_SHARED_MEMORY_MAX_OBJECTS];
static GraphicsSurfaceObject surface_objects[KERNEL_GRAPHICS_SURFACE_MAX_OBJECTS];
static KernelSpinlock object_lock =
    KERNEL_SPINLOCK_INITIALIZER(KERNEL_LOCK_CLASS_HANDLE, "kernel_objects");

static uint64_t make_object_id(uint32_t index, uint32_t generation) {
    return ((uint64_t)generation << 32) | (uint64_t)(index + 1u);
}

static int decode_object_id(uint64_t object_id, uint32_t max_index, uint32_t* index_out, uint32_t* generation_out) {
    if ((object_id & 0xFFFFFFFFULL) == 0) {
        return 0;
    }

    uint32_t index = (uint32_t)(object_id & 0xFFFFFFFFULL) - 1u;
    uint32_t generation = (uint32_t)(object_id >> 32);
    if (index >= max_index || generation == 0) {
        return 0;
    }

    *index_out = index;
    *generation_out = generation;
    return 1;
}

static uint32_t next_generation(uint32_t generation) {
    generation++;
    return generation == 0 ? 1 : generation;
}

static void zero_bytes(uint8_t* bytes, uint32_t size) {
    if (bytes == 0) {
        return;
    }
    for (uint32_t i = 0; i < size; i++) {
        bytes[i] = 0;
    }
}

static SharedMemoryObject* shared_from_id(uint64_t object_id) {
    uint32_t index = 0;
    uint32_t generation = 0;
    if (!decode_object_id(object_id, KERNEL_SHARED_MEMORY_MAX_OBJECTS, &index, &generation)) {
        return 0;
    }

    SharedMemoryObject* object = &shared_objects[index];
    if (!object->active || object->generation != generation) {
        return 0;
    }
    return object;
}

static GraphicsSurfaceObject* surface_from_id(uint64_t object_id) {
    uint32_t index = 0;
    uint32_t generation = 0;
    if (!decode_object_id(object_id, KERNEL_GRAPHICS_SURFACE_MAX_OBJECTS, &index, &generation)) {
        return 0;
    }

    GraphicsSurfaceObject* object = &surface_objects[index];
    if (!object->active || object->generation != generation) {
        return 0;
    }
    return object;
}

static uint8_t* release_shared_locked(SharedMemoryObject* object) {
    if (object == 0 || !object->active || object->ref_count == 0) {
        return 0;
    }
    object->ref_count--;
    if (object->ref_count != 0) {
        return 0;
    }

    uint8_t* bytes = object->bytes;
    object->active = 0;
    object->owner_pid = 0;
    object->size = 0;
    object->page_count = 0;
    object->rights = 0;
    object->bytes = 0;
    return bytes;
}

static int release_surface_locked(GraphicsSurfaceObject* object,
                                  uint32_t* backing_slot) {
    if (object == 0 || !object->active || object->ref_count == 0) {
        return 0;
    }
    object->ref_count--;
    if (object->ref_count != 0) {
        return 0;
    }

    *backing_slot = object->backing_slot;
    object->active = 0;
    object->owner_pid = 0;
    object->byte_size = 0;
    object->page_count = 0;
    object->backing_slot = 0;
    gfx_surface_init(&object->surface, 0, 0, 0, 0, OS64_PIXEL_FORMAT_RGB, 0);
    return 1;
}

static int is_refcounted_object_type(uint32_t type) {
    return type == KERNEL_HANDLE_TYPE_SHARED_MEMORY ||
           type == KERNEL_HANDLE_TYPE_GRAPHICS_SURFACE;
}

void kernel_objects_init() {
    kernel_spinlock_init(&object_lock,
                         KERNEL_LOCK_CLASS_HANDLE,
                         "kernel_objects");
    for (uint32_t i = 0; i < KERNEL_SHARED_MEMORY_MAX_OBJECTS; i++) {
        if (shared_objects[i].bytes != 0) {
            kfree(shared_objects[i].bytes);
        }
        shared_objects[i].active = 0;
        shared_objects[i].owner_pid = 0;
        shared_objects[i].ref_count = 0;
        shared_objects[i].size = 0;
        shared_objects[i].page_count = 0;
        shared_objects[i].rights = 0;
        shared_objects[i].bytes = 0;
        shared_objects[i].generation = 0;
    }
    for (uint32_t i = 0; i < KERNEL_GRAPHICS_SURFACE_MAX_OBJECTS; i++) {
        if (surface_objects[i].active) {
            kernel_graphics_surface_backing_release(
                surface_objects[i].backing_slot);
        }
        surface_objects[i].active = 0;
        surface_objects[i].owner_pid = 0;
        surface_objects[i].ref_count = 0;
        surface_objects[i].byte_size = 0;
        surface_objects[i].page_count = 0;
        surface_objects[i].backing_slot = 0;
        gfx_surface_init(&surface_objects[i].surface, 0, 0, 0, 0, OS64_PIXEL_FORMAT_RGB, 0);
        surface_objects[i].generation = 0;
    }
    kernel_graphics_surface_backing_init();
    if (kernel_sync_init != 0) {
        kernel_sync_init();
    }
}

void kernel_object_get_stats(KernelObjectStats* stats) {
    if (stats == 0) {
        return;
    }
    stats->active_shared_memory = 0;
    stats->active_surfaces = 0;
    stats->surface_pages = 0;
    stats->reserved = 0;
    stats->shared_memory_bytes = 0;
    stats->surface_bytes = 0;
    stats->surface_backing_bytes = 0;
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&object_lock, &token)) {
        return;
    }
    for (uint32_t i = 0; i < KERNEL_SHARED_MEMORY_MAX_OBJECTS; i++) {
        if (shared_objects[i].active) {
            stats->active_shared_memory++;
            stats->shared_memory_bytes += shared_objects[i].page_count * VM_PAGE_SIZE;
        }
    }
    for (uint32_t i = 0; i < KERNEL_GRAPHICS_SURFACE_MAX_OBJECTS; i++) {
        if (surface_objects[i].active) {
            stats->active_surfaces++;
            stats->surface_pages += surface_objects[i].page_count;
            stats->surface_bytes += surface_objects[i].byte_size;
            stats->surface_backing_bytes +=
                (uint64_t)surface_objects[i].page_count * VM_PAGE_SIZE;
        }
    }
    kernel_spinlock_release(&object_lock, &token);
}

uint64_t kernel_shared_memory_create(KernelHandleTable* table,
                                     uint32_t owner_pid,
                                     uint32_t size,
                                     uint32_t rights) {
    if (table == 0 || size == 0) {
        return 0;
    }

    uint32_t page_count = (size + VM_PAGE_SIZE - 1u) / VM_PAGE_SIZE;
    if (page_count == 0 || page_count > KERNEL_SHARED_MEMORY_MAX_PAGES) {
        return 0;
    }
    if (kernel_fault_injection_should_fail(KERNEL_FAULT_POINT_SHARED_MEMORY)) {
        return 0;
    }

    uint32_t byte_size = page_count * (uint32_t)VM_PAGE_SIZE;
    uint8_t* bytes = (uint8_t*)kmalloc(byte_size);
    if (bytes == 0) {
        return 0;
    }
    zero_bytes(bytes, byte_size);

    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&object_lock, &token)) {
        kfree(bytes);
        return 0;
    }
    for (uint32_t i = 0; i < KERNEL_SHARED_MEMORY_MAX_OBJECTS; i++) {
        SharedMemoryObject* object = &shared_objects[i];
        if (object->active) {
            continue;
        }

        object->active = 1;
        object->generation = next_generation(object->generation);
        object->owner_pid = owner_pid;
        object->ref_count = 1;
        object->size = size;
        object->page_count = page_count;
        object->rights = rights;
        object->bytes = bytes;

        uint64_t object_id = make_object_id(i, object->generation);
        kernel_spinlock_release(&object_lock, &token);
        uint64_t handle = kernel_handle_alloc(table,
                                              KERNEL_HANDLE_TYPE_SHARED_MEMORY,
                                              rights,
                                              object_id,
                                              byte_size);
        if (handle == 0) {
            KernelHandle failed = {};
            failed.active = 1;
            failed.type = KERNEL_HANDLE_TYPE_SHARED_MEMORY;
            failed.object = object_id;
            kernel_object_release_handle_object(&failed);
            return 0;
        }
        return handle;
    }

    kernel_spinlock_release(&object_lock, &token);
    kfree(bytes);
    return 0;
}

int kernel_shared_memory_get_info(uint64_t object_id, KernelSharedMemoryInfo* info) {
    if (info == 0) {
        return KERNEL_OBJECT_ERR_NOT_FOUND;
    }
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&object_lock, &token)) {
        return KERNEL_OBJECT_ERR_NOT_FOUND;
    }
    SharedMemoryObject* object = shared_from_id(object_id);
    if (object == 0) {
        kernel_spinlock_release(&object_lock, &token);
        return KERNEL_OBJECT_ERR_NOT_FOUND;
    }

    info->owner_pid = object->owner_pid;
    info->generation = object->generation;
    info->size = object->size;
    info->page_count = object->page_count;
    info->rights = object->rights;
    info->ref_count = object->ref_count;
    kernel_spinlock_release(&object_lock, &token);
    return KERNEL_OBJECT_OK;
}

int kernel_shared_memory_read(uint64_t object_id, uint32_t offset, uint8_t* buffer, uint32_t size) {
    if (buffer == 0) {
        return KERNEL_OBJECT_ERR_INVALID;
    }
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&object_lock, &token)) {
        return KERNEL_OBJECT_ERR_INVALID;
    }
    SharedMemoryObject* object = shared_from_id(object_id);
    if (object == 0 || offset > object->size || size > object->size - offset) {
        kernel_spinlock_release(&object_lock, &token);
        return KERNEL_OBJECT_ERR_INVALID;
    }
    for (uint32_t i = 0; i < size; i++) {
        buffer[i] = object->bytes[offset + i];
    }
    kernel_spinlock_release(&object_lock, &token);
    return KERNEL_OBJECT_OK;
}

int kernel_shared_memory_write(uint64_t object_id, uint32_t offset, const uint8_t* buffer, uint32_t size) {
    if (buffer == 0) {
        return KERNEL_OBJECT_ERR_INVALID;
    }
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&object_lock, &token)) {
        return KERNEL_OBJECT_ERR_INVALID;
    }
    SharedMemoryObject* object = shared_from_id(object_id);
    if (object == 0 || offset > object->size || size > object->size - offset) {
        kernel_spinlock_release(&object_lock, &token);
        return KERNEL_OBJECT_ERR_INVALID;
    }
    for (uint32_t i = 0; i < size; i++) {
        object->bytes[offset + i] = buffer[i];
    }
    kernel_spinlock_release(&object_lock, &token);
    return KERNEL_OBJECT_OK;
}

uint64_t kernel_graphics_surface_create(KernelHandleTable* table,
                                        uint32_t owner_pid,
                                        uint32_t width,
                                        uint32_t height,
                                        uint32_t pixel_format,
                                        uint32_t rights) {
    if (table == 0 || width == 0 || height == 0 ||
        (pixel_format != OS64_PIXEL_FORMAT_RGB && pixel_format != OS64_PIXEL_FORMAT_BGR) ||
        width > KERNEL_GRAPHICS_SURFACE_MAX_PIXELS / height) {
        return 0;
    }

    uint32_t pixel_count = width * height;
    if (pixel_count > KERNEL_GRAPHICS_SURFACE_MAX_PIXELS) {
        return 0;
    }

    uint32_t byte_size = pixel_count * sizeof(uint32_t);
    uint32_t reserved_index = UINT32_MAX;
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&object_lock, &token)) {
        return 0;
    }
    for (uint32_t i = 0; i < KERNEL_GRAPHICS_SURFACE_MAX_OBJECTS; i++) {
        GraphicsSurfaceObject* object = &surface_objects[i];
        if (object->active) {
            continue;
        }
        object->active = 2;
        reserved_index = i;
        break;
    }
    kernel_spinlock_release(&object_lock, &token);
    if (reserved_index == UINT32_MAX) {
        return 0;
    }

    GraphicsSurfaceObject* object = &surface_objects[reserved_index];
    uint32_t* pixels = 0;
    uint32_t page_count = 0;
    if (!kernel_graphics_surface_backing_allocate(reserved_index,
                                                      byte_size,
                                                      &pixels,
                                                      &page_count)) {
        kernel_graphics_surface_backing_release(reserved_index);
        if (kernel_spinlock_acquire(&object_lock, &token)) {
            object->active = 0;
            kernel_spinlock_release(&object_lock, &token);
        }
        return 0;
    }

    GraphicsSurface surface = {};
    if (!gfx_surface_init(&surface,
                          pixels,
                          width,
                          height,
                          width,
                          pixel_format,
                          GFX_SURFACE_FLAG_PAGE_BACKED)) {
        kernel_graphics_surface_backing_release(reserved_index);
        if (kernel_spinlock_acquire(&object_lock, &token)) {
            object->active = 0;
            kernel_spinlock_release(&object_lock, &token);
        }
        return 0;
    }
    if (!kernel_spinlock_acquire(&object_lock, &token)) {
        kernel_graphics_surface_backing_release(reserved_index);
        object->active = 0;
        return 0;
    }
    object->surface = surface;
    object->generation = next_generation(object->generation);
    object->owner_pid = owner_pid;
    object->ref_count = 1;
    object->byte_size = byte_size;
    object->page_count = page_count;
    object->backing_slot = reserved_index;
    object->active = 1;
    uint64_t object_id = make_object_id(reserved_index, object->generation);
    kernel_spinlock_release(&object_lock, &token);

    uint64_t handle = kernel_handle_alloc(table,
                                              KERNEL_HANDLE_TYPE_GRAPHICS_SURFACE,
                                              rights,
                                              object_id,
                                              byte_size);
    if (handle == 0) {
        KernelHandle failed = {};
        failed.active = 1;
        failed.type = KERNEL_HANDLE_TYPE_GRAPHICS_SURFACE;
        failed.object = object_id;
        kernel_object_release_handle_object(&failed);
        return 0;
    }
    return handle;
}

int kernel_graphics_surface_get_info(uint64_t object_id, KernelGraphicsSurfaceInfo* info) {
    if (info == 0) {
        return KERNEL_OBJECT_ERR_NOT_FOUND;
    }
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&object_lock, &token)) {
        return KERNEL_OBJECT_ERR_NOT_FOUND;
    }
    GraphicsSurfaceObject* object = surface_from_id(object_id);
    if (object == 0) {
        kernel_spinlock_release(&object_lock, &token);
        return KERNEL_OBJECT_ERR_NOT_FOUND;
    }

    info->owner_pid = object->owner_pid;
    info->generation = object->generation;
    info->width = object->surface.width;
    info->height = object->surface.height;
    info->stride_pixels = object->surface.stride_pixels;
    info->pixel_format = object->surface.pixel_format;
    info->byte_size = object->byte_size;
    info->ref_count = object->ref_count;
    kernel_spinlock_release(&object_lock, &token);
    return KERNEL_OBJECT_OK;
}

GraphicsSurface* kernel_graphics_surface_get(uint64_t object_id) {
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&object_lock, &token)) {
        return 0;
    }
    GraphicsSurfaceObject* object = surface_from_id(object_id);
    GraphicsSurface* surface = object != 0 ? &object->surface : 0;
    kernel_spinlock_release(&object_lock, &token);
    return surface;
}

int kernel_graphics_surface_get_backing(uint64_t object_id,
                                        uint32_t* backing_slot,
                                        uint32_t* page_count,
                                        uint32_t* byte_size) {
    if (backing_slot == 0 || page_count == 0 || byte_size == 0) {
        return KERNEL_OBJECT_ERR_INVALID;
    }
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&object_lock, &token)) {
        return KERNEL_OBJECT_ERR_INVALID;
    }
    GraphicsSurfaceObject* object = surface_from_id(object_id);
    if (object == 0) {
        kernel_spinlock_release(&object_lock, &token);
        return KERNEL_OBJECT_ERR_INVALID;
    }
    *backing_slot = object->backing_slot;
    *page_count = object->page_count;
    *byte_size = object->byte_size;
    kernel_spinlock_release(&object_lock, &token);
    return KERNEL_OBJECT_OK;
}

int kernel_object_retain_handle_object(const KernelHandle* handle) {
    if (handle == 0 || !handle->active) {
        return 0;
    }
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&object_lock, &token)) {
        return 0;
    }
    if (handle->type == KERNEL_HANDLE_TYPE_SHARED_MEMORY) {
        SharedMemoryObject* object = shared_from_id(handle->object);
        if (object == 0) {
            kernel_spinlock_release(&object_lock, &token);
            return 0;
        }
        object->ref_count++;
        kernel_spinlock_release(&object_lock, &token);
        return 1;
    }
    if (handle->type == KERNEL_HANDLE_TYPE_GRAPHICS_SURFACE) {
        GraphicsSurfaceObject* object = surface_from_id(handle->object);
        if (object == 0) {
            kernel_spinlock_release(&object_lock, &token);
            return 0;
        }
        object->ref_count++;
        kernel_spinlock_release(&object_lock, &token);
        return 1;
    }
    kernel_spinlock_release(&object_lock, &token);
    return 0;
}

void kernel_object_release_handle_object(const KernelHandle* handle) {
    if (handle == 0) {
        return;
    }
    if (handle->type == KERNEL_HANDLE_TYPE_SHARED_MEMORY) {
        uint8_t* bytes = 0;
        KernelSpinlockToken token;
        if (kernel_spinlock_acquire(&object_lock, &token)) {
            bytes = release_shared_locked(shared_from_id(handle->object));
            kernel_spinlock_release(&object_lock, &token);
        }
        if (bytes != 0) {
            kfree(bytes);
        }
    } else if (handle->type == KERNEL_HANDLE_TYPE_GRAPHICS_SURFACE) {
        uint32_t backing_slot = 0;
        int release_backing = 0;
        KernelSpinlockToken token;
        if (kernel_spinlock_acquire(&object_lock, &token)) {
            release_backing =
                release_surface_locked(surface_from_id(handle->object),
                                       &backing_slot);
            kernel_spinlock_release(&object_lock, &token);
        }
        if (release_backing) {
            kernel_graphics_surface_backing_release(backing_slot);
        }
    } else {
        if (kernel_sync_release_handle_object != 0) {
            kernel_sync_release_handle_object(handle);
        }
    }
}

uint64_t kernel_object_clone_handle(KernelHandleTable* target_table, const KernelHandle* source) {
    return kernel_object_clone_handle_with_rights(target_table,
                                                  source,
                                                  source != 0 ? source->rights : 0);
}

uint64_t kernel_object_clone_handle_with_rights(KernelHandleTable* target_table,
                                                const KernelHandle* source,
                                                uint32_t rights) {
    if (target_table == 0 || source == 0 || !source->active) {
        return 0;
    }
    if (rights == 0 || (rights & ~source->rights) != 0) {
        return 0;
    }
    if (!is_refcounted_object_type(source->type)) {
        return 0;
    }
    if (!kernel_object_retain_handle_object(source)) {
        return 0;
    }

    uint64_t cloned = kernel_handle_alloc(target_table,
                                          source->type,
                                          rights,
                                          source->object,
                                          source->extra);
    if (cloned == 0) {
        kernel_object_release_handle_object(source);
    }
    return cloned;
}

int kernel_object_close_handle(KernelHandleTable* table, uint64_t handle, KernelHandle* closed_out) {
    KernelHandle closed;
    if (!kernel_handle_close(table, handle, &closed)) {
        return 0;
    }
    kernel_object_release_handle_object(&closed);
    if (closed_out != 0) {
        *closed_out = closed;
    }
    return 1;
}

uint32_t kernel_object_release_table(KernelHandleTable* table) {
    if (table == 0) {
        return 0;
    }

    KernelHandle detached[KERNEL_HANDLE_TABLE_SIZE];
    uint32_t released = kernel_handle_detach_all(table,
                                                 detached,
                                                 KERNEL_HANDLE_TABLE_SIZE);
    for (uint32_t i = 0; i < released; i++) {
        kernel_object_release_handle_object(&detached[i]);
    }
    return released;
}
