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

int main() {
    uint32_t pixels[8 * 6];
    for (unsigned i = 0; i < 8 * 6; i++) {
        pixels[i] = 0x00CCCCCCu;
    }

    GraphicsSurface rgb;
    check(gfx_surface_init(&rgb, pixels, 8, 6, 8, OS64_PIXEL_FORMAT_RGB, 0) == 1);

    gfx_put_pixel(&rgb, 2, 3, OS64_COLOR_RGB(0x11, 0x22, 0x33));
    check(pixels[3 * 8 + 2] == 0x00112233u);

    gfx_put_pixel(&rgb, -1, 0, 0);
    gfx_put_pixel(&rgb, 8, 0, 0);
    check(pixels[0] == 0x00CCCCCCu);

    OsRect rect = {-2, 1, 5, 3};
    gfx_fill_rect(&rgb, &rect, OS64_COLOR_RGB(0xAA, 0xBB, 0xCC));
    for (unsigned y = 0; y < 6; y++) {
        for (unsigned x = 0; x < 8; x++) {
            uint32_t value = pixels[y * 8 + x];
            if (y >= 1 && y < 4 && x < 3) {
                check(value == 0x00AABBCCu);
            }
        }
    }

    uint32_t bgr_pixels[4 * 4];
    for (unsigned i = 0; i < 4 * 4; i++) {
        bgr_pixels[i] = 0;
    }

    GraphicsSurface bgr;
    check(gfx_surface_init(&bgr, bgr_pixels, 4, 4, 4, OS64_PIXEL_FORMAT_BGR, 0) == 1);
    gfx_put_pixel(&bgr, 1, 1, OS64_COLOR_RGB(0x12, 0x34, 0x56));
    check(bgr_pixels[1 * 4 + 1] == 0x00563412u);

    OsRect bgr_rect = {2, 2, 10, 10};
    gfx_fill_rect(&bgr, &bgr_rect, OS64_COLOR_RGB(0x10, 0x20, 0x30));
    check(bgr_pixels[2 * 4 + 2] == 0x00302010u);
    check(bgr_pixels[3 * 4 + 3] == 0x00302010u);
    check(bgr_pixels[0] == 0);

    return failures == 0 ? 0 : 1;
}
"""


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="os64_gfx_draw_") as temp_dir:
        temp_path = Path(temp_dir)
        source_path = temp_path / "graphics_draw_test.cpp"
        binary_path = temp_path / "graphics_draw_test"
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
            str(REPO_ROOT / "kernel/graphics/graphics_surface.cpp"),
            str(REPO_ROOT / "kernel/graphics/graphics_draw.cpp"),
            str(source_path),
            "-o",
            str(binary_path),
        ]
        subprocess.run(compile_cmd, check=True)
        subprocess.run([str(binary_path)], check=True)

    print("graphics draw test OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
