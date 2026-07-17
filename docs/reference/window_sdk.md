# Window SDK

The public Window SDK is the application boundary for creating, drawing, and
receiving input from windows. Applications include `<os64/os64.h>` and use the
`OsWindow` context; they do not discover `displayd`, consume raw input, or send
the chunked window protocol themselves.

The client ABI version is `OS64_WINDOW_CLIENT_ABI_VERSION == 1`. The wire
protocol remains `OS64_WINDOW_ABI_VERSION == 1` in
`include/os64/window_types.h`.

## Lifecycle

```c
#include <os64/os64.h>

OsWindow window;
long result = os_window_create(&window, 120, 100, 360, 240);
if (result < 0) return 1;

OsSurfaceCanvas canvas;
os_surface_canvas_init(&canvas, window.pixels, &window.surface_info);
os_surface_canvas_fill_rect(
    &canvas, (OsRect){0, 0, 360, 240}, OS_RGB(24, 36, 61));
os_window_damage_all(&window);

os_window_focus(&window);
OsWindowEvent event;
while (os_window_wait_event(&window, &event, 0) == OS_SUCCESS) {
    if (event.command == OS_WINDOW_EVENT_KEY &&
        event.input.data.key.keycode == OS_KEY_ESCAPE) {
        break;
    }
}

os_window_destroy(&window);
```

`os_window_create` initializes the context, discovers the exact registered
window-service PID plus generation, queries display capabilities, allocates and
maps a compatible surface, transfers read-only composition authority, and
creates a visible window. On failure it releases every surface resource it
created.

`os_window_destroy` destroys the server object, unmaps and closes the local
surface, and resets the context. If the server rejects or times out the
destroy, the context remains live so the caller can retry; process-exit cleanup
is the final fallback.

## Surface Ownership

- The surface and writable mapping in `OsWindow` are owned by the SDK context.
- `os_window_replace_surface` allocates a same-sized blank surface, atomically
  installs it, then releases the old surface after server acceptance.
- `os_window_resize` follows the same transaction with new dimensions. The new
  pixels are blank and must be repainted before damage is submitted.
- `os_window_attach_surface` consumes the supplied surface and mapping only on
  success. On failure, ownership stays with the caller.
- Applications must not separately unmap or close `window.surface` while the
  context is live.

Surface content changes become visible only after `os_window_damage` or
`os_window_damage_all`. One call accepts 1 through 16 positive rectangles. The
SDK clips at the server composition boundary and hides the four-rectangle IPC
chunking protocol from the application.

## Drawing

`OsSurfaceCanvas` wraps a mapped 32-bit RGB/BGR surface. The public helpers are:

- `os_surface_canvas_put_pixel`;
- `os_surface_canvas_fill_rect`;
- `os_surface_canvas_draw_line`;
- `os_surface_canvas_draw_text` with the built-in 5x7 ASCII foundation.

Rectangles, lines, and text clip to canvas bounds. Drawing modifies mapped
memory only; no helper performs a display syscall or implicitly submits
damage. `OS_SURFACE_TEXT_TRANSPARENT_BG` leaves unset glyph pixels unchanged.

## State And Events

Move, show, hide, and focus operations update the local context only after an
accepted correlated reply. `os_window_get_info` queries live server state and
returns geometry, format, content generation, visibility, and focus in the
stable 56-byte `OsWindowInfo` structure.

`os_window_poll_event` is nonblocking. `os_window_wait_event` waits forever
when `timeout_ticks` is zero or returns `OS_ERR_TIMEOUT` after a nonzero
deadline. Both accept events only from the exact registered window-service
identity and validate the target window id plus generation. Focus and key
events share one monotonic server event sequence; applications should reject
duplicates or regressions according to their own state policy.

## Permissions

Ordinary GUI programs should launch with
`OS_PROCESS_PERMISSION_PROFILE_GUI_APPLICATION`. That profile permits the
surface, IPC, service-discovery, timing, and process operations needed by the
SDK, but does not grant raw `INPUT` or direct `DISPLAY`. `inputd` consumes raw
events, `windowd` owns focus and routing, and `displayd` remains the sole normal
present authority.

## Verification

```sh
make test-window-sdk-contracts
make test-gui-app
```

The host target exercises ABI layout, malformed and unrelated messages,
request/reply correlation, lifecycle operations, chunked damage, event
validation, canvas clipping, and permission/source boundaries. The QEMU target
drives initial paint, focus, F1 surface replacement/redraw, F2 resize, Escape
shutdown, pixel evidence, and resource accounting.
