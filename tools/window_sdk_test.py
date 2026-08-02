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
static uint32_t next_request = 10;
static uint64_t ticks;
static uint32_t display_request;
static int display_reply_ready;
static OsIpcMessageV2 reply_message;
static int reply_ready;
static int unrelated_reply_pending;
static int bad_reply;
static OsIpcMessageV2 event_message;
static int event_ready;
static uint32_t sent_commands[32];
static uint32_t sent_count;
static uint32_t damage_content;
static uint32_t damage_rects;
static uint32_t damage_chunks;
static uint32_t filter_checks;
static uint64_t next_handle = 1;
static int32_t server_x;
static int32_t server_y;
static uint32_t server_width;
static uint32_t server_height;
static uint32_t server_stride;
static uint32_t server_format = OS64_PIXEL_FORMAT_BGR;
static uint32_t server_content;
static uint32_t server_visible;
static uint32_t server_focused;

typedef struct FakeSurface {
    int active;
    OsGraphicsSurfaceHandleInfo info;
    uint32_t pixels[500 * 500];
} FakeSurface;

static FakeSurface surfaces[12];

static void check(int condition, const char* name) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", name);
        failures++;
    }
}

void* os_memset(void* destination, int value, size_t size) {
    uint8_t* bytes = destination;
    for (size_t i = 0; i < size; i++) bytes[i] = (uint8_t)value;
    return destination;
}

void* os_memcpy(void* destination, const void* source, size_t size) {
    uint8_t* out = destination;
    const uint8_t* in = source;
    for (size_t i = 0; i < size; i++) out[i] = in[i];
    return destination;
}

static int text_equal(const char* left, const char* right) {
    uint32_t i = 0;
    while (left[i] != 0 && right[i] != 0 && left[i] == right[i]) i++;
    return left[i] == right[i];
}

long os_service_find_owner_identity(const char* name, OsProcessIdentity* identity) {
    if (name == 0 || identity == 0) return OS_ERR_INVALID_ARGUMENT;
    if (text_equal(name, "window")) {
        identity->pid = 50; identity->generation = 7; return OS_SUCCESS;
    }
    if (text_equal(name, "display")) {
        identity->pid = 40; identity->generation = 6; return OS_SUCCESS;
    }
    return OS_ERR_NOT_FOUND;
}

void os_msg_init(OsIpcMessage* message, uint32_t type) {
    os_memset(message, 0, sizeof(*message));
    message->size = sizeof(*message);
    message->type = type;
}

void os_msg_v2_init(OsIpcMessageV2* message, uint32_t type) {
    os_memset(message, 0, sizeof(*message));
    message->size = sizeof(*message);
    message->abi_version = OS64_IPC_ABI_VERSION_V2;
    message->type = type;
}

void os_ipc_filter_init(OsIpcReceiveFilter* filter) {
    os_memset(filter, 0, sizeof(*filter));
    filter->size = sizeof(*filter);
}

uint32_t os_msg_next_request_id(void) { return next_request++; }
uint64_t os_time_ticks(void) { return ticks; }
long os_sleep(uint32_t amount) { ticks += amount == 0 ? 1 : amount; return 0; }

long os_msg_send_to_identity(OsProcessIdentity target, const OsIpcMessage* message) {
    check(target.pid == 40 && target.generation == 6, "display identity");
    OsServiceQueryRequest request;
    os_memcpy(&request, message->payload, sizeof(request));
    display_request = request.request_id;
    display_reply_ready = 1;
    return OS_SUCCESS;
}

long os_msg_recv(OsIpcMessage* message) {
    if (!display_reply_ready) return OS_ERR_WOULD_BLOCK;
    display_reply_ready = 0;
    os_msg_init(message, OS_IPC_MESSAGE_REPLY);
    message->sender_pid = 40;
    message->sender_generation = 6;
    OsDisplayServiceInfoReply reply;
    os_memset(&reply, 0, sizeof(reply));
    reply.size = sizeof(reply);
    reply.command = OS_SERVICE_QUERY_DISPLAY_INFO;
    reply.result = OS_SUCCESS;
    reply.request_id = display_request;
    reply.abi_version = OS64_SERVICE_PROTOCOL_ABI_VERSION;
    reply.ready = 1;
    reply.width = 800;
    reply.height = 600;
    reply.pixels_per_scanline = 800;
    reply.format = OS64_PIXEL_FORMAT_BGR;
    message->length = sizeof(reply);
    os_memcpy(message->payload, &reply, sizeof(reply));
    return OS_SUCCESS;
}

