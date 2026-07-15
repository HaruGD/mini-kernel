# Window Service And Single-Window ABI

Phase 4D adds `windowd_c.elf` between GUI applications and `displayd_c.elf`.
The first implementation intentionally supports one opaque, display-sized
window. It proves the complete service pipeline before z-order, geometry,
focus, and general damage composition are added.

```text
restricted GUI application
  -> transferred client surface (READ | MAP at the receiver)
  -> windowd: ownership, generation checks, opaque full-frame copy
  -> windowd composite surface
  -> displayd present protocol
  -> display backend / GOP
```

`windowd` is supervised as the system service `window`, depends on `display`,
and uses the GUI-service permission profile. It has no `DISPLAY` permission
and contains no direct graphics-present call. `displayd` remains the sole
normal presentation authority.

## Protocol v1

The shared ABI is `OS64_WINDOW_ABI_VERSION == 1` in
`include/os64/window_types.h`. Phase 4D implements these commands:

- `CREATE`: transfers one full-screen client surface and creates the sole
  window;
- `SET_SURFACE`: atomically replaces that window's current surface after the
  replacement frame is acknowledged by `displayd`;
- `DAMAGE`: announces a new full-frame content generation on the current
  surface;
- `DESTROY`: releases the caller-owned window and clears the composite frame;
- `REPLY`: returns the operation result, window id and generation, and the
  content generation accepted through the display pipeline.

Every request uses IPC v2 and binds mutation to the sender PID plus process
generation, request id, window id, window generation, and monotonically
increasing content generation. The service rejects malformed ABI fields,
wrong owners, stale window generations, duplicate content generations,
unexpected handles, non-display-sized surfaces, stride mismatches, and format
mismatches without changing the active window.

Phase 4D uses one fixed window id and advances its generation after every
destroy/create cycle. The fixed id is not a reusable authority token without
the matching generation.

## Composition And Lifetime

`windowd` allocates one display-sized, page-backed composite surface and maps
it read/write. Transferred client surfaces are attenuated by IPC and mapped
read-only. Composition copies opaque 24-bit pixel values into the composite;
client writes can never target the composite or physical framebuffer.

`CREATE`, `SET_SURFACE`, and `DAMAGE` keep the frame pending until
`os_display_present` returns the exact accepted generation. A failed surface
replacement restores the previous composite and releases the candidate.
Repeated client updates are bounded by the one-request service loop and the
display protocol's newest-full-frame replacement rule.

The window service checks owner liveness by PID plus process generation using
the GUI-service-only process-liveness query. Unexpected client exit drops the
read mapping and transferred handle immediately while retaining the last
composite pixels as the display fallback. Explicit `DESTROY` clears and
presents the composite.

`windowd` tracks the registered display owner identity. When `displayd`
disappears it keeps a full frame pending; when a different owner generation is
registered it submits the complete composite again. The reconnect path does
not grant `windowd` direct display authority.

## Current Limits

- one client and one visible full-screen window;
- opaque RGB/BGR 32-bit surfaces only;
- full-frame composition and damage only;
- no move, resize, show/hide, z-order, focus, or input routing;
- the test producer uses the existing SDK test binary; the public Window SDK
  remains Phase 4G work.

## Verification

```sh
make test-window-contracts
make test-window-single
```

The host target covers ABI validation, wrong-owner and stale-generation
denial, duplicate generations, the one-client bound, pixel copying, alpha-bit
removal, stride bounds, supervision policy, authority separation, and the
display reconnect/full-resubmit implementation path. The QEMU target starts
`inputd`, `displayd`, and `windowd` under supervision, runs a restricted
client through `CREATE`, `SET_SURFACE`, full damage, and `DESTROY`, validates
deterministic 800x600 pixels, proves direct-display denial, and checks
unexpected-exit resource cleanup.
