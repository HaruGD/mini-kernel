#!/usr/bin/env python3
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

HARNESS = r'''
#include <stdint.h>
#include <stdio.h>
#include <os64/os64.h>

static int failures;
static uint32_t ticks;
static uint32_t send_attempts;
static uint32_t draw_calls;

static void check(int condition, const char* name) {
    if (!condition) { fprintf(stderr, "FAIL: %s\n", name); failures++; }
}

void* os_memset(void* out, int value, size_t size) {
    uint8_t* bytes = out;
    for (size_t i = 0; i < size; i++) bytes[i] = (uint8_t)value;
    return out;
}
void* os_memcpy(void* out, const void* in, size_t size) {
    uint8_t* target = out; const uint8_t* source = in;
    for (size_t i = 0; i < size; i++) target[i] = source[i];
    return out;
}
uint64_t os_time_ticks(void) { return ticks; }
long os_sleep(uint32_t amount) { ticks += amount ? amount : 1; return 0; }
void os_msg_v2_init(OsIpcMessageV2* message, uint32_t type) {
    os_memset(message, 0, sizeof(*message));
    message->size = sizeof(*message);
    message->abi_version = OS64_IPC_ABI_VERSION_V2;
    message->type = type;
}
long os_msg_v2_send_to_identity(OsProcessIdentity peer,
                                const OsIpcMessageV2* message) {
    check(peer.pid == 7 && peer.generation == 9, "send identity");
    check(message->type == OS_IPC_MESSAGE_EVENT &&
          message->length == sizeof(OsTerminalPacket), "send envelope");
    send_attempts++;
    return send_attempts < 3 ? OS_ERR_QUEUE_FULL : OS_SUCCESS;
}
long os_surface_canvas_fill_rect(OsSurfaceCanvas* canvas, OsRect rect,
                                 uint32_t color) {
    (void)canvas; (void)rect; (void)color; draw_calls++; return OS_SUCCESS;
}
long os_surface_canvas_draw_text(OsSurfaceCanvas* canvas, int32_t x, int32_t y,
                                 const char* text, uint32_t foreground,
                                 uint32_t background, uint32_t flags) {
    (void)canvas; (void)x; (void)y; (void)text; (void)foreground;
    (void)background; (void)flags; draw_calls++; return OS_SUCCESS;
}
long os_surface_canvas_draw_line(OsSurfaceCanvas* canvas, int32_t x0, int32_t y0,
                                 int32_t x1, int32_t y1, uint32_t color) {
    (void)canvas; (void)x0; (void)y0; (void)x1; (void)y1; (void)color;
    draw_calls++; return OS_SUCCESS;
}

int main(void) {
    OsTerminalPacket packet;
    os_terminal_packet_init(&packet, OS_TERMINAL_COMMAND_HELLO);
    packet.sequence = 1; packet.columns = 80; packet.rows = 24;
    check(os_terminal_packet_validate(&packet) == OS_SUCCESS, "hello packet");
    packet.length = 1;
    check(os_terminal_packet_validate(&packet) == OS_ERR_BAD_BUFFER,
          "hello rejects payload");
    os_terminal_packet_init(&packet, OS_TERMINAL_COMMAND_OUTPUT);
    packet.sequence = 2; packet.length = 1; packet.data[0] = 'x';
    check(os_terminal_packet_validate(&packet) == OS_SUCCESS, "output packet");
    check(os_terminal_send((OsProcessIdentity){7, 9}, &packet, 4) == OS_SUCCESS &&
          send_attempts == 3 && ticks == 2, "bounded queue retry");

    static OsTerminalModel model;
    check(os_terminal_model_init(&model, 20, 5) == OS_SUCCESS, "model init");
    check(os_terminal_model_init(&model, 19, 5) == OS_ERR_INVALID_ARGUMENT,
          "minimum dimensions");
    check(os_terminal_model_init(&model, 20, 5) == OS_SUCCESS, "model reset");
    check(os_terminal_model_write(&model, "a\nb\bC", 5) == OS_SUCCESS &&
          model.cells[0][0].codepoint == 'a' &&
          model.cells[1][0].codepoint == 'C', "newline and backspace");
    uint32_t normal = model.foreground;
    const char red[] = "\x1b[31mR";
    check(os_terminal_model_write(&model, red, sizeof(red) - 1) == OS_SUCCESS &&
          model.cells[1][1].codepoint == 'R' &&
          model.cells[1][1].foreground != normal, "ANSI SGR color");
    const char clear[] = "\x1b[2J\x1b[2;3HX";
    check(os_terminal_model_write(&model, clear, sizeof(clear) - 1) == OS_SUCCESS &&
          model.history_count == 2 && model.cells[1][2].codepoint == 'X',
          "ANSI clear and cursor position");
    check(os_terminal_model_resize(&model, 30, 8) == OS_SUCCESS &&
          model.columns == 30 && model.rows == 8, "responsive model resize");
    for (uint32_t i = 0; i < 140; i++)
        os_terminal_model_write(&model, "line\n", 5);
    check(model.history_count == OS_TERMINAL_HISTORY_ROWS &&
          model.dropped_history_rows > 0, "bounded scrollback");
    check(os_terminal_model_scroll(&model, 20) == OS_SUCCESS &&
          model.view_offset == 20, "scrollback navigation");
    check(os_terminal_model_scroll(&model, -100) == OS_SUCCESS &&
          model.view_offset == 0, "scrollback return to tail");

    static uint32_t pixels[320 * 120];
    OsSurfaceCanvas canvas = {pixels, 320, 120, 320, OS64_PIXEL_FORMAT_BGR};
    check(os_terminal_model_render(&model, &canvas,
                                   (OsRect){0, 0, 300, 80}) == OS_SUCCESS &&
          draw_calls > 1, "model render and cursor");
    return failures == 0 ? 0 : 1;
}
'''


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="os64_terminal_model_") as temp:
        temp_path = Path(temp)
        source = temp_path / "terminal_model_test.c"
        binary = temp_path / "terminal_model_test"
        source.write_text(HARNESS, encoding="utf-8")
        subprocess.run([
            "gcc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
            "-I", str(ROOT / "user/sdk/include"),
            str(ROOT / "user/sdk/src/terminal.c"),
            str(ROOT / "user/sdk/src/syscall.c"), str(source),
            "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)

    terminal = (ROOT / "user/programs/terminal.c").read_text(encoding="utf-8")
    shell = (ROOT / "user/programs/terminal_shell.c").read_text(encoding="utf-8")
    required_terminal = [
        "os_window_create", "os_window_apply_configure", "os_terminal_model_render",
        "OS_TERMINAL_COMMAND_RESIZE", "OS_TERMINAL_COMMAND_HANGUP",
        "os_reap_children",
    ]
    required_shell = [
        '"ushell/ushell_main.inc"', "os_terminal_session_bind",
        "os_terminal_session_exit", "terminal_read_line",
    ]
    if any(token not in terminal for token in required_terminal):
        raise RuntimeError("terminal frontend contract is incomplete")
    if any(token not in shell for token in required_shell):
        raise RuntimeError("terminal shell stream contract is incomplete")
    kernel_session = (ROOT / "kernel/process/process_terminal.cpp").read_text(
        encoding="utf-8")
    required_session = [
        "process_terminal_inherit", "PROCESS_TERMINAL_OUTPUT_CAPACITY",
        "process_terminal_read_output", "process_terminal_notify_message",
        "process_terminal_close",
    ]
    if any(token not in kernel_session for token in required_session):
        raise RuntimeError("kernel terminal inheritance contract is incomplete")
    print("terminal packet/model/ANSI/scrollback tests OK")
    print("terminal frontend/shell/session lifecycle source checks OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
