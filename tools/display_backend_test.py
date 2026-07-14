#!/usr/bin/env python3
import subprocess
import tempfile
import textwrap
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


TEST_SOURCE = r"""
#include <stdint.h>
#include "drivers/gop.h"
#include "kernel/graphics/display_backend.h"

static GOPInfo fake_info = {0, 0, 8, 6, 8, OS64_PIXEL_FORMAT_RGB};
static OsRect captured[DISPLAY_BACKEND_MAX_DAMAGE_RECTS];
static uint32_t captured_count;
static int fail_present;

GOPDriver gop;

GOPDriver::GOPDriver() {}

const GOPInfo* GOPDriver::info() const {
    return &fake_info;
}

uint32_t GOPDriver::present_surface(const GraphicsSurface*,
                                    const OsRect* rects,
                                    uint32_t rect_count) {
    if (fail_present) {
        return 0;
    }
    captured_count = rect_count;
    for (uint32_t i = 0; i < rect_count; i++) {
        captured[i] = rects[i];
    }
    return rect_count;
}

static int failures;

static void check(int condition) {
    if (!condition) {
        failures++;
    }
}

int main() {
    uint32_t pixels[8 * 6] = {0};
    GraphicsSurface surface;
    check(gfx_surface_init(&surface, pixels, 8, 6, 8,
                           OS64_PIXEL_FORMAT_RGB, 0) == 1);
    display_backend_init();
    DisplayBackendInfo info;
    check(display_backend_get_info(&info) == DISPLAY_BACKEND_OK);
    check(info.width == 8 && info.height == 6 && info.stride_pixels == 8);

    OsRect partial = {-2, 1, 5, 3};
    uint32_t presented = 0;
    check(display_backend_present(&surface, &partial, 1, &presented) ==
          DISPLAY_BACKEND_OK);
    check(presented == 1 && captured_count == 1);
    check(captured[0].x == 0 && captured[0].y == 1 &&
          captured[0].width == 3 && captured[0].height == 3);

    OsRect full = {0, 0, 8, 6};
    check(display_backend_present(&surface, &full, 1, &presented) ==
          DISPLAY_BACKEND_OK);
    DisplayBackendStats stats;
    display_backend_get_stats(&stats);
    check(stats.present_count == 2 && stats.full_present_count == 1 &&
          stats.presented_rects == 2 && stats.rejected_count == 0);

    GraphicsSurface wrong = surface;
    wrong.width = 7;
    check(display_backend_present(&wrong, &full, 1, &presented) ==
          DISPLAY_BACKEND_ERR_INVALID);
    wrong = surface;
    wrong.pixel_format = 99;
    check(display_backend_present(&wrong, &full, 1, &presented) ==
          DISPLAY_BACKEND_ERR_INVALID);
    OsRect outside = {20, 20, 2, 2};
    check(display_backend_present(&surface, &outside, 1, &presented) ==
          DISPLAY_BACKEND_ERR_INVALID);
    check(display_backend_present(&surface, &full,
                                  DISPLAY_BACKEND_MAX_DAMAGE_RECTS + 1,
                                  &presented) == DISPLAY_BACKEND_ERR_INVALID);
    fail_present = 1;
    check(display_backend_present(&surface, &full, 1, &presented) ==
          DISPLAY_BACKEND_ERR_IO);
    display_backend_get_stats(&stats);
    check(stats.present_count == 2 && stats.rejected_count == 5);
    return failures == 0 ? 0 : 1;
}
"""


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="os64_display_backend_") as temp_dir:
        temp_path = Path(temp_dir)
        source_path = temp_path / "display_backend_test.cpp"
        binary_path = temp_path / "display_backend_test"
        source_path.write_text(textwrap.dedent(TEST_SOURCE), encoding="utf-8")
        subprocess.run(
            [
                "g++",
                "-std=c++17",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(REPO_ROOT / "include"),
                str(REPO_ROOT / "kernel/graphics/graphics_clip.cpp"),
                str(REPO_ROOT / "kernel/graphics/graphics_surface.cpp"),
                str(REPO_ROOT / "kernel/graphics/graphics_dirty.cpp"),
                str(REPO_ROOT / "kernel/graphics/display_backend.cpp"),
                str(source_path),
                "-o",
                str(binary_path),
            ],
            check=True,
        )
        subprocess.run([str(binary_path)], check=True)
    print("display backend test OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