OsHandle os_surface_create(uint32_t width, uint32_t height, uint32_t format) {
    if (width == 0 || height == 0 || width > 500 || height > 500) return 0;
    uint64_t handle = next_handle++;
    FakeSurface* surface = &surfaces[handle];
    os_memset(surface, 0, sizeof(*surface));
    surface->active = 1;
    surface->info.owner_pid = 1;
    surface->info.generation = (uint32_t)handle;
    surface->info.width = width;
    surface->info.height = height;
    surface->info.stride_pixels = width;
    surface->info.pixel_format = format;
    surface->info.byte_size = width * height * 4;
    surface->info.ref_count = 1;
    return handle;
}

long os_surface_get_info(OsHandle handle, OsGraphicsSurfaceHandleInfo* info) {
    if (handle >= 12 || !surfaces[handle].active || info == 0)
        return OS_ERR_NOT_FOUND;
    *info = surfaces[handle].info;
    return OS_SUCCESS;
}

void* os_surface_map(OsHandle handle, uint32_t flags) {
    if (handle >= 12 || !surfaces[handle].active ||
        flags != (OS_SURFACE_MAP_READ | OS_SURFACE_MAP_WRITE)) return 0;
    return surfaces[handle].pixels;
}

long os_surface_unmap(OsHandle handle, void* address) {
    return handle < 12 && surfaces[handle].active &&
           address == surfaces[handle].pixels ? OS_SUCCESS : OS_ERR_NOT_FOUND;
}

long os_surface_close(OsHandle handle) {
    if (handle >= 12 || !surfaces[handle].active) return OS_ERR_NOT_FOUND;
    surfaces[handle].active = 0;
    return OS_SUCCESS;
}

static void queue_reply(uint32_t request_id, uint32_t operation,
                        uint32_t content_generation) {
    os_msg_v2_init(&reply_message, OS_IPC_MESSAGE_REPLY);
    reply_message.sender_pid = 50;
    reply_message.sender_generation = 7;
    reply_message.reply_to = request_id;
    OsWindowReply reply;
    os_memset(&reply, 0, sizeof(reply));
    reply.size = sizeof(reply);
    reply.abi_version = OS64_WINDOW_ABI_VERSION;
    reply.command = OS_WINDOW_REPLY;
    reply.result = OS_SUCCESS;
    reply.request_id = request_id;
    reply.operation = bad_reply ? OS_WINDOW_CREATE : operation;
    reply.window_id = 3;
    reply.window_generation = 9;
    reply.accepted_content_generation = content_generation;
    reply_message.length = sizeof(reply);
    os_memcpy(reply_message.payload, &reply, sizeof(reply));
    reply_ready = 1;
}

static void queue_info_reply(uint32_t request_id, uint32_t window_id) {
    os_msg_v2_init(&reply_message, OS_IPC_MESSAGE_REPLY);
    reply_message.sender_pid = 50;
    reply_message.sender_generation = 7;
    reply_message.reply_to = request_id;
    OsWindowInfoReply reply;
    os_memset(&reply, 0, sizeof(reply));
    reply.size = sizeof(reply);
    reply.abi_version = OS64_WINDOW_ABI_VERSION;
    reply.command = OS_WINDOW_GET_INFO;
    reply.result = OS_SUCCESS;
    reply.request_id = request_id;
    reply.capacity = OS_WINDOW_MAX_WINDOWS;
    if (window_id == 0) {
        reply.width = 800;
        reply.height = 600;
        reply.stride_pixels = 800;
        reply.pixel_format = OS64_PIXEL_FORMAT_BGR;
    } else {
        reply.window_id = 3;
        reply.window_generation = 9;
        reply.x = server_x;
        reply.y = server_y;
        reply.width = server_width;
        reply.height = server_height;
        reply.stride_pixels = server_stride;
        reply.pixel_format = server_format;
        reply.content_generation = server_content;
        reply.visible = server_visible;
        reply.focused = server_focused;
    }
    reply_message.length = sizeof(reply);
    os_memcpy(reply_message.payload, &reply, sizeof(reply));
    reply_ready = 1;
}

