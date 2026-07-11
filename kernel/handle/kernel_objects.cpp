#include "kernel/handle/kernel_objects.h"

#include <stddef.h>

extern "C" {
    #include "kernel/mm/heap.h"
}

#include "kernel/mm/vm.h"
#include "os64/graphics_types.h"

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
    GraphicsSurface surface;
};

static SharedMemoryObject shared_objects[KERNEL_SHARED_MEMORY_MAX_OBJECTS];
static GraphicsSurfaceObject surface_objects[KERNEL_GRAPHICS_SURFACE_MAX_OBJECTS];

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

static void release_shared(SharedMemoryObject* object) {
    if (object == 0 || !object->active || object->ref_count == 0) {
        return;
    }
    object->ref_count--;
    if (object->ref_count != 0) {
        return;
    }

    if (object->bytes != 0) {
        kfree(object->bytes);
    }
    object->active = 0;
    object->owner_pid = 0;
    object->size = 0;
    object->page_count = 0;
    object->rights = 0;
    object->bytes = 0;
}

static void release_surface(GraphicsSurfaceObject* object) {
    if (object == 0 || !object->active || object->ref_count == 0) {
        return;
    }
    object->ref_count--;
    if (object->ref_count != 0) {
        return;
    }

    if ((object->surface.flags & GFX_SURFACE_FLAG_OWNS_PIXELS) != 0 &&
        object->surface.pixels != 0) {
        kfree(object->surface.pixels);
    }
    object->active = 0;
    object->owner_pid = 0;
    object->byte_size = 0;
    gfx_surface_init(&object->surface, 0, 0, 0, 0, OS64_PIXEL_FORMAT_RGB, 0);
}

static int is_refcounted_object_type(uint32_t type) {
    return type == KERNEL_HANDLE_TYPE_SHARED_MEMORY ||
           type == KERNEL_HANDLE_TYPE_GRAPHICS_SURFACE;
}

