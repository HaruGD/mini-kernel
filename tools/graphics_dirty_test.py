#!/usr/bin/env python3
import subprocess
import tempfile
import textwrap
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


TEST_SOURCE = r"""
#include <stdint.h>
#include "kernel/graphics/graphics2d.h"

static int failures = 0;

static void check(int condition) {
    if (!condition) {
        failures++;
    }
}

static void expect_rect(const OsRect* rect,
                        int32_t x,
                        int32_t y,
                        int32_t width,
                        int32_t height) {
    check(rect != 0);
    if (rect == 0) {
        return;
    }
    check(rect->x == x);
    check(rect->y == y);
    check(rect->width == width);
    check(rect->height == height);
}

int main() {
    GraphicsDirtyTracker tracker;
    OsRect bounds = {0, 0, 100, 80};
    gfx_dirty_init(&tracker, &bounds);

    check(gfx_dirty_count(&tracker) == 0);
    check(gfx_dirty_is_full(&tracker) == 0);

    OsRect first = {10, 10, 20, 10};
    gfx_dirty_mark(&tracker, &first);
    check(gfx_dirty_count(&tracker) == 1);
    expect_rect(&gfx_dirty_rects(&tracker)[0], 10, 10, 20, 10);

    OsRect overlap = {25, 15, 10, 10};
    gfx_dirty_mark(&tracker, &overlap);
    check(gfx_dirty_count(&tracker) == 1);
    expect_rect(&gfx_dirty_rects(&tracker)[0], 10, 10, 25, 15);

    OsRect partial = {-5, -5, 10, 10};
    gfx_dirty_mark(&tracker, &partial);
    check(gfx_dirty_count(&tracker) == 2);
    expect_rect(&gfx_dirty_rects(&tracker)[1], 0, 0, 5, 5);

    OsRect outside = {120, 0, 20, 20};
    gfx_dirty_mark(&tracker, &outside);
    check(gfx_dirty_count(&tracker) == 2);

    gfx_dirty_clear(&tracker);
    check(gfx_dirty_count(&tracker) == 0);
    check(gfx_dirty_is_full(&tracker) == 0);

    for (uint32_t i = 0; i < GFX_DIRTY_MAX_RECTS + 1; i++) {
        OsRect rect = {(int32_t)(i % 10) * 9, (int32_t)(i / 10) * 9, 2, 2};
        gfx_dirty_mark(&tracker, &rect);
    }
    check(gfx_dirty_is_full(&tracker) == 1);
    check(gfx_dirty_count(&tracker) == 1);
    expect_rect(&gfx_dirty_rects(&tracker)[0], 0, 0, 100, 80);

    gfx_dirty_clear(&tracker);
    gfx_dirty_mark_full(&tracker);
    check(gfx_dirty_is_full(&tracker) == 1);
    check(gfx_dirty_count(&tracker) == 1);
    expect_rect(&gfx_dirty_rects(&tracker)[0], 0, 0, 100, 80);

    return failures == 0 ? 0 : 1;
}
"""


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="os64_gfx_dirty_") as temp_dir:
        temp_path = Path(temp_dir)
        source_path = temp_path / "graphics_dirty_test.cpp"
        binary_path = temp_path / "graphics_dirty_test"
        source_path.write_text(textwrap.dedent(TEST_SOURCE), encoding="utf-8")

        compile_cmd = [
            "g++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(REPO_ROOT / "include"),
            str(REPO_ROOT / "kernel/graphics/graphics_clip.cpp"),
            str(REPO_ROOT / "kernel/graphics/graphics_dirty.cpp"),
            str(source_path),
            "-o",
            str(binary_path),
        ]
        subprocess.run(compile_cmd, check=True)
        subprocess.run([str(binary_path)], check=True)

    print("graphics dirty test OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
