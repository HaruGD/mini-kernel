#include <os64/os64.h>
#include "internal.h"

typedef struct OsGraphicsRectCommand {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t color;
} OsGraphicsRectCommand;

_Static_assert(sizeof(OsGraphicsRectCommand) == 20, "graphics rectangle ABI changed");

long os_gfx_get_info(OsGraphicsInfo* info) {
    if (info == 0) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    return os_syscall1(OS_SYS_GFX_GET_INFO, (long)info);
}

long os_gfx_put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    return os_syscall3(OS_SYS_GFX_PUT_PIXEL, x, y, color);
}

long os_gfx_fill_rect(uint32_t x,
                      uint32_t y,
                      uint32_t width,
                      uint32_t height,
                      uint32_t color) {
    OsGraphicsRectCommand command;
    command.x = x;
    command.y = y;
    command.width = width;
    command.height = height;
    command.color = color;
    return os_syscall1(OS_SYS_GFX_FILL_RECT, (long)&command);
}

long os_gfx_clear(uint32_t color) {
    return os_syscall1(OS_SYS_GFX_CLEAR, color);
}