long os_msg_v2_send_to_identity(OsProcessIdentity target,
                                const OsIpcMessageV2* message) {
    check(target.pid == 50 && target.generation == 7, "window identity");
    const uint32_t* words = (const uint32_t*)message->payload;
    uint32_t command = message->length >= 12 ? words[2] : 0;
    if (sent_count < 32) sent_commands[sent_count++] = command;
    if (command == OS_WINDOW_GET_INFO) {
        queue_info_reply(message->request_id, words[5]);
    } else if (command == OS_WINDOW_CREATE) {
        server_x = (int32_t)words[6];
        server_y = (int32_t)words[7];
        server_width = words[8];
        server_height = words[9];
        server_stride = words[10];
        server_format = words[11];
        server_content = words[5];
        server_visible = 1;
        unrelated_reply_pending = 1;
        queue_reply(message->request_id, command, words[5]);
    } else if (command == OS_WINDOW_SET_SURFACE || command == OS_WINDOW_RESIZE) {
        server_content = words[7];
        server_width = words[8];
        server_height = words[9];
        server_stride = words[10];
        server_format = words[11];
        queue_reply(message->request_id, command, words[7]);
    } else if (command == OS_WINDOW_DAMAGE_BEGIN) {
        damage_content = words[7];
        damage_rects = words[9];
        damage_chunks = words[10];
    } else if (command == OS_WINDOW_DAMAGE_COMMIT) {
        server_content = damage_content;
        queue_reply(message->request_id, OS_WINDOW_DAMAGE, damage_content);
    } else if (command == OS_WINDOW_DAMAGE_RECTS) {
        check(words[7] > 0 && words[7] <= 4, "chunk rect count");
    } else {
        if (command == OS_WINDOW_MOVE) {
            server_x = (int32_t)words[7];
            server_y = (int32_t)words[8];
        } else if (command == OS_WINDOW_SHOW) {
            server_visible = 1;
        } else if (command == OS_WINDOW_HIDE) {
            server_visible = 0;
            server_focused = 0;
        } else if (command == OS_WINDOW_FOCUS) {
            server_focused = 1;
        }
        queue_reply(message->request_id, command, 0);
    }
    return OS_SUCCESS;
}

long os_msg_v2_recv_match(const OsIpcReceiveFilter* filter,
                          OsIpcMessageV2* message) {
    check(filter != 0 && message != 0, "receive arguments");
    if (reply_ready && filter->type == OS_IPC_MESSAGE_REPLY &&
        filter->reply_to == reply_message.reply_to) {
        check(filter->flags == (OS_IPC_FILTER_SENDER | OS_IPC_FILTER_TYPE |
                                OS_IPC_FILTER_REPLY_TO), "reply filter flags");
        check(filter->sender_pid == 50 && filter->sender_generation == 7,
              "reply exact identity");
        *message = reply_message;
        reply_ready = 0;
        filter_checks++;
        return OS_SUCCESS;
    }
    if (event_ready && filter->type == OS_IPC_MESSAGE_EVENT) {
        *message = event_message;
        event_ready = 0;
        return OS_SUCCESS;
    }
    return OS_ERR_WOULD_BLOCK;
}

static void queue_event(const OsWindow* window, uint32_t command,
                        uint32_t sequence, uint32_t window_id) {
    os_msg_v2_init(&event_message, OS_IPC_MESSAGE_EVENT);
    event_message.sender_pid = 50;
    event_message.sender_generation = 7;
    OsWindowEvent event;
    os_memset(&event, 0, sizeof(event));
    event.size = sizeof(event);
    event.abi_version = OS64_WINDOW_ABI_VERSION;
    event.command = command;
    event.event_sequence = sequence;
    event.window_id = window_id;
    event.window_generation = window->window_generation;
    event.input.size = sizeof(event.input);
    event_message.length = sizeof(event);
    os_memcpy(event_message.payload, &event, sizeof(event));
    event_ready = 1;
}

