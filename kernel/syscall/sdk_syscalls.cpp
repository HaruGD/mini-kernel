#include <stdint.h>

#include "drivers/gop.h"
#include "drivers/keyboard.h"
#include "drivers/pit.h"
#include "kernel/syscall64.h"
#include "kernel/userprog64.h"
#include "kernel/syscall/sdk_syscalls.h"

struct UserGraphicsRect {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t color;
};

struct UserKeyboardEvent {
    uint32_t type;
    uint32_t keycode;
    uint32_t modifiers;
    uint32_t character;
};

static_assert(sizeof(OsGraphicsInfo) == 16, "OsGraphicsInfo ABI changed");
static_assert(sizeof(UserGraphicsRect) == 20, "UserGraphicsRect ABI changed");
static_assert(sizeof(UserKeyboardEvent) == 16, "UserKeyboardEvent ABI changed");

static uint64_t invalid_argument() {
    return (uint64_t)(int64_t)SYS_ERR_INVALID_ARGUMENT;
}

static uint64_t dispatch_graphics(uint64_t syscall_no,
                                  uint64_t arg1,
                                  uint64_t arg2,
                                  uint64_t arg3) {
    if (syscall_no == SYS_GFX_GET_INFO) {
        const GOPInfo* info = gop.info();
        if (info == 0) {
            return (uint64_t)(int64_t)SYS_ERR_NOT_READY;
        }

        OsGraphicsInfo user_info;
        user_info.width = info->width;
        user_info.height = info->height;
        user_info.pixels_per_scanline = info->pixels_per_scanline;
        user_info.format = info->format;
        if (!copy_kernel_to_user_buffer((uint8_t*)(uintptr_t)arg1,
                                        (const uint8_t*)&user_info,
                                        sizeof(user_info))) {
            return invalid_argument();
        }
        return 0;
    }

    if (syscall_no == SYS_GFX_PUT_PIXEL) {
        const GOPInfo* info = gop.info();
        if (info == 0) {
            return (uint64_t)(int64_t)SYS_ERR_NOT_READY;
        }
        if (arg1 >= info->width || arg2 >= info->height) {
            return (uint64_t)(int64_t)SYS_ERR_OUT_OF_RANGE;
        }
        gop.putpixel((uint32_t)arg1, (uint32_t)arg2, (uint32_t)arg3);
        return 0;
    }

    if (syscall_no == SYS_GFX_FILL_RECT) {
        UserGraphicsRect rect;
        if (!copy_user_buffer((const uint8_t*)(uintptr_t)arg1,
                              (uint8_t*)&rect,
                              sizeof(rect))) {
            return invalid_argument();
        }

        const GOPInfo* info = gop.info();
        if (info == 0) {
            return (uint64_t)(int64_t)SYS_ERR_NOT_READY;
        }
        if (rect.width == 0 || rect.height == 0) {
            return invalid_argument();
        }
        if (rect.x >= info->width || rect.y >= info->height) {
            return (uint64_t)(int64_t)SYS_ERR_OUT_OF_RANGE;
        }
        gop.fill_rect(rect.x, rect.y, rect.width, rect.height, rect.color);
        return 0;
    }

    if (!gop.ready()) {
        return (uint64_t)(int64_t)SYS_ERR_NOT_READY;
    }
    gop.clear((uint32_t)arg1);
    return 0;
}

static uint64_t dispatch_keyboard(uint64_t user_event_address, bool wait) {
    if (!user_buffer_writable((uint8_t*)(uintptr_t)user_event_address,
                              sizeof(UserKeyboardEvent))) {
        return invalid_argument();
    }

    while (1) {
        KeyboardEvent event;
        if (keyboard.try_read_event(&event)) {
            UserKeyboardEvent user_event;
            user_event.type = event.type;
            user_event.keycode = event.keycode;
            user_event.modifiers = event.modifiers;
            user_event.character = event.character;
            if (!copy_kernel_to_user_buffer((uint8_t*)(uintptr_t)user_event_address,
                                            (const uint8_t*)&user_event,
                                            sizeof(user_event))) {
                return invalid_argument();
            }
            return 0;
        }
        if (!wait) {
            return (uint64_t)(int64_t)SYS_ERR_WOULD_BLOCK;
        }
        if (continue_woken_processes(0)) {
            redraw_user_shell_prompt_if_needed();
            continue;
        }
        if (continue_background_processes(0)) {
            redraw_user_shell_prompt_if_needed();
            continue;
        }
        __asm__ volatile("sti; hlt; cli");
    }
}

bool dispatch_sdk_syscall64(uint64_t syscall_no,
                            uint64_t arg1,
                            uint64_t arg2,
                            uint64_t arg3,
                            uint64_t* result) {
    if (result == 0) {
        return false;
    }
    if (syscall_no == SYS_TIME_TICKS) {
        *result = pit.get_tick64();
        return true;
    }
    if (syscall_no == SYS_TIME_FREQUENCY) {
        *result = pit.get_frequency();
        return true;
    }
    if (syscall_no >= SYS_GFX_GET_INFO && syscall_no <= SYS_GFX_CLEAR) {
        *result = dispatch_graphics(syscall_no, arg1, arg2, arg3);
        return true;
    }
    if (syscall_no == SYS_KEYBOARD_EVENT) {
        *result = dispatch_keyboard(arg1, arg2 != 0);
        return true;
    }
    return false;
}
