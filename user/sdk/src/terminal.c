#include <limits.h>

#include "os64/os64.h"

#define TERMINAL_PARSER_NORMAL 0u
#define TERMINAL_PARSER_ESCAPE 1u
#define TERMINAL_PARSER_CSI 2u
#define TERMINAL_CELL_WIDTH 6u
#define TERMINAL_CELL_HEIGHT 8u

static int dimensions_valid(uint32_t columns, uint32_t rows) {
    return columns >= OS_TERMINAL_MIN_COLUMNS &&
           columns <= OS_TERMINAL_MAX_COLUMNS &&
           rows >= OS_TERMINAL_MIN_ROWS && rows <= OS_TERMINAL_MAX_ROWS;
}

void os_terminal_packet_init(OsTerminalPacket* packet, uint32_t command) {
    if (packet == 0) return;
    os_memset(packet, 0, sizeof(*packet));
    packet->size = sizeof(*packet);
    packet->abi_version = OS64_TERMINAL_ABI_VERSION;
    packet->command = command;
}

long os_terminal_packet_validate(const OsTerminalPacket* packet) {
    if (packet == 0 || packet->size != sizeof(*packet) ||
        packet->abi_version != OS64_TERMINAL_ABI_VERSION ||
        packet->command < OS_TERMINAL_COMMAND_HELLO ||
        packet->command > OS_TERMINAL_COMMAND_EXIT ||
        (packet->flags & ~OS_TERMINAL_VALID_FLAGS) != 0 ||
        packet->sequence == 0 || packet->length > OS_TERMINAL_PACKET_DATA_MAX) {
        return OS_ERR_BAD_BUFFER;
    }
    if ((packet->command == OS_TERMINAL_COMMAND_OUTPUT ||
         packet->command == OS_TERMINAL_COMMAND_INPUT) && packet->length == 0) {
        return OS_ERR_BAD_BUFFER;
    }
    if ((packet->command == OS_TERMINAL_COMMAND_HELLO ||
         packet->command == OS_TERMINAL_COMMAND_RESIZE) &&
        !dimensions_valid(packet->columns, packet->rows)) {
        return OS_ERR_OUT_OF_RANGE;
    }
    if (packet->command != OS_TERMINAL_COMMAND_OUTPUT &&
        packet->command != OS_TERMINAL_COMMAND_INPUT && packet->length != 0) {
        return OS_ERR_BAD_BUFFER;
    }
    return OS_SUCCESS;
}

long os_terminal_send(OsProcessIdentity peer,
                      OsTerminalPacket* packet,
                      uint32_t retry_ticks) {
    if (peer.pid == 0 || peer.generation == 0 ||
        os_terminal_packet_validate(packet) < 0) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    OsIpcMessageV2 message;
    os_msg_v2_init(&message, OS_IPC_MESSAGE_EVENT);
    message.length = sizeof(*packet);
    os_memcpy(message.payload, packet, sizeof(*packet));
    uint32_t start = (uint32_t)os_time_ticks();
    while (1) {
        long result = os_msg_v2_send_to_identity(peer, &message);
        if (result != OS_ERR_QUEUE_FULL && result != OS_ERR_WOULD_BLOCK) {
            return result;
        }
        if (retry_ticks == 0 ||
            (uint32_t)(os_time_ticks() - start) >= retry_ticks) {
            return result;
        }
        os_sleep(1);
    }
}

static int model_valid(const OsTerminalModel* model) {
    return model != 0 && model->size == sizeof(*model) &&
           model->abi_version == OS64_TERMINAL_ABI_VERSION &&
           dimensions_valid(model->columns, model->rows) &&
           model->history_count > 0 &&
           model->history_count <= OS_TERMINAL_HISTORY_ROWS &&
           model->cursor_history_row < model->history_count &&
           model->cursor_column < model->columns;
}

