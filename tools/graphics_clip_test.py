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

static void expect_rect(const OsRect* rect,
                        int32_t x,
                        int32_t y,
                        int32_t width,
                        int32_t height) {
    if (rect->x != x || rect->y != y ||
        rect->width != width || rect->height != height) {
        failures++;
    }
}

static void expect_clip(const OsRect* bounds,
                        const OsRect* input,
                        int expected_result,
                        int32_t x,
                        int32_t y,
                        int32_t width,
                        int32_t height) {
    OsRect output = {123, 456, 789, 321};
    int result = gfx_clip_rect(bounds, input, &output);
    if (result != expected_result) {
        failures++;
    }
    expect_rect(&output, x, y, width, height);
}

int main() {
    OsRect bounds = {0, 0, 100, 80};

    OsRect full = {10, 20, 30, 40};
    expect_clip(&bounds, &full, 1, 10, 20, 30, 40);

    OsRect partial = {-10, -5, 30, 20};
    expect_clip(&bounds, &partial, 1, 0, 0, 20, 15);

    OsRect right_bottom = {90, 70, 50, 50};
    expect_clip(&bounds, &right_bottom, 1, 90, 70, 10, 10);

    OsRect offscreen = {120, 10, 20, 20};
    expect_clip(&bounds, &offscreen, 0, 0, 0, 0, 0);

    OsRect empty_width = {10, 10, 0, 20};
    expect_clip(&bounds, &empty_width, 0, 0, 0, 0, 0);

    OsRect empty_height = {10, 10, 20, -1};
    expect_clip(&bounds, &empty_height, 0, 0, 0, 0, 0);

    OsRect huge_bounds = {INT32_MAX - 20, INT32_MAX - 20, 20, 20};
    OsRect huge_input = {INT32_MAX - 10, INT32_MAX - 12, 50, 50};
    expect_clip(&huge_bounds, &huge_input, 1, INT32_MAX - 10, INT32_MAX - 12, 10, 12);

    OsRect min_bounds = {INT32_MIN, INT32_MIN, 50, 50};
    OsRect min_input = {INT32_MIN, INT32_MIN, 10, 10};
    expect_clip(&min_bounds, &min_input, 1, INT32_MIN, INT32_MIN, 10, 10);

    expect_clip(0, &full, 0, 0, 0, 0, 0);
    expect_clip(&bounds, 0, 0, 0, 0, 0, 0);

    return failures == 0 ? 0 : 1;
}
"""


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="os64_gfx_clip_") as temp_dir:
        temp_path = Path(temp_dir)
        source_path = temp_path / "graphics_clip_test.cpp"
        binary_path = temp_path / "graphics_clip_test"
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
            str(source_path),
            "-o",
            str(binary_path),
        ]
        subprocess.run(compile_cmd, check=True)
        subprocess.run([str(binary_path)], check=True)

    print("graphics clipping test OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
