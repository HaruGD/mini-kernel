# OS64 2D Graphics Library Plan

This document defines the first reusable 2D graphics layer for OS64.
The goal is not a full GUI yet. The goal is a small, stable drawing library
that can support demos, simple games, a future compositor, and eventually a
desktop UI without tying applications directly to GOP hardware details.

## Current Baseline

OS64 currently has:

- UEFI GOP framebuffer handoff through `BootInfo`.
- A built-in GOP driver that can:
  - read framebuffer info
  - put one pixel
  - fill a rectangle
  - clear the screen
- syscall-mediated user graphics calls through the SDK.
- external display-capable `.drv` modules through kernel GOP exports.

Current limitations:

- Drawing loops are still tied closely to GOP framebuffer access.
- User programs can draw, but there is no reusable 2D primitive layer.
- There is no RAM back buffer.
- There is no dirty-rectangle tracking.
- There is no bitmap, text, clipping, or sprite API.
- Direct per-pixel user calls are too expensive for real animation because
  every pixel call crosses the syscall boundary.

## Direction

The graphics stack should move toward this shape:

```text
User program / future window server
        |
        v
User SDK graphics API
        |
        v
Graphics syscall layer
        |
        v
Kernel 2D graphics library
        |
        +--> RAM surface / back buffer
        |
        +--> GOP framebuffer surface
        |
        v
GOP present path
```

The important rule is:

```text
Drawing algorithms operate on surfaces.
GOP is only one surface backend.
```

That keeps the same 2D code useful later for:

- framebuffer terminal drawing
- full-screen graphics demos
- simple games
- compositor surface rendering
- window contents
- future GPU-backed surfaces

## Non-Goals For This Stage

Do not build these yet:

- full compositor
- window manager
- GPU acceleration
- alpha-composited desktop
- mouse cursor compositor
- image file decoders
- anti-aliased vector graphics
- font shaping or Unicode text layout

Those should come after the 2D primitive and buffering path is stable.

## Core Types

The shared graphics ABI should define small fixed-size types.

```c
typedef struct OsPoint {
    int32_t x;
    int32_t y;
} OsPoint;

typedef struct OsRect {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
} OsRect;

typedef enum OsPixelFormat {
    OS_PIXEL_FORMAT_XRGB8888 = 1,
    OS_PIXEL_FORMAT_BGRX8888 = 2
} OsPixelFormat;

typedef struct OsSurfaceInfo {
    uint32_t width;
    uint32_t height;
    uint32_t stride_pixels;
    uint32_t pixel_format;
} OsSurfaceInfo;
```

Kernel-internal surfaces also need a pixel pointer:

```c
typedef struct GraphicsSurface {
    uint32_t* pixels;
    uint32_t width;
    uint32_t height;
    uint32_t stride_pixels;
    uint32_t pixel_format;
    uint32_t flags;
} GraphicsSurface;
```

User space must not receive the physical framebuffer address.
User space should receive only dimensions, format, and drawing APIs.

## Color Format

The public API should use one logical color format:

```text
0x00RRGGBB
```

The surface backend converts that logical color into native framebuffer order.

This avoids leaking GOP RGB/BGR differences into applications.

## Primitive API

The first kernel 2D primitive layer should provide:

```c
int gfx_clip_rect(const OsRect* bounds, const OsRect* input, OsRect* output);

void gfx_put_pixel(GraphicsSurface* dst, int32_t x, int32_t y, uint32_t color);
void gfx_fill_rect(GraphicsSurface* dst, const OsRect* rect, uint32_t color);
void gfx_draw_hline(GraphicsSurface* dst, int32_t x, int32_t y, int32_t width, uint32_t color);
void gfx_draw_vline(GraphicsSurface* dst, int32_t x, int32_t y, int32_t height, uint32_t color);
void gfx_draw_line(GraphicsSurface* dst, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color);
```

Then add bitmap and text:

```c
void gfx_blit(GraphicsSurface* dst, const GraphicsSurface* src, const OsRect* src_rect, int32_t dst_x, int32_t dst_y);
void gfx_blit_keyed(GraphicsSurface* dst, const GraphicsSurface* src, const OsRect* src_rect, int32_t dst_x, int32_t dst_y, uint32_t color_key);
void gfx_draw_glyph(GraphicsSurface* dst, int32_t x, int32_t y, char ch, uint32_t fg, uint32_t bg, uint32_t flags);
void gfx_draw_text(GraphicsSurface* dst, int32_t x, int32_t y, const char* text, uint32_t fg, uint32_t bg, uint32_t flags);
```

