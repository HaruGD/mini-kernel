#include <os64/os64.h>

#define TERMINAL_MARGIN 8
#define TERMINAL_BOTTOM_MARGIN 12
#define TERMINAL_CELL_WIDTH 6u
#define TERMINAL_CELL_HEIGHT 8u

static OsTerminalModel terminal_model;
static OsProcessIdentity shell_identity;
static uint32_t frontend_sequence = 1;
static uint32_t shell_sequence;
static uint32_t output_bytes;
static uint32_t output_overflows;
typedef struct TerminalTranscriptMarker {
    const char* text;
    const char* name;
    uint32_t progress;
} TerminalTranscriptMarker;

static TerminalTranscriptMarker transcript_markers[] = {
    {"phase5d-ok", "phase5d-ok", 0},
    {"utest.bin", "list-files", 0},
    {"Saved: /term5d.txt", "save-file", 0},
    {"stream-output-ok", "cat-file", 0},
    {"argv[1]=child-output-ok", "external-output", 0},
    {"Press one key to return:", "external-input-ready", 0},
    {"You chose: x", "external-input", 0},
};
static int child_exited;
static int32_t child_status;
static int fault_shortcut_enabled;

static uint32_t next_sequence(void) {
    uint32_t sequence = frontend_sequence++;
    if (frontend_sequence == 0) frontend_sequence = 1;
    return sequence == 0 ? next_sequence() : sequence;
}

