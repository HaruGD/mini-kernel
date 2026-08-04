#ifndef OS64_TERMINAL_TYPES_H
#define OS64_TERMINAL_TYPES_H

#include <stddef.h>
#include <stdint.h>

#define OS64_TERMINAL_ABI_VERSION 1u

#define OS_TERMINAL_COMMAND_HELLO 1u
#define OS_TERMINAL_COMMAND_OUTPUT 2u
#define OS_TERMINAL_COMMAND_INPUT 3u
#define OS_TERMINAL_COMMAND_RESIZE 4u
#define OS_TERMINAL_COMMAND_HANGUP 5u
#define OS_TERMINAL_COMMAND_EXIT 6u

#define OS_TERMINAL_FLAG_OVERFLOW (1u << 0)
#define OS_TERMINAL_VALID_FLAGS OS_TERMINAL_FLAG_OVERFLOW

#define OS_TERMINAL_PACKET_DATA_MAX 60u
#define OS_TERMINAL_MIN_COLUMNS 20u
#define OS_TERMINAL_MAX_COLUMNS 120u
#define OS_TERMINAL_MIN_ROWS 5u
#define OS_TERMINAL_MAX_ROWS 60u
#define OS_TERMINAL_HISTORY_ROWS 128u
#define OS_TERMINAL_TAB_WIDTH 8u

typedef struct OsTerminalPacket {
    uint32_t size;
    uint32_t abi_version;
    uint32_t command;
    uint32_t flags;
    uint32_t sequence;
    uint32_t columns;
    uint32_t rows;
    uint32_t length;
    int32_t status;
    uint8_t data[OS_TERMINAL_PACKET_DATA_MAX];
} OsTerminalPacket;

typedef struct OsTerminalCell {
    uint32_t codepoint;
    uint32_t foreground;
    uint32_t background;
} OsTerminalCell;

typedef struct OsTerminalModel {
    uint32_t size;
    uint32_t abi_version;
    uint32_t columns;
    uint32_t rows;
    uint32_t cursor_column;
    uint32_t cursor_history_row;
    uint32_t history_count;
    uint32_t view_offset;
    uint32_t foreground;
    uint32_t background;
    uint32_t saved_column;
    uint32_t saved_history_row;
    uint32_t parser_state;
    uint32_t parameter_count;
    uint32_t parameters[4];
    uint32_t dropped_history_rows;
    OsTerminalCell cells[OS_TERMINAL_HISTORY_ROWS][OS_TERMINAL_MAX_COLUMNS];
} OsTerminalModel;

#ifdef __cplusplus
#define OS64_TERMINAL_STATIC_ASSERT(condition, message) static_assert((condition), message)
#else
#define OS64_TERMINAL_STATIC_ASSERT(condition, message) _Static_assert((condition), message)
#endif

OS64_TERMINAL_STATIC_ASSERT(sizeof(OsTerminalPacket) == 96,
                            "OsTerminalPacket ABI changed");
OS64_TERMINAL_STATIC_ASSERT(offsetof(OsTerminalPacket, data) == 36,
                            "OsTerminalPacket.data offset changed");
OS64_TERMINAL_STATIC_ASSERT(sizeof(OsTerminalCell) == 12,
                            "OsTerminalCell ABI changed");

#undef OS64_TERMINAL_STATIC_ASSERT

#endif
