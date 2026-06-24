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
    uint32_t pixels[16 * 12];
    GraphicsSurface surface;
    check(gfx_surface_init(&surface, pixels, 16, 10, 16, OS64_PIXEL_FORMAT_RGB, 0) == 1);
    check(gfx_surface_is_valid(&surface) == 1);
    check(surface.pixels == pixels);
    check(surface.width == 16);
    check(surface.height == 10);
    check(surface.stride_pixels == 16);
    check(surface.pixel_format == OS64_PIXEL_FORMAT_RGB);

    OsSurfaceInfo info;
    check(gfx_surface_get_info(&surface, &info) == 1);
    check(info.width == 16);
    check(info.height == 10);
    check(info.stride_pixels == 16);
    check(info.pixel_format == OS64_PIXEL_FORMAT_RGB);

    OsRect bounds;
    check(gfx_surface_bounds(&surface, &bounds) == 1);
    check(bounds.x == 0);
    check(bounds.y == 0);
    check(bounds.width == 16);
    check(bounds.height == 10);

    check(gfx_surface_contains_point(&surface, 0, 0) == 1);
    check(gfx_surface_contains_point(&surface, 15, 9) == 1);
    check(gfx_surface_contains_point(&surface, 16, 9) == 0);
    check(gfx_surface_contains_point(&surface, 15, 10) == 0);
    check(gfx_surface_contains_point(&surface, -1, 0) == 0);
    check(gfx_surface_contains_point(&surface, 0, -1) == 0);

    GraphicsSurface invalid;
    check(gfx_surface_init(&invalid, 0, 16, 10, 16, OS64_PIXEL_FORMAT_RGB, 0) == 0);
    check(gfx_surface_is_valid(&invalid) == 0);
    check(invalid.pixels == 0);
    check(invalid.width == 0);
    check(invalid.height == 0);

    check(gfx_surface_init(&invalid, pixels, 0, 10, 16, OS64_PIXEL_FORMAT_RGB, 0) == 0);
    check(gfx_surface_init(&invalid, pixels, 16, 0, 16, OS64_PIXEL_FORMAT_RGB, 0) == 0);
    check(gfx_surface_init(&invalid, pixels, 16, 10, 15, OS64_PIXEL_FORMAT_RGB, 0) == 0);
    check(gfx_surface_init(&invalid, pixels, 16, 10, 16, 99, 0) == 0);
    check(gfx_surface_init(&invalid, pixels, 0x80000000u, 1, 0x80000000u, OS64_PIXEL_FORMAT_RGB, 0) == 0);
    check(gfx_surface_init(&invalid, pixels, 1, 0x80000000u, 1, OS64_PIXEL_FORMAT_RGB, 0) == 0);

    OsSurfaceInfo zero_info = {1, 2, 3, 4};
    check(gfx_surface_get_info(&invalid, &zero_info) == 0);
    check(zero_info.width == 0);
    check(zero_info.height == 0);
    check(zero_info.stride_pixels == 0);
    check(zero_info.pixel_format == OS64_PIXEL_FORMAT_RGB);

    OsRect zero_bounds = {1, 2, 3, 4};
    check(gfx_surface_bounds(&invalid, &zero_bounds) == 0);
    check(zero_bounds.x == 0);
    check(zero_bounds.y == 0);
    check(zero_bounds.width == 0);
    check(zero_bounds.height == 0);

    return failures == 0 ? 0 : 1;
}
"""


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="os64_gfx_surface_") as temp_dir:
        temp_path = Path(temp_dir)
        source_path = temp_path / "graphics_surface_test.cpp"
        binary_path = temp_path / "graphics_surface_test"
        source_path.write_text(textwrap.dedent(TEST_SOURCE), encoding="utf-8")

        compile_cmd = [
            "g++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(REPO_ROOT / "include"),
            str(REPO_ROOT / "kernel/graphics/graphics_surface.cpp"),
            str(source_path),
            "-o",
            str(binary_path),
        ]
        subprocess.run(compile_cmd, check=True)
        subprocess.run([str(binary_path)], check=True)

    print("graphics surface test OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