static uint32_t append_u32(char* output, uint32_t offset, uint32_t value) {
    char reversed[10];
    uint32_t count = 0;
    do {
        reversed[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0);
    while (count != 0) output[offset++] = reversed[--count];
    output[offset] = '\0';
    return offset;
}

static long launch_shell(void) {
    OsProcessIdentity self;
    long result = os_get_process_identity(&self);
    if (result < 0) return result;
    char command[96] = "terminal_shell.elf ";
    uint32_t offset = (uint32_t)os_strlen(command);
    offset = append_u32(command, offset, self.pid);
    command[offset++] = ' ';
    command[offset] = '\0';
    append_u32(command, offset, self.generation);
    uint32_t permissions = OS_PROCESS_PERMISSION_IPC |
                           OS_PROCESS_PERMISSION_MANAGE_CHILD;
    result = os_run_with_permissions(command, permissions);
    if (result < 0) return result;
    if (os_set_background(0, 1) < 0) return OS_ERR_NOT_READY;

    OsIpcReceiveFilter filter;
    os_ipc_filter_init(&filter);
    filter.flags = OS_IPC_FILTER_TYPE;
    filter.type = OS_IPC_MESSAGE_EVENT;
    uint32_t start = (uint32_t)os_time_ticks();
    while ((uint32_t)(os_time_ticks() - start) < 500u) {
        OsIpcMessageV2 message;
        result = os_msg_v2_recv_match(&filter, &message);
        if (result == OS_ERR_WOULD_BLOCK) {
            os_sleep(1);
            continue;
        }
        if (result < 0 || message.length != sizeof(OsTerminalPacket)) continue;
        OsTerminalPacket packet;
        os_memcpy(&packet, message.payload, sizeof(packet));
        if (os_terminal_packet_validate(&packet) < 0 ||
            packet.command != OS_TERMINAL_COMMAND_HELLO) continue;
        shell_identity = os_msg_v2_sender_identity(&message);
        shell_sequence = packet.sequence;
        return shell_identity.pid != 0 && shell_identity.generation != 0
            ? OS_SUCCESS : OS_ERR_BAD_BUFFER;
    }
    return OS_ERR_TIMEOUT;
}

static void dimensions_for_window(const OsWindow* window,
                                  uint32_t* columns,
                                  uint32_t* rows) {
    uint32_t width = window->surface_info.width > TERMINAL_MARGIN * 2
        ? window->surface_info.width - TERMINAL_MARGIN * 2 : 0;
    uint32_t vertical_margins = TERMINAL_MARGIN + TERMINAL_BOTTOM_MARGIN;
    uint32_t height = window->surface_info.height > vertical_margins
        ? window->surface_info.height - vertical_margins : 0;
    *columns = width / TERMINAL_CELL_WIDTH;
    *rows = height / TERMINAL_CELL_HEIGHT;
    if (*columns < OS_TERMINAL_MIN_COLUMNS) *columns = OS_TERMINAL_MIN_COLUMNS;
    if (*columns > OS_TERMINAL_MAX_COLUMNS) *columns = OS_TERMINAL_MAX_COLUMNS;
    if (*rows < OS_TERMINAL_MIN_ROWS) *rows = OS_TERMINAL_MIN_ROWS;
    if (*rows > OS_TERMINAL_MAX_ROWS) *rows = OS_TERMINAL_MAX_ROWS;
}

static long send_control(uint32_t command,
                         const uint8_t* bytes,
                         uint32_t length,
                         uint32_t columns,
                         uint32_t rows) {
    OsTerminalPacket packet;
    os_terminal_packet_init(&packet, command);
    packet.sequence = next_sequence();
    packet.columns = columns;
    packet.rows = rows;
    packet.length = length;
    if (length != 0) os_memcpy(packet.data, bytes, length);
    return os_terminal_send(shell_identity, &packet, 20);
}

static long send_resize(const OsWindow* window) {
    uint32_t columns;
    uint32_t rows;
    dimensions_for_window(window, &columns, &rows);
    long result = os_terminal_model_resize(&terminal_model, columns, rows);
    if (result < 0) return result;
    result = send_control(OS_TERMINAL_COMMAND_RESIZE, 0, 0, columns, rows);
    if (result >= 0)
        os_printf("[terminal] resized columns=%u rows=%u surface=%ux%u\n",
                  columns, rows, window->surface_info.width,
                  window->surface_info.height);
    return result;
}

static long paint(OsWindow* window) {
    OsSurfaceCanvas canvas;
    long result = os_surface_canvas_init(&canvas, window->pixels,
                                         &window->surface_info);
    if (result < 0) return result;
    OsRect bounds = {
        TERMINAL_MARGIN, TERMINAL_MARGIN,
        (int32_t)canvas.width - TERMINAL_MARGIN * 2,
        (int32_t)canvas.height - TERMINAL_MARGIN - TERMINAL_BOTTOM_MARGIN,
    };
    result = os_surface_canvas_fill_rect(
        &canvas, (OsRect){0, 0, (int32_t)canvas.width, (int32_t)canvas.height},
        OS_RGB(18, 20, 25));
    if (result < 0) return result;
    return os_terminal_model_render(&terminal_model, &canvas, bounds);
}

static void observe_marker(const uint8_t* bytes, uint32_t length) {
    for (uint32_t i = 0; i < length; i++) {
        for (uint32_t marker_index = 0;
             marker_index < sizeof(transcript_markers) /
                            sizeof(transcript_markers[0]);
             marker_index++) {
            TerminalTranscriptMarker* marker = &transcript_markers[marker_index];
            if (bytes[i] == (uint8_t)marker->text[marker->progress]) {
                marker->progress++;
                if (marker->text[marker->progress] == '\0') {
                    os_printf("[terminal] transcript marker=%s\n", marker->name);
                    marker->progress = 0;
                }
            } else {
                marker->progress = bytes[i] == (uint8_t)marker->text[0]
                    ? 1u : 0u;
            }
        }
    }
}

static int drain_shell_output(void) {
    int changed = 0;
    for (uint32_t count = 0; count < 64; count++) {
        OsTerminalPacket packet;
        long result = os_terminal_session_read(shell_identity, &packet);
        if (result == OS_ERR_WOULD_BLOCK) break;
        if (result < 0) return (int)result;
        if (os_terminal_packet_validate(&packet) < 0 ||
            packet.sequence <= shell_sequence) continue;
        shell_sequence = packet.sequence;
        if (packet.command == OS_TERMINAL_COMMAND_OUTPUT) {
            if (packet.flags & OS_TERMINAL_FLAG_OVERFLOW) {
                static const char warning[] = "\n[terminal output truncated]\n";
                os_terminal_model_write(&terminal_model, warning,
                                        sizeof(warning) - 1u);
                output_overflows++;
            }
            os_terminal_model_write(&terminal_model, packet.data, packet.length);
            observe_marker(packet.data, packet.length);
            output_bytes += packet.length;
            changed = 1;
        } else if (packet.command == OS_TERMINAL_COMMAND_EXIT) {
            child_exited = 1;
            child_status = packet.status;
            os_printf("[terminal] child exit status=%d output=%u overflow=%u\n",
                      child_status, output_bytes, output_overflows);
        }
    }
    return changed;
}

static int permission_boundary_ok(void) {
    OsInputEvent input;
    OsGraphicsInfo display;
    return os_input_poll(&input) == OS_ERR_PERMISSION_DENIED &&
           os_gfx_get_info(&display) == OS_ERR_PERMISSION_DENIED;
}

static void terminate_child(void) {
    int forced = 0;
    if (shell_identity.pid != 0 && os_process_identity_alive(shell_identity) >= 0) {
        (void)send_control(OS_TERMINAL_COMMAND_HANGUP, 0, 0,
                           terminal_model.columns, terminal_model.rows);
        uint32_t start = (uint32_t)os_time_ticks();
        while ((uint32_t)(os_time_ticks() - start) < 200u &&
               os_process_identity_alive(shell_identity) >= 0) {
            (void)drain_shell_output();
            os_sleep(1);
        }
        (void)drain_shell_output();
        if (os_process_identity_alive(shell_identity) >= 0) {
            forced = 1;
            (void)os_kill(shell_identity.pid);
        }
    }
    long reaped = os_reap_children();
    long session_closed = os_terminal_session_close(shell_identity);
    os_printf("[terminal] child reaped=%ld forced=%d cleanup OK\n",
              reaped, forced);
    if (session_closed < 0 && session_closed != OS_ERR_NO_TARGET)
        os_printf("[terminal] session close failed %ld\n", session_closed);
}

int main(int argc, char** argv) {
    fault_shortcut_enabled = argc == 2 &&
        os_streq(argv[1], "--fault-test");
    if (!permission_boundary_ok()) {
        os_puts("[terminal] permission boundary failed");
        return 1;
    }
    long result = launch_shell();
    if (result < 0) {
        os_printf("[terminal] shell launch failed %ld\n", result);
        return 1;
    }
    if (os_terminal_model_init(&terminal_model, 117, 52) < 0) {
        terminate_child();
        return 1;
    }
    OsWindow window;
    result = os_window_create(&window, 160, 100, 720, 440);
    if (result < 0) {
        terminate_child();
        return 1;
    }
    long resize_result = send_resize(&window);
    int drain_result = resize_result >= 0 ? drain_shell_output() : 0;
    long paint_result = resize_result >= 0 && drain_result >= 0
        ? paint(&window) : OS_ERR_NOT_READY;
    long damage_result = paint_result >= 0
        ? os_window_damage_all(&window) : OS_ERR_NOT_READY;
    long focus_result = damage_result >= 0
        ? os_window_focus(&window) : OS_ERR_NOT_READY;
    if (resize_result < 0 || drain_result < 0 || paint_result < 0 ||
        damage_result < 0 || focus_result < 0) {
        os_printf("[terminal] bring-up failed resize=%ld drain=%d paint=%ld damage=%ld focus=%ld\n",
                  resize_result, drain_result, paint_result, damage_result,
                  focus_result);
        os_window_destroy(&window);
        terminate_child();
        return 1;
    }
    os_printf("[terminal] ready child=%u:%u columns=%u rows=%u\n",
              shell_identity.pid, shell_identity.generation,
              terminal_model.columns, terminal_model.rows);

    int closing = 0;
    while (!closing && !child_exited) {
        int changed = drain_shell_output();
        if (changed && (paint(&window) < 0 || os_window_damage_all(&window) < 0))
            break;
        OsWindowEvent event;
        result = os_window_wait_event(&window, &event, 1);
        if (result == OS_ERR_TIMEOUT) {
            if (os_process_identity_alive(shell_identity) < 0 && !child_exited) {
                child_exited = 1;
                child_status = OS_ERR_CANCELLED;
                os_puts("[terminal] child lost status=-18");
            }
            continue;
        }
        if (result < 0) break;
        if (event.command == OS_WINDOW_EVENT_CLOSE_REQUEST) {
            closing = 1;
        } else if (event.command == OS_WINDOW_EVENT_CONFIGURE) {
            if (os_window_apply_configure(&window) < 0 ||
                send_resize(&window) < 0 || paint(&window) < 0 ||
                os_window_damage_all(&window) < 0) break;
        } else if (event.command == OS_WINDOW_EVENT_KEY &&
                   event.input.type == OS_INPUT_EVENT_KEY &&
                   event.input.data.key.type == OS_KEY_EVENT_DOWN) {
            OsKeyEvent key = event.input.data.key;
            if ((key.modifiers & (OS_KEY_MOD_CTRL | OS_KEY_MOD_SHIFT)) ==
                    (OS_KEY_MOD_CTRL | OS_KEY_MOD_SHIFT) &&
                (key.character == 'q' || key.character == 'Q')) {
                closing = 1;
            } else if (fault_shortcut_enabled &&
                       (key.modifiers & (OS_KEY_MOD_CTRL | OS_KEY_MOD_SHIFT)) ==
                           (OS_KEY_MOD_CTRL | OS_KEY_MOD_SHIFT) &&
                       (key.character == 'k' || key.character == 'K')) {
                (void)os_kill(shell_identity.pid);
            } else if ((key.modifiers & OS_KEY_MOD_CTRL) &&
                       key.keycode == OS_KEY_UP) {
                os_terminal_model_scroll(&terminal_model,
                                         (int32_t)terminal_model.rows / 2);
                paint(&window); os_window_damage_all(&window);
            } else if ((key.modifiers & OS_KEY_MOD_CTRL) &&
                       key.keycode == OS_KEY_DOWN) {
                os_terminal_model_scroll(&terminal_model,
                                         -(int32_t)terminal_model.rows / 2);
                paint(&window); os_window_damage_all(&window);
            } else {
                uint8_t byte = (uint8_t)key.character;
                if ((key.modifiers & OS_KEY_MOD_CTRL) &&
                    (byte == 'c' || byte == 'C')) byte = 3;
                if ((key.modifiers & OS_KEY_MOD_CTRL) &&
                    (byte == 'd' || byte == 'D')) byte = 4;
                if (byte != 0 && send_control(OS_TERMINAL_COMMAND_INPUT,
                                              &byte, 1,
                                              terminal_model.columns,
                                              terminal_model.rows) < 0) {
                    static const char warning[] = "\n[terminal input busy]\n";
                    os_terminal_model_write(&terminal_model, warning,
                                            sizeof(warning) - 1u);
                    paint(&window); os_window_damage_all(&window);
                }
            }
        }
    }
    (void)drain_shell_output();
    os_window_destroy(&window);
    terminate_child();
    return child_exited && child_status == 0 ? 0 : (closing ? 0 : 1);
}