static uint32_t ansi_color(uint32_t index, int bright) {
    static const uint32_t normal[8] = {
        OS_RGB(18, 20, 25), OS_RGB(196, 70, 70), OS_RGB(80, 180, 105),
        OS_RGB(205, 170, 70), OS_RGB(80, 125, 205), OS_RGB(170, 90, 190),
        OS_RGB(65, 175, 185), OS_RGB(205, 210, 220),
    };
    static const uint32_t vivid[8] = {
        OS_RGB(90, 95, 105), OS_RGB(255, 100, 100), OS_RGB(110, 230, 135),
        OS_RGB(255, 220, 100), OS_RGB(110, 160, 255), OS_RGB(225, 130, 245),
        OS_RGB(95, 225, 235), OS_RGB(250, 250, 250),
    };
    return bright ? vivid[index & 7u] : normal[index & 7u];
}

static void clear_row(OsTerminalModel* model, uint32_t row) {
    for (uint32_t column = 0; column < OS_TERMINAL_MAX_COLUMNS; column++) {
        model->cells[row][column].codepoint = ' ';
        model->cells[row][column].foreground = model->foreground;
        model->cells[row][column].background = model->background;
    }
}

static void clear_all(OsTerminalModel* model) {
    for (uint32_t row = 0; row < OS_TERMINAL_HISTORY_ROWS; row++) {
        clear_row(model, row);
    }
    model->cursor_column = 0;
    model->cursor_history_row = 0;
    model->history_count = 1;
    model->view_offset = 0;
}

long os_terminal_model_init(OsTerminalModel* model,
                            uint32_t columns,
                            uint32_t rows) {
    if (model == 0 || !dimensions_valid(columns, rows)) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    os_memset(model, 0, sizeof(*model));
    model->size = sizeof(*model);
    model->abi_version = OS64_TERMINAL_ABI_VERSION;
    model->columns = columns;
    model->rows = rows;
    model->foreground = ansi_color(7, 0);
    model->background = ansi_color(0, 0);
    clear_all(model);
    return OS_SUCCESS;
}

long os_terminal_model_resize(OsTerminalModel* model,
                              uint32_t columns,
                              uint32_t rows) {
    if (!model_valid(model) || !dimensions_valid(columns, rows)) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    model->columns = columns;
    model->rows = rows;
    if (model->cursor_column >= columns) model->cursor_column = columns - 1u;
    uint32_t maximum_offset = model->history_count > rows
        ? model->history_count - rows : 0;
    if (model->view_offset > maximum_offset) model->view_offset = maximum_offset;
    return OS_SUCCESS;
}

static void new_line(OsTerminalModel* model) {
    model->cursor_column = 0;
    if (model->cursor_history_row + 1u < OS_TERMINAL_HISTORY_ROWS) {
        model->cursor_history_row++;
        if (model->cursor_history_row >= model->history_count) {
            model->history_count = model->cursor_history_row + 1u;
            clear_row(model, model->cursor_history_row);
        }
    } else {
        for (uint32_t row = 1; row < OS_TERMINAL_HISTORY_ROWS; row++) {
            os_memcpy(model->cells[row - 1u], model->cells[row],
                      sizeof(model->cells[row]));
        }
        model->cursor_history_row = OS_TERMINAL_HISTORY_ROWS - 1u;
        clear_row(model, model->cursor_history_row);
        model->dropped_history_rows++;
    }
    model->view_offset = 0;
}

static void put_codepoint(OsTerminalModel* model, uint32_t codepoint) {
    if (model->cursor_column >= model->columns) new_line(model);
    OsTerminalCell* cell =
        &model->cells[model->cursor_history_row][model->cursor_column++];
    cell->codepoint = codepoint;
    cell->foreground = model->foreground;
    cell->background = model->background;
    model->view_offset = 0;
    if (model->cursor_column >= model->columns) new_line(model);
}

static uint32_t parameter(const OsTerminalModel* model,
                          uint32_t index,
                          uint32_t fallback) {
    if (index >= model->parameter_count || model->parameters[index] == 0)
        return fallback;
    return model->parameters[index];
}

static void apply_sgr(OsTerminalModel* model) {
    if (model->parameter_count == 0) model->parameter_count = 1;
    for (uint32_t i = 0; i < model->parameter_count; i++) {
        uint32_t value = model->parameters[i];
        if (value == 0) {
            model->foreground = ansi_color(7, 0);
            model->background = ansi_color(0, 0);
        } else if (value >= 30 && value <= 37) {
            model->foreground = ansi_color(value - 30u, 0);
        } else if (value >= 40 && value <= 47) {
            model->background = ansi_color(value - 40u, 0);
        } else if (value >= 90 && value <= 97) {
            model->foreground = ansi_color(value - 90u, 1);
        } else if (value >= 100 && value <= 107) {
            model->background = ansi_color(value - 100u, 1);
        }
    }
}

