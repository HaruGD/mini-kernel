#!/usr/bin/env python3
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

HARNESS = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "window_compositor.h"
#include "window_interaction.h"

int main(void) {
    WindowTable table;
    window_state_init(&table);
    OsProcessIdentity owner = {4, 9};
    WindowEntry* entry = window_state_commit_create(&table, owner, 1,
                                                     20, 30, 40, 20);
    assert(entry);
    window_state_set_decorated(entry, 1);
    uint32_t source_pixels[4] = {
        OS_RGB(255, 0, 0), OS_RGB(0, 255, 0),
        OS_RGB(0, 0, 255), OS_RGB(255, 255, 255),
    };
    WindowCompositorSource sources[OS_WINDOW_MAX_WINDOWS] = {0};
    sources[entry->window_id - 1] =
        (WindowCompositorSource){source_pixels, 2, 2, 2};
    uint32_t underlay_pixels[100 * 80];
    uint32_t output[100 * 80];
    memset(underlay_pixels, 0, sizeof(underlay_pixels));
    memset(output, 0, sizeof(output));
    WindowCompositorSource underlay = {underlay_pixels, 100, 100, 80};
    WindowDamageAccumulator damage;
    window_damage_init(&damage, 100, 80);
    window_damage_full(&damage);
    assert(window_compositor_compose_underlay(output, 100, 100, 80,
                                               &underlay, &table, sources,
                                               &damage) == 0);
    assert(output[30 * 100 + 20] == OS_RGB(255, 0, 0));
    assert(output[30 * 100 + 59] == OS_RGB(0, 255, 0));
    assert(output[49 * 100 + 20] == OS_RGB(0, 0, 255));
    assert(output[49 * 100 + 59] == OS_RGB(255, 255, 255));
    assert(output[7 * 100 + 25] == OS_RGB(48, 78, 128));
    assert(output[12 * 100 + 50] == OS_RGB(190, 66, 72));

    OsRect frame = window_state_frame_rect(entry);
    assert(frame.x == 16 && frame.y == 6 && frame.width == 48 && frame.height == 48);
    puts("window decoration/scaling compositor tests OK");
    return 0;
}
'''


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="os64-window-interaction-") as directory:
        source = Path(directory) / "test.c"
        binary = Path(directory) / "test"
        source.write_text(HARNESS)
        subprocess.run([
            "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
            "-I", str(ROOT / "include"),
            "-I", str(ROOT / "user" / "sdk" / "include"),
            "-I", str(ROOT / "user" / "programs" / "windowd"),
            str(source),
            str(ROOT / "user" / "programs" / "windowd" / "window_state.c"),
            str(ROOT / "user" / "programs" / "windowd" / "window_interaction.c"),
            str(ROOT / "user" / "programs" / "windowd" / "window_compositor.c"),
            "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)
    sdk = (ROOT / "user" / "sdk" / "src" / "window.c").read_text()
    assert "OS_WINDOW_FLAG_DECORATED" in sdk
    assert "OS_WINDOW_EVENT_CONFIGURE" in sdk
    print("interactive window public ABI source checks OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
