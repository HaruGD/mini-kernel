#ifndef KERNEL_HANDLE_KERNEL_OBJECTS_H
#define KERNEL_HANDLE_KERNEL_OBJECTS_H

#include <stdint.h>

#include "kernel/graphics/graphics2d.h"
#include "kernel/handle/kernel_handle.h"

#define KERNEL_SHARED_MEMORY_MAX_OBJECTS 16u
#define KERNEL_SHARED_MEMORY_MAX_PAGES 64u
#define KERNEL_GRAPHICS_SURFACE_MAX_OBJECTS 16u
#define KERNEL_GRAPHICS_SURFACE_MAX_PIXELS (1920u * 1080u)

#define KERNEL_OBJECT_OK 0
#define KERNEL_OBJECT_ERR_INVALID (-2)
#define KERNEL_OBJECT_ERR_NO_SLOT (-7)
#define KERNEL_OBJECT_ERR_NO_MEMORY (-10)
#define KERNEL_OBJECT_ERR_NOT_FOUND (-12)

struct KernelSharedMemoryInfo {
    uint32_t owner_pid;
    uint32_t generation;
    uint32_t size;
    uint32_t page_count;
    uint32_t rights;
    uint32_t ref_count;
};

struct KernelGraphicsSurfaceInfo {
    uint32_t owner_pid;
    uint32_t generation;
    uint32_t width;
    uint32_t height;
    uint32_t stride_pixels;
    uint32_t pixel_format;
    uint32_t byte_size;
    uint32_t ref_count;
};

void kernel_objects_init();

uint64_t kernel_shared_memory_create(KernelHandleTable* table,
                                     uint32_t owner_pid,
                                     uint32_t size,
                                     uint32_t rights);
int kernel_shared_memory_get_info(uint64_t object_id, KernelSharedMemoryInfo* info);
int kernel_shared_memory_read(uint64_t object_id, uint32_t offset, uint8_t* buffer, uint32_t size);
int kernel_shared_memory_write(uint64_t object_id, uint32_t offset, const uint8_t* buffer, uint32_t size);

uint64_t kernel_graphics_surface_create(KernelHandleTable* table,
                                        uint32_t owner_pid,
                                        uint32_t width,
                                        uint32_t height,
                                        uint32_t pixel_format,
                                        uint32_t rights);
int kernel_graphics_surface_get_info(uint64_t object_id, KernelGraphicsSurfaceInfo* info);
GraphicsSurface* kernel_graphics_surface_get(uint64_t object_id);

uint64_t kernel_object_clone_handle(KernelHandleTable* target_table, const KernelHandle* source);
int kernel_object_close_handle(KernelHandleTable* table, uint64_t handle, KernelHandle* closed_out);
uint32_t kernel_object_release_table(KernelHandleTable* table);
int kernel_object_retain_handle_object(const KernelHandle* handle);
void kernel_object_release_handle_object(const KernelHandle* handle);

#endif
