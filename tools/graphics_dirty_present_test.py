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

static void fill_pixels(uint32_t* pixels, uint32_t count, uint32_t color) {
    for (uint32_t i = 0; i < count; i++) {
        pixels[i] = color;
    }
}

static void expect_partial_rect(const uint32_t* pixels,
                                uint32_t width,
                                uint32_t height,
                                const OsRect* rect,
                                uint32_t changed,
                                uint32_t unchanged) {
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            int inside = x >= (uint32_t)rect->x &&
                         y >= (uint32_t)rect->y &&
                         x < (uint32_t)(rect->x + rect->width) &&
                         y < (uint32_t)(rect->y + rect->height);
            uint32_t expected = inside ? changed : unchanged;
            check(pixels[y * width + x] == expected);
        }
    }
}

int main() {
    const uint32_t width = 8;
    const uint32_t height = 6;
    uint32_t source_pixels[width * height];
    uint32_t destination_pixels[width * height];
    fill_pixels(source_pixels, width * height, 0x00222222);
    fill_pixels(destination_pixels, width * height, 0x00111111);

    GraphicsSurface source;
    GraphicsSurface destination;
    check(gfx_surface_init(&source,
                           source_pixels,
                           width,
                           height,
                           width,
                           OS64_PIXEL_FORMAT_RGB,
                           0) == 1);
    check(gfx_surface_init(&destination,
                           destination_pixels,
                           width,
                           height,
                           width,
                           OS64_PIXEL_FORMAT_RGB,
                           0) == 1);

    GraphicsDirtyTracker dirty;
    OsRect bounds = {0, 0, (int32_t)width, (int32_t)height};
    gfx_dirty_init(&dirty, &bounds);

    OsRect partial = {2, 1, 3, 2};
    gfx_dirty_mark(&dirty, &partial);
    check(gfx_present_dirty_surface(&destination, &source, &dirty) == 1);
    check(gfx_dirty_count(&dirty) == 0);
    check(gfx_dirty_is_full(&dirty) == 0);
    expect_partial_rect(destination_pixels,
                        width,
                        height,
                        &partial,
                        0x00222222,
                        0x00111111);

    fill_pixels(source_pixels, width * height, 0x00333333);
    gfx_dirty_mark_full(&dirty);
    check(gfx_present_dirty_surface(&destination, &source, &dirty) == 1);
    check(gfx_dirty_count(&dirty) == 0);
    for (uint32_t i = 0; i < width * height; i++) {
        check(destination_pixels[i] == 0x00333333);
    }

    check(gfx_present_dirty_surface(&destination, &source, &dirty) == 0);
    check(gfx_dirty_count(&dirty) == 0);

    return failures == 0 ? 0 : 1;
}
"""


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="os64_gfx_dirty_present_") as temp_dir:
        temp_path = Path(temp_dir)
        source_path = temp_path / "graphics_dirty_present_test.cpp"
        binary_path = temp_path / "graphics_dirty_present_test"
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
            str(REPO_ROOT / "kernel/graphics/graphics_dirty.cpp"),
            str(REPO_ROOT / "kernel/graphics/graphics_present.cpp"),
            str(REPO_ROOT / "kernel/graphics/graphics_font.cpp"),
            str(source_path),
            "-o",
            str(binary_path),
        ]
        subprocess.run(compile_cmd, check=True)
        subprocess.run([str(binary_path)], check=True)

    print("graphics dirty present test OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
