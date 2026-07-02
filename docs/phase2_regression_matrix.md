# Phase 2 Regression Matrix

Phase 2 stabilizes the first reusable 2D graphics path and common input-event
path on the active UEFI kernel.

| ID | Area | Scenario | Expected result | Status | Automated by |
| --- | --- | --- | --- | --- | --- |
| G01 | Graphics ABI | Shared primitive, surface, color, bitmap, and text structs | Kernel/user ABI sizes stay stable | Automated | `make test-user-sdk` and compile-time assertions |
| G02 | Clipping | Empty, full, partial, negative, and overflow rectangles | Drawing clips safely and never writes outside a surface | Automated | `make test-graphics` |
| G03 | Surfaces | RAM surface creation and bounds checks | Surface operations work independently from GOP MMIO | Automated | `make test-graphics` |
| G04 | Basic drawing | Pixel and filled rectangle through surfaces | Existing GOP behavior is retained | Automated | `make test-graphics`, `make test-user-sdk` |
| G05 | Lines | Horizontal, vertical, diagonal, reversed, and off-screen lines | Lines render only inside clipped bounds | Automated | `make test-graphics` |
| G06 | Blit | Opaque and color-key bitmap blits | Source and destination clipping are correct | Automated | `make test-graphics`, `make test-user-sdk` |
| G07 | Text | Bitmap font, fallback glyph, newline, and bounded text | Text renders predictably and clips per glyph | Automated | `make test-graphics`, `make test-user-sdk` |
| G08 | Demo | `ugfxdemo_c.elf` draws through the SDK | Demo exits cleanly and leaves a nonblank frame | Automated | `make test-graphics` |
| G09 | Back buffer | Full-frame present from RAM to GOP | Framebuffer becomes visibly nonblank | Automated | `python3 tools/uefi_screen_smoke.py`, `make test-graphics` |
| G10 | Dirty present | Partial present updates only dirty rectangles | Unchanged framebuffer regions remain byte-identical | Automated | `make test-graphics` |
| G11 | Screen modes | 1280x800 and 800x600 QEMU GOP captures | Placement, clipping, and nonblank pixels are verified | Automated | `make test-graphics` |
| I01 | Input ABI | Key, pointer, and common event payload sizes | Kernel/user ABI sizes match | Automated | `make test-user-sdk` |
| I02 | Input queue | FIFO, wraparound, empty, full, and overflow behavior | Oldest events drop and counters update | Automated | `make test-input` |
| I03 | Keyboard translation | PS/2 set-1 key up/down and modifiers | Common key events preserve keycode, character, and modifiers | Automated | `make test-input` |
| I04 | Input syscalls | Blocking and nonblocking event reads | Poll returns would-block; wait sleeps until input arrives | Automated | `make test-user-sdk`, `make test-input` |
| I05 | Process routing | Focused process receives input | Background processes cannot consume focused input | Automated | `make test-input` |
| I06 | Legacy getchar | Existing C shell line input uses the focused event queue | `ushell_c.elf` accepts commands without duplicate characters | Automated | `python3 tools/uefi_userland_smoke.py` |
| I07 | Event-loop sample | `uevent_c.elf` blocks for key events and exits on input | User event loop wakes and returns normally | Automated | `make test-input` |

Run the full Phase 2 closure baseline with:

```sh
make clean && make all && make uefi
make test-phase1
python3 tools/uefi_smoke.py
python3 tools/uefi_userland_smoke.py
python3 tools/uefi_screen_smoke.py
make test-user-sdk
make test-graphics
make test-input
```

The latest Phase 2 closure run passed this baseline on QEMU/OVMF.
