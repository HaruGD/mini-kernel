#ifndef OS64_INPUT_H
#define OS64_INPUT_H

#include <stdint.h>

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

typedef struct OsKeyEvent {
    uint32_t type;
    uint32_t keycode;
    uint32_t modifiers;
    uint32_t character;
} OsKeyEvent;

long os_key_poll(OsKeyEvent* event);
long os_key_wait(OsKeyEvent* event);

#endif