static void apply_csi(OsTerminalModel* model, uint8_t final) {
    uint32_t visible_start = model->history_count > model->rows
        ? model->history_count - model->rows : 0;
    if (final == 'm') {
        apply_sgr(model);
    } else if (final == 'H' || final == 'f') {
        uint32_t row = parameter(model, 0, 1);
        uint32_t column = parameter(model, 1, 1);
        if (row > model->rows) row = model->rows;
        if (column > model->columns) column = model->columns;
        uint32_t target = visible_start + row - 1u;
        if (target >= OS_TERMINAL_HISTORY_ROWS)
            target = OS_TERMINAL_HISTORY_ROWS - 1u;
        while (model->history_count <= target) {
            clear_row(model, model->history_count++);
        }
        model->cursor_history_row = target;
        model->cursor_column = column - 1u;
    } else if (final == 'A') {
        uint32_t amount = parameter(model, 0, 1);
        uint32_t minimum = visible_start;
        model->cursor_history_row =
            amount > model->cursor_history_row - minimum
                ? minimum : model->cursor_history_row - amount;
    } else if (final == 'B') {
        uint32_t amount = parameter(model, 0, 1);
        uint32_t maximum = model->history_count - 1u;
        model->cursor_history_row = amount > maximum - model->cursor_history_row
            ? maximum : model->cursor_history_row + amount;
    } else if (final == 'C') {
        uint32_t amount = parameter(model, 0, 1);
        model->cursor_column = amount >= model->columns - model->cursor_column
            ? model->columns - 1u : model->cursor_column + amount;
    } else if (final == 'D') {
        uint32_t amount = parameter(model, 0, 1);
        model->cursor_column = amount > model->cursor_column
            ? 0 : model->cursor_column - amount;
    } else if (final == 'J' && parameter(model, 0, 0) == 2) {
        clear_all(model);
    } else if (final == 'K') {
        uint32_t mode = parameter(model, 0, 0);
        uint32_t first = mode == 1 ? 0 : model->cursor_column;
        uint32_t last = mode == 1 ? model->cursor_column + 1u : model->columns;
        if (mode == 2) { first = 0; last = model->columns; }
        for (uint32_t column = first; column < last; column++) {
            OsTerminalCell* cell =
                &model->cells[model->cursor_history_row][column];
            cell->codepoint = ' ';
            cell->foreground = model->foreground;
            cell->background = model->background;
        }
    } else if (final == 's') {
        model->saved_column = model->cursor_column;
        model->saved_history_row = model->cursor_history_row;
    } else if (final == 'u') {
        if (model->saved_history_row < model->history_count)
            model->cursor_history_row = model->saved_history_row;
        if (model->saved_column < model->columns)
            model->cursor_column = model->saved_column;
    }
}

long os_terminal_model_write(OsTerminalModel* model,
                             const void* bytes,
                             uint32_t length) {
    if (!model_valid(model) || (length != 0 && bytes == 0)) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    const uint8_t* input = (const uint8_t*)bytes;
    for (uint32_t i = 0; i < length; i++) {
        uint8_t byte = input[i];
        if (model->parser_state == TERMINAL_PARSER_ESCAPE) {
            if (byte == '[') {
                model->parser_state = TERMINAL_PARSER_CSI;
                model->parameter_count = 1;
                for (uint32_t p = 0; p < 4; p++) model->parameters[p] = 0;
            } else {
                model->parser_state = TERMINAL_PARSER_NORMAL;
            }
            continue;
        }
        if (model->parser_state == TERMINAL_PARSER_CSI) {
            if (byte >= '0' && byte <= '9') {
                uint32_t* value = &model->parameters[model->parameter_count - 1u];
                if (*value <= 999u) *value = *value * 10u + (byte - '0');
                continue;
            }
            if (byte == ';' && model->parameter_count < 4) {
                model->parameter_count++;
                continue;
            }
            apply_csi(model, byte);
            model->parser_state = TERMINAL_PARSER_NORMAL;
            continue;
        }
        if (byte == 0x1Bu) {
            model->parser_state = TERMINAL_PARSER_ESCAPE;
        } else if (byte == '\r') {
            model->cursor_column = 0;
        } else if (byte == '\n') {
            new_line(model);
        } else if (byte == '\b') {
            if (model->cursor_column > 0) model->cursor_column--;
        } else if (byte == '\t') {
            uint32_t target = (model->cursor_column + OS_TERMINAL_TAB_WIDTH) &
                              ~(OS_TERMINAL_TAB_WIDTH - 1u);
            do { put_codepoint(model, ' '); }
            while (model->cursor_column != 0 && model->cursor_column < target);
        } else if (byte >= 32u && byte <= 126u) {
            put_codepoint(model, byte);
        }
    }
    return OS_SUCCESS;
}