int main(void) {
    check(sizeof(OsWindowInfo) == 56, "public info ABI");
    check(offsetof(OsWindowInfo, focused) == 48, "public info offset");
    check(os_window_create(0, 0, 0, 10, 10) == OS_ERR_INVALID_ARGUMENT,
          "null create");
    OsWindow window;
    check(os_window_create(&window, 10, 20, 100, 80) == OS_SUCCESS,
          "create through SDK");
    check(window.window_id == 3 && window.window_generation == 9 &&
          window.content_generation == 1, "create reply state");
    check(unrelated_reply_pending == 1 && filter_checks == 2,
          "correlated reply preserves unrelated traffic");

    OsWindowInfo info;
    check(os_window_get_info(&window, &info) == OS_SUCCESS &&
          info.x == 10 && info.y == 20 && info.width == 100 &&
          info.height == 80, "local information");
    check(os_window_damage(&window, 0, 1) == OS_ERR_INVALID_ARGUMENT,
          "null damage");
    OsRect invalid = {0, 0, 0, 5};
    check(os_window_damage(&window, &invalid, 1) == OS_ERR_INVALID_ARGUMENT,
          "invalid damage rect");
    OsRect rects[6] = {
        {0, 0, 10, 10}, {10, 0, 10, 10}, {20, 0, 10, 10},
        {30, 0, 10, 10}, {40, 0, 10, 10}, {50, 0, 10, 10},
    };
    uint32_t before = sent_count;
    check(os_window_damage(&window, rects, 6) == OS_SUCCESS,
          "chunked damage");
    check(sent_count - before == 4 &&
          sent_commands[before] == OS_WINDOW_DAMAGE_BEGIN &&
          sent_commands[before + 1] == OS_WINDOW_DAMAGE_RECTS &&
          sent_commands[before + 2] == OS_WINDOW_DAMAGE_RECTS &&
          sent_commands[before + 3] == OS_WINDOW_DAMAGE_COMMIT &&
          damage_rects == 6 && damage_chunks == 2,
          "SDK hides chunk transport");

    check(os_window_move(&window, -5, 33) == OS_SUCCESS &&
          window.x == -5 && window.y == 33, "move state");
    bad_reply = 1;
    check(os_window_move(&window, 77, 88) == OS_ERR_BAD_BUFFER &&
          window.x == -5 && window.y == 33, "malformed correlated reply");
    bad_reply = 0;

    queue_event(&window, OS_WINDOW_EVENT_FOCUS_IN, 7, window.window_id);
    OsWindowEvent event;
    check(os_window_poll_event(&window, &event) == OS_SUCCESS &&
          event.event_sequence == 7 && window.focused == 1,
          "unexpected event preserved for event API");
    queue_event(&window, OS_WINDOW_EVENT_KEY, 8, window.window_id + 1);
    check(os_window_poll_event(&window, &event) == OS_ERR_BAD_BUFFER,
          "wrong window event rejected");

    check(os_window_replace_surface(&window) == OS_SUCCESS,
          "surface replacement");
    check(os_window_resize(&window, 140, 90) == OS_SUCCESS,
          "surface resize");
    check(os_window_get_info(&window, &info) == OS_SUCCESS &&
          info.width == 140 && info.height == 90,
          "resized information");
    check(os_window_hide(&window) == OS_SUCCESS && window.visible == 0,
          "hide");
    check(os_window_show(&window) == OS_SUCCESS && window.visible == 1,
          "show");
    check(os_window_focus(&window) == OS_SUCCESS, "focus");
    check(os_window_destroy(&window) == OS_SUCCESS && window.window_id == 0,
          "destroy cleanup");
    check(os_window_create(&window, 5, 6, 40, 30) == OS_SUCCESS,
          "create for local abandon");
    before = sent_count;
    os_window_abandon(&window);
    check(window.window_id == 0 && window.surface == 0 &&
          sent_count == before, "server-loss local abandon");

    uint32_t pixels[64 * 48];
    OsGraphicsSurfaceHandleInfo canvas_info;
    os_memset(&canvas_info, 0, sizeof(canvas_info));
    canvas_info.width = 64;
    canvas_info.height = 48;
    canvas_info.stride_pixels = 64;
    canvas_info.pixel_format = OS64_PIXEL_FORMAT_BGR;
    OsSurfaceCanvas canvas;
    check(os_surface_canvas_init(&canvas, pixels, &canvas_info) == OS_SUCCESS,
          "canvas init");
    os_memset(pixels, 0, sizeof(pixels));
    check(os_surface_canvas_fill_rect(&canvas, (OsRect){-2, -2, 5, 5},
                                      OS_RGB(1, 2, 3)) == OS_SUCCESS &&
          pixels[0] == OS_RGB(1, 2, 3) && pixels[3] == 0,
          "canvas clipped fill and logical color");
    check(os_surface_canvas_draw_line(&canvas, 0, 47, 63, 47,
                                      OS_RGB(4, 5, 6)) == OS_SUCCESS &&
          pixels[47 * 64 + 63] == OS_RGB(4, 5, 6), "canvas line");
    check(os_surface_canvas_draw_line(&canvas, INT32_MIN, 24, INT32_MAX, 24,
                                      OS_RGB(10, 11, 12)) == OS_SUCCESS &&
          pixels[24 * 64] == OS_RGB(10, 11, 12) &&
          pixels[24 * 64 + 63] == OS_RGB(10, 11, 12),
          "canvas extreme line clipping");
    check(os_surface_canvas_draw_text(&canvas, 4, 4, "OS64",
                                      OS_RGB(7, 8, 9), 0,
                                      OS_SURFACE_TEXT_TRANSPARENT_BG) == OS_SUCCESS,
          "canvas text");
    uint32_t text_pixels = 0;
    for (uint32_t i = 0; i < 64 * 48; i++) {
        if (pixels[i] == OS_RGB(7, 8, 9)) text_pixels++;
    }
    check(text_pixels > 20, "canvas text pixels");
    check(os_surface_canvas_fill_rect(&canvas, (OsRect){0, 0, 0, 1}, 0) ==
          OS_ERR_INVALID_ARGUMENT, "canvas negative case");

    if (failures != 0) return 1;
    puts("window SDK ABI/transport/canvas tests OK");
    return 0;
}
'''


def require_sources() -> None:
    app = (ROOT / "user/programs/ugui_c.c").read_text(encoding="utf-8")
    launcher = (ROOT / "user/programs/ugui_launch_c.c").read_text(encoding="utf-8")
    windowd = (ROOT / "user/programs/windowd_c.c").read_text(encoding="utf-8")
    protocol = (ROOT / "user/programs/windowd/window_protocol.c").read_text(
        encoding="utf-8")
    forbidden = ("os_msg_", "os_surface_create", "os_service_find", "os_syscall")
    if any(marker in app for marker in forbidden):
        raise RuntimeError("GUI app bypasses the public Window SDK")
    required = (
        "os_window_create", "os_window_replace_surface", "os_window_damage",
        "os_window_resize", "os_window_wait_event", "os_window_destroy",
        "os_surface_canvas_draw_text",
    )
    if any(marker not in app for marker in required):
        raise RuntimeError("GUI app does not exercise the complete public SDK")
    if "OS_PROCESS_PERMISSION_PROFILE_GUI_APPLICATION" not in launcher:
        raise RuntimeError("GUI launcher does not use the restricted profile")
    if "OS_PROCESS_PERMISSION_INPUT" in launcher or "OS_PROCESS_PERMISSION_DISPLAY" in launcher:
        raise RuntimeError("GUI launcher grants raw input or display authority")
    if "handle_info" not in windowd or "OS_WINDOW_GET_INFO" not in windowd or \
       "window_protocol_validate_info" not in protocol:
        raise RuntimeError("correlated server information path is incomplete")


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="os64_window_sdk_") as directory:
        source = Path(directory) / "window_sdk_test.c"
        binary = Path(directory) / "window_sdk_test"
        source.write_text(HARNESS, encoding="utf-8")
        subprocess.run([
            "gcc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
            "-I", str(ROOT / "include"),
            "-I", str(ROOT / "user/sdk/include"),
            str(source),
            str(ROOT / "user/sdk/src/window.c"),
            str(ROOT / "user/sdk/src/surface_draw.c"),
            str(ROOT / "user/sdk/src/text.c"),
            "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)
    require_sources()
    print("first GUI application public-SDK/permission source checks OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
