#ifndef OS64_INPUT_TYPES_H
#define OS64_INPUT_TYPES_H

#include <stdint.h>
#include <stddef.h>

#define OS64_INPUT_ABI_VERSION 1u

#define OS_INPUT_EVENT_NONE 0u
#define OS_INPUT_EVENT_KEY 1u
#define OS_INPUT_EVENT_POINTER 2u

#define OS_KEY_EVENT_NONE 0u
#define OS_KEY_EVENT_DOWN 1u
#define OS_KEY_EVENT_UP 2u

#define OS_KEY_MOD_SHIFT (1u << 0)
#define OS_KEY_MOD_CTRL (1u << 1)
#define OS_KEY_MOD_ALT (1u << 2)
#define OS_KEY_MOD_CAPS_LOCK (1u << 3)

#define OS_KEY_ESCAPE 0x001u
#define OS_KEY_ENTER 0x01Cu
#define OS_KEY_SPACE 0x039u
#define OS_KEY_UP 0x148u
#define OS_KEY_LEFT 0x14Bu
#define OS_KEY_RIGHT 0x14Du
#define OS_KEY_DOWN 0x150u

#define OS_POINTER_EVENT_NONE 0u
#define OS_POINTER_EVENT_MOVE 1u
#define OS_POINTER_EVENT_BUTTON_DOWN 2u
#define OS_POINTER_EVENT_BUTTON_UP 3u
#define OS_POINTER_EVENT_WHEEL 4u

#define OS_POINTER_BUTTON_LEFT (1u << 0)
#define OS_POINTER_BUTTON_RIGHT (1u << 1)
#define OS_POINTER_BUTTON_MIDDLE (1u << 2)
#define OS_POINTER_BUTTON_X1 (1u << 3)
#define OS_POINTER_BUTTON_X2 (1u << 4)

#define OS_POINTER_POSITION_UNKNOWN ((int32_t)0x80000000)

typedef struct OsKeyEvent {
    uint32_t type;
    uint32_t keycode;
    uint32_t modifiers;
    uint32_t character;
} OsKeyEvent;

typedef struct OsPointerEvent {
    uint32_t type;
    int32_t x;
    int32_t y;
    int32_t delta_x;
    int32_t delta_y;
    int32_t wheel_delta;
    uint32_t buttons;
    uint32_t changed_buttons;
} OsPointerEvent;

typedef struct OsInputEvent {
    uint32_t type;
    uint32_t size;
    uint64_t timestamp_ticks;
    union {
        OsKeyEvent key;
        OsPointerEvent pointer;
    } data;
} OsInputEvent;

#ifdef __cplusplus
#define OS64_INPUT_STATIC_ASSERT(condition, message) static_assert((condition), message)
#else
#define OS64_INPUT_STATIC_ASSERT(condition, message) _Static_assert((condition), message)
#endif

OS64_INPUT_STATIC_ASSERT(sizeof(OsKeyEvent) == 16, "OsKeyEvent ABI changed");
OS64_INPUT_STATIC_ASSERT(offsetof(OsKeyEvent, type) == 0, "OsKeyEvent.type offset changed");
OS64_INPUT_STATIC_ASSERT(offsetof(OsKeyEvent, keycode) == 4, "OsKeyEvent.keycode offset changed");
OS64_INPUT_STATIC_ASSERT(offsetof(OsKeyEvent, modifiers) == 8, "OsKeyEvent.modifiers offset changed");
OS64_INPUT_STATIC_ASSERT(offsetof(OsKeyEvent, character) == 12, "OsKeyEvent.character offset changed");

OS64_INPUT_STATIC_ASSERT(sizeof(OsPointerEvent) == 32, "OsPointerEvent ABI changed");
OS64_INPUT_STATIC_ASSERT(offsetof(OsPointerEvent, type) == 0, "OsPointerEvent.type offset changed");
OS64_INPUT_STATIC_ASSERT(offsetof(OsPointerEvent, x) == 4, "OsPointerEvent.x offset changed");
OS64_INPUT_STATIC_ASSERT(offsetof(OsPointerEvent, y) == 8, "OsPointerEvent.y offset changed");
OS64_INPUT_STATIC_ASSERT(offsetof(OsPointerEvent, delta_x) == 12, "OsPointerEvent.delta_x offset changed");
OS64_INPUT_STATIC_ASSERT(offsetof(OsPointerEvent, delta_y) == 16, "OsPointerEvent.delta_y offset changed");
OS64_INPUT_STATIC_ASSERT(offsetof(OsPointerEvent, wheel_delta) == 20, "OsPointerEvent.wheel_delta offset changed");
OS64_INPUT_STATIC_ASSERT(offsetof(OsPointerEvent, buttons) == 24, "OsPointerEvent.buttons offset changed");
OS64_INPUT_STATIC_ASSERT(offsetof(OsPointerEvent, changed_buttons) == 28, "OsPointerEvent.changed_buttons offset changed");

OS64_INPUT_STATIC_ASSERT(sizeof(OsInputEvent) == 48, "OsInputEvent ABI changed");
OS64_INPUT_STATIC_ASSERT(offsetof(OsInputEvent, type) == 0, "OsInputEvent.type offset changed");
OS64_INPUT_STATIC_ASSERT(offsetof(OsInputEvent, size) == 4, "OsInputEvent.size offset changed");
OS64_INPUT_STATIC_ASSERT(offsetof(OsInputEvent, timestamp_ticks) == 8, "OsInputEvent.timestamp_ticks offset changed");
OS64_INPUT_STATIC_ASSERT(offsetof(OsInputEvent, data) == 16, "OsInputEvent.data offset changed");

#undef OS64_INPUT_STATIC_ASSERT

#endif