long os_terminal_model_scroll(OsTerminalModel* model, int32_t rows) {
    if (!model_valid(model) || rows == INT32_MIN) return OS_ERR_INVALID_ARGUMENT;
    uint32_t maximum = model->history_count > model->rows
        ? model->history_count - model->rows : 0;
    if (rows > 0) {
        uint32_t amount = (uint32_t)rows;
        model->view_offset = amount > maximum - model->view_offset
            ? maximum : model->view_offset + amount;
    } else {
        uint32_t amount = (uint32_t)(-rows);
        model->view_offset = amount > model->view_offset
            ? 0 : model->view_offset - amount;
    }
    return OS_SUCCESS;
}

long os_terminal_model_render(const OsTerminalModel* model,
                              OsSurfaceCanvas* canvas,
                              OsRect bounds) {
    if (!model_valid(model) || canvas == 0 || canvas->pixels == 0 ||
        bounds.width <= 0 || bounds.height <= 0) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    long result = os_surface_canvas_fill_rect(canvas, bounds, ansi_color(0, 0));
    if (result < 0) return result;
    uint32_t columns = (uint32_t)bounds.width / TERMINAL_CELL_WIDTH;
    uint32_t rows = (uint32_t)bounds.height / TERMINAL_CELL_HEIGHT;
    if (columns > model->columns) columns = model->columns;
    if (rows > model->rows) rows = model->rows;
    if (columns == 0 || rows == 0) return OS_SUCCESS;
    uint32_t tail = model->history_count - 1u;
    uint32_t end = tail >= model->view_offset ? tail - model->view_offset : 0;
    uint32_t start = end + 1u > rows ? end + 1u - rows : 0;
    for (uint32_t row = start, screen_row = 0;
         row <= end && screen_row < rows; row++, screen_row++) {
        for (uint32_t column = 0; column < columns; column++) {
            const OsTerminalCell* cell = &model->cells[row][column];
            int32_t x = bounds.x + (int32_t)(column * TERMINAL_CELL_WIDTH);
            int32_t y = bounds.y + (int32_t)(screen_row * TERMINAL_CELL_HEIGHT);
            if (cell->background != ansi_color(0, 0)) {
                result = os_surface_canvas_fill_rect(
                    canvas, (OsRect){x, y, TERMINAL_CELL_WIDTH,
                                             TERMINAL_CELL_HEIGHT},
                    cell->background);
                if (result < 0) return result;
            }
            if (cell->codepoint != ' ') {
                char glyph[2] = {(char)cell->codepoint, '\0'};
                result = os_surface_canvas_draw_text(
                    canvas, x, y, glyph, cell->foreground, cell->background,
                    OS_SURFACE_TEXT_TRANSPARENT_BG);
                if (result < 0) return result;
            }
        }
    }
    if (model->view_offset == 0 && model->cursor_history_row >= start &&
        model->cursor_history_row <= end && model->cursor_column < columns) {
        int32_t x = bounds.x + (int32_t)(model->cursor_column * TERMINAL_CELL_WIDTH);
        int32_t y = bounds.y +
            (int32_t)((model->cursor_history_row - start) * TERMINAL_CELL_HEIGHT) +
            (TERMINAL_CELL_HEIGHT - 1);
        return os_surface_canvas_draw_line(canvas, x, y,
                                           x + TERMINAL_CELL_WIDTH - 2, y,
                                           ansi_color(7, 1));
    }
    return OS_SUCCESS;
}