void kernel_objects_init() {
    for (uint32_t i = 0; i < KERNEL_SHARED_MEMORY_MAX_OBJECTS; i++) {
        while (shared_objects[i].active && shared_objects[i].ref_count != 0) {
            release_shared(&shared_objects[i]);
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
        while (surface_objects[i].active && surface_objects[i].ref_count != 0) {
            release_surface(&surface_objects[i]);
        }
        surface_objects[i].active = 0;
        surface_objects[i].owner_pid = 0;
        surface_objects[i].ref_count = 0;
        surface_objects[i].byte_size = 0;
        gfx_surface_init(&surface_objects[i].surface, 0, 0, 0, 0, OS64_PIXEL_FORMAT_RGB, 0);
        surface_objects[i].generation = 0;
    }
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

    uint32_t byte_size = page_count * (uint32_t)VM_PAGE_SIZE;
    for (uint32_t i = 0; i < KERNEL_SHARED_MEMORY_MAX_OBJECTS; i++) {
        SharedMemoryObject* object = &shared_objects[i];
        if (object->active) {
            continue;
        }

        uint8_t* bytes = (uint8_t*)kmalloc(byte_size);
        if (bytes == 0) {
            return 0;
        }
        zero_bytes(bytes, byte_size);

        object->active = 1;
        object->generation = next_generation(object->generation);
        object->owner_pid = owner_pid;
        object->ref_count = 1;
        object->size = size;
        object->page_count = page_count;
        object->rights = rights;
        object->bytes = bytes;

        uint64_t object_id = make_object_id(i, object->generation);
        uint64_t handle = kernel_handle_alloc(table,
                                              KERNEL_HANDLE_TYPE_SHARED_MEMORY,
                                              rights,
                                              object_id,
                                              byte_size);
        if (handle == 0) {
            release_shared(object);
            return 0;
        }
        return handle;
    }

    return 0;
}

int kernel_shared_memory_get_info(uint64_t object_id, KernelSharedMemoryInfo* info) {
    SharedMemoryObject* object = shared_from_id(object_id);
    if (object == 0 || info == 0) {
        return KERNEL_OBJECT_ERR_NOT_FOUND;
    }

    info->owner_pid = object->owner_pid;
    info->generation = object->generation;
    info->size = object->size;
    info->page_count = object->page_count;
    info->rights = object->rights;
    info->ref_count = object->ref_count;
    return KERNEL_OBJECT_OK;
}

int kernel_shared_memory_read(uint64_t object_id, uint32_t offset, uint8_t* buffer, uint32_t size) {
    SharedMemoryObject* object = shared_from_id(object_id);
    if (object == 0 || buffer == 0 || offset > object->size || size > object->size - offset) {
        return KERNEL_OBJECT_ERR_INVALID;
    }
    for (uint32_t i = 0; i < size; i++) {
        buffer[i] = object->bytes[offset + i];
    }
    return KERNEL_OBJECT_OK;
}

int kernel_shared_memory_write(uint64_t object_id, uint32_t offset, const uint8_t* buffer, uint32_t size) {
    SharedMemoryObject* object = shared_from_id(object_id);
    if (object == 0 || buffer == 0 || offset > object->size || size > object->size - offset) {
        return KERNEL_OBJECT_ERR_INVALID;
    }
    for (uint32_t i = 0; i < size; i++) {
        object->bytes[offset + i] = buffer[i];
    }
    return KERNEL_OBJECT_OK;
}

uint64_t kernel_graphics_surface_create(KernelHandleTable* table,
                                        uint32_t owner_pid,
                                        uint32_t width,
                                        uint32_t height,
                                        uint32_t pixel_format,
                                        uint32_t rights) {
    if (table == 0 || width == 0 || height == 0 || width > KERNEL_GRAPHICS_SURFACE_MAX_PIXELS / height) {
        return 0;
    }

    uint32_t pixel_count = width * height;
    if (pixel_count > KERNEL_GRAPHICS_SURFACE_MAX_PIXELS) {
        return 0;
    }

    uint32_t byte_size = pixel_count * sizeof(uint32_t);
    for (uint32_t i = 0; i < KERNEL_GRAPHICS_SURFACE_MAX_OBJECTS; i++) {
        GraphicsSurfaceObject* object = &surface_objects[i];
        if (object->active) {
            continue;
        }

        uint32_t* pixels = (uint32_t*)kmalloc(byte_size);
        if (pixels == 0) {
            return 0;
        }
        zero_bytes((uint8_t*)pixels, byte_size);

        if (!gfx_surface_init(&object->surface,
                              pixels,
                              width,
                              height,
                              width,
                              pixel_format,
                              GFX_SURFACE_FLAG_OWNS_PIXELS)) {
            kfree(pixels);
            return 0;
        }

        object->active = 1;
        object->generation = next_generation(object->generation);
        object->owner_pid = owner_pid;
        object->ref_count = 1;
        object->byte_size = byte_size;

        uint64_t object_id = make_object_id(i, object->generation);
        uint64_t handle = kernel_handle_alloc(table,
                                              KERNEL_HANDLE_TYPE_GRAPHICS_SURFACE,
                                              rights,
                                              object_id,
                                              byte_size);
        if (handle == 0) {
            release_surface(object);
            return 0;
        }
        return handle;
    }

    return 0;
}

int kernel_graphics_surface_get_info(uint64_t object_id, KernelGraphicsSurfaceInfo* info) {
    GraphicsSurfaceObject* object = surface_from_id(object_id);
    if (object == 0 || info == 0) {
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
    return KERNEL_OBJECT_OK;
}

GraphicsSurface* kernel_graphics_surface_get(uint64_t object_id) {
    GraphicsSurfaceObject* object = surface_from_id(object_id);
    return object != 0 ? &object->surface : 0;
}

int kernel_object_retain_handle_object(const KernelHandle* handle) {
    if (handle == 0 || !handle->active) {
        return 0;
    }
    if (handle->type == KERNEL_HANDLE_TYPE_SHARED_MEMORY) {
        SharedMemoryObject* object = shared_from_id(handle->object);
        if (object == 0) {
            return 0;
        }
        object->ref_count++;
        return 1;
    }
    if (handle->type == KERNEL_HANDLE_TYPE_GRAPHICS_SURFACE) {
        GraphicsSurfaceObject* object = surface_from_id(handle->object);
        if (object == 0) {
            return 0;
        }
        object->ref_count++;
        return 1;
    }
    return 0;
}

void kernel_object_release_handle_object(const KernelHandle* handle) {
    if (handle == 0) {
        return;
    }
    if (handle->type == KERNEL_HANDLE_TYPE_SHARED_MEMORY) {
        release_shared(shared_from_id(handle->object));
    } else if (handle->type == KERNEL_HANDLE_TYPE_GRAPHICS_SURFACE) {
        release_surface(surface_from_id(handle->object));
    }
}

uint64_t kernel_object_clone_handle(KernelHandleTable* target_table, const KernelHandle* source) {
    if (target_table == 0 || source == 0 || !source->active) {
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
                                          source->rights,
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

    uint32_t released = 0;
    for (uint32_t i = 0; i < KERNEL_HANDLE_TABLE_SIZE; i++) {
        KernelHandle* entry = &table->entries[i];
        if (!entry->active) {
            continue;
        }
        kernel_object_release_handle_object(entry);
        entry->active = 0;
        entry->type = KERNEL_HANDLE_TYPE_NONE;
        entry->rights = 0;
        entry->object = 0;
        entry->extra = 0;
        released++;
    }
    table->active_count = 0;
    return released;
}