Every primitive must clip safely.
No primitive may write outside the destination surface.

## User SDK API

The first SDK layer should avoid per-pixel animation as the normal path.
Per-pixel access can exist for tests and tiny demos, but application drawing
should prefer batched primitives.

Initial SDK API:

```c
long os_gfx_get_info(OsGraphicsInfo* info);
long os_gfx_clear(uint32_t color);
long os_gfx_put_pixel(uint32_t x, uint32_t y, uint32_t color);
long os_gfx_fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color);
long os_gfx_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color);
long os_gfx_text(int32_t x, int32_t y, const char* text, uint32_t fg, uint32_t bg, uint32_t flags);
long os_gfx_blit(const OsBitmap* bitmap, const OsRect* src_rect, int32_t dst_x, int32_t dst_y, uint32_t flags);
```

Later SDK API after back buffering:

```c
long os_gfx_begin_frame(void);
long os_gfx_present(void);
long os_gfx_present_rect(const OsRect* rect);
```

The SDK should hide syscall details. User programs should include:

```c
#include <os64/os64.h>
```

## Back Buffer

The first back buffer should live in kernel memory.

```text
draw -> RAM back buffer -> present -> GOP framebuffer
```

This improves:

- flicker control
- frame consistency
- future compositor design
- terminal/graphics ownership control

Failure policy:

- If back-buffer allocation fails, direct GOP drawing remains available.
- The OS must not panic just because graphics buffering is unavailable.

## Dirty Rectangles

Dirty rectangles should start simple.

Use a bounded list:

```text
max dirty rects: small fixed number, for example 64
overflow policy: merge into one full-screen dirty rect
```

Dirty tracking is needed because copying the whole framebuffer every frame is
expensive, especially with cache-disabled GOP memory.

## Text Rendering

Text rendering should use a built-in bitmap font first.

Rules:

- keep font data in a separate file
- support printable ASCII first
- unknown characters render as a deterministic fallback glyph
- clipping must work per glyph
- no Unicode shaping in this stage

This is enough for:

- debug overlays
- simple games
- GUI prototype labels
- future terminal rendering reuse

## Ownership Model

At first, graphics can remain global.
Only one foreground graphics client should draw at a time.

Before compositor work starts, define ownership:

```text
terminal mode
graphics mode
future compositor mode
```

The immediate goal is to prevent shell text and graphics frame presents from
interleaving mid-frame.

## Test Plan

Add `make test-graphics` when the first primitives land.

Minimum coverage:

- rectangle clipping:
  - empty rect
  - full rect
  - partial rect
  - negative coordinates
  - integer overflow
- fill rect at every framebuffer edge
- horizontal and vertical lines
- diagonal and reversed lines
- off-screen lines
- bitmap blit with source and destination clipping
- color-key blit
- text drawing with fallback glyph
- full-frame present
- dirty-rect present

Screen smoke should verify:

- nonblank framebuffer
- expected colored regions
- expected text pixels
- no drawing outside requested regions

## Implementation Order

Follow `docs/phase2_task_breakdown.md`.

Recommended first commits:

1. `G01`: shared primitive type definitions
2. `G02`: clipping helper and tests
3. `G03`: memory surface type
4. `G04`: route existing GOP operations through surface drawing
5. `G05` to `G06`: line primitives
6. `G07` to `G08`: bitmap blit
7. `G09` to `G10`: bitmap font and text
8. `G11`: graphics demo program
9. `G12` to `G17`: back buffer, present, dirty rectangles, smoke tests

## Design Rules

- Keep hardware access in GOP/driver code.
- Keep drawing algorithms in reusable graphics-library code.
- Keep application policy in user space.
- Never expose physical framebuffer addresses to user programs.
- Avoid per-pixel syscalls in real demos and games.
- Make every draw primitive clip before writing.
- Prefer fixed-size ABI structs and explicit versioning.
- Keep the first library simple enough to test completely in QEMU.

