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

    uint32_t line_pixels[6 * 5];
    for (unsigned i = 0; i < 6 * 5; i++) {
        line_pixels[i] = 0;
    }

    GraphicsSurface lines;
    check(gfx_surface_init(&lines, line_pixels, 6, 5, 6, OS64_PIXEL_FORMAT_RGB, 0) == 1);

    gfx_draw_hline(&lines, -2, 0, 5, OS64_COLOR_RGB(0x01, 0x02, 0x03));
    check(line_pixels[0] == 0x00010203u);
    check(line_pixels[1] == 0x00010203u);
    check(line_pixels[2] == 0x00010203u);
    check(line_pixels[3] == 0);

    gfx_draw_hline(&lines, 4, 4, 5, OS64_COLOR_RGB(0x04, 0x05, 0x06));
    check(line_pixels[4 * 6 + 4] == 0x00040506u);
    check(line_pixels[4 * 6 + 5] == 0x00040506u);

    gfx_draw_hline(&lines, 0, 5, 6, OS64_COLOR_RGB(0xFF, 0xFF, 0xFF));
    check(line_pixels[0] == 0x00010203u);

    gfx_draw_vline(&lines, 0, -2, 4, OS64_COLOR_RGB(0x07, 0x08, 0x09));
    check(line_pixels[0] == 0x00070809u);
    check(line_pixels[6] == 0x00070809u);
    check(line_pixels[12] == 0);

    gfx_draw_vline(&lines, 5, 3, 5, OS64_COLOR_RGB(0x0A, 0x0B, 0x0C));
    check(line_pixels[3 * 6 + 5] == 0x000A0B0Cu);
    check(line_pixels[4 * 6 + 5] == 0x000A0B0Cu);

    gfx_draw_vline(&lines, 6, 0, 5, OS64_COLOR_RGB(0xFF, 0xFF, 0xFF));
    check(line_pixels[5] == 0);

    uint32_t general_pixels[8 * 8];
    for (unsigned i = 0; i < 8 * 8; i++) {
        general_pixels[i] = 0;
    }

    GraphicsSurface general;
    check(gfx_surface_init(&general, general_pixels, 8, 8, 8, OS64_PIXEL_FORMAT_RGB, 0) == 1);

    gfx_draw_line(&general, 0, 0, 7, 7, OS64_COLOR_RGB(0x11, 0x00, 0x00));
    check(general_pixels[0] == 0x00110000u);
    check(general_pixels[1 * 8 + 1] == 0x00110000u);
    check(general_pixels[7 * 8 + 7] == 0x00110000u);

    gfx_draw_line(&general, 7, 0, 0, 7, OS64_COLOR_RGB(0x00, 0x22, 0x00));
    check(general_pixels[7] == 0x00002200u);
    check(general_pixels[1 * 8 + 6] == 0x00002200u);
    check(general_pixels[7 * 8] == 0x00002200u);

    gfx_draw_line(&general, -3, 4, 3, 4, OS64_COLOR_RGB(0x00, 0x00, 0x33));
    check(general_pixels[4 * 8 + 0] == 0x00000033u);
    check(general_pixels[4 * 8 + 1] == 0x00000033u);
    check(general_pixels[4 * 8 + 2] == 0x00000033u);
    check(general_pixels[4 * 8 + 3] == 0x00000033u);

    gfx_draw_line(&general, 2, -3, 2, 2, OS64_COLOR_RGB(0x44, 0x44, 0x00));
    check(general_pixels[2] == 0x00444400u);
    check(general_pixels[1 * 8 + 2] == 0x00444400u);
    check(general_pixels[2 * 8 + 2] == 0x00444400u);

    gfx_draw_line(&general, 10, 10, 14, 14, OS64_COLOR_RGB(0xFF, 0xFF, 0xFF));
    check(general_pixels[7 * 8 + 7] == 0x00110000u);

    uint32_t source_pixels[4 * 4];
    for (unsigned y = 0; y < 4; y++) {
        for (unsigned x = 0; x < 4; x++) {
            source_pixels[y * 4 + x] = 0x00010000u * y + x + 1;
        }
    }
    uint32_t destination_pixels[6 * 5];
    for (unsigned i = 0; i < 6 * 5; i++) {
        destination_pixels[i] = 0x00EEEEEEu;
    }

    GraphicsSurface source;
    GraphicsSurface destination;
    check(gfx_surface_init(&source, source_pixels, 4, 4, 4, OS64_PIXEL_FORMAT_RGB, 0) == 1);
    check(gfx_surface_init(&destination, destination_pixels, 6, 5, 6, OS64_PIXEL_FORMAT_RGB, 0) == 1);

    OsRect source_rect = {1, 1, 3, 3};
    gfx_blit(&destination, &source, &source_rect, 2, 1);
    check(destination_pixels[1 * 6 + 2] == source_pixels[1 * 4 + 1]);
    check(destination_pixels[1 * 6 + 4] == source_pixels[1 * 4 + 3]);
    check(destination_pixels[3 * 6 + 2] == source_pixels[3 * 4 + 1]);
    check(destination_pixels[0] == 0x00EEEEEEu);

    OsRect partial_source = {-1, -1, 4, 4};
    gfx_blit(&destination, &source, &partial_source, -1, -1);
    check(destination_pixels[0] == source_pixels[0]);
    check(destination_pixels[1] == source_pixels[1]);
    check(destination_pixels[6] == source_pixels[4]);

    uint32_t bgr_source_pixels[2 * 2];
    bgr_source_pixels[0] = 0x00563412u;
    bgr_source_pixels[1] = 0x00EFCDABu;
    bgr_source_pixels[2] = 0;
    bgr_source_pixels[3] = 0;
    uint32_t rgb_destination_pixels[2 * 2] = {0, 0, 0, 0};
    GraphicsSurface bgr_source;
    GraphicsSurface rgb_destination;
    check(gfx_surface_init(&bgr_source, bgr_source_pixels, 2, 2, 2, OS64_PIXEL_FORMAT_BGR, 0) == 1);
    check(gfx_surface_init(&rgb_destination, rgb_destination_pixels, 2, 2, 2, OS64_PIXEL_FORMAT_RGB, 0) == 1);
    OsRect bgr_source_rect = {0, 0, 2, 1};
    gfx_blit(&rgb_destination, &bgr_source, &bgr_source_rect, 0, 0);
    check(rgb_destination_pixels[0] == 0x00123456u);
    check(rgb_destination_pixels[1] == 0x00ABCDEFu);

    uint32_t keyed_source_pixels[3 * 3] = {
        0x00FF00FFu, 0x00000011u, 0x00FF00FFu,
        0x00000022u, 0x00FF00FFu, 0x00000033u,
        0x00FF00FFu, 0x00000044u, 0x00FF00FFu,
    };
    uint32_t keyed_destination_pixels[4 * 4];
    for (unsigned i = 0; i < 4 * 4; i++) {
        keyed_destination_pixels[i] = 0x00EEEEEEu;
    }

    GraphicsSurface keyed_source;
    GraphicsSurface keyed_destination;
    check(gfx_surface_init(&keyed_source, keyed_source_pixels, 3, 3, 3, OS64_PIXEL_FORMAT_RGB, 0) == 1);
    check(gfx_surface_init(&keyed_destination, keyed_destination_pixels, 4, 4, 4, OS64_PIXEL_FORMAT_RGB, 0) == 1);
    OsRect keyed_rect = {0, 0, 3, 3};
    gfx_blit_keyed(&keyed_destination,
                   &keyed_source,
                   &keyed_rect,
                   1,
                   1,
                   OS64_COLOR_RGB(0xFF, 0x00, 0xFF));
    check(keyed_destination_pixels[1 * 4 + 1] == 0x00EEEEEEu);
    check(keyed_destination_pixels[1 * 4 + 2] == 0x00000011u);
    check(keyed_destination_pixels[2 * 4 + 1] == 0x00000022u);
    check(keyed_destination_pixels[2 * 4 + 2] == 0x00EEEEEEu);
    check(keyed_destination_pixels[2 * 4 + 3] == 0x00000033u);
    check(keyed_destination_pixels[3 * 4 + 2] == 0x00000044u);

    OsRect keyed_partial = {0, 1, 3, 2};
    gfx_blit_keyed(&keyed_destination,
                   &keyed_source,
                   &keyed_partial,
                   -1,
                   0,
                   OS64_COLOR_RGB(0xFF, 0x00, 0xFF));
    check(keyed_destination_pixels[0] == 0x00EEEEEEu);
    check(keyed_destination_pixels[1] == 0x00000033u);
    check(keyed_destination_pixels[4] == 0x00000044u);
    check(keyed_destination_pixels[5] == 0x00EEEEEEu);

    uint32_t text_pixels[18 * 18];
    for (unsigned i = 0; i < 18 * 18; i++) {
        text_pixels[i] = 0x00CCCCCCu;
    }
    GraphicsSurface text_surface;
    check(gfx_surface_init(&text_surface, text_pixels, 18, 18, 18, OS64_PIXEL_FORMAT_RGB, 0) == 1);

    gfx_draw_glyph(&text_surface,
                   1,
                   1,
                   'A',
                   OS64_COLOR_RGB(0xFF, 0xFF, 0xFF),
                   OS64_COLOR_RGB(0x00, 0x00, 0x00),
                   0);
    check(text_pixels[1 * 18 + 1] == 0x00000000u);
    check(text_pixels[1 * 18 + 2] == 0x00FFFFFFu);
    check(text_pixels[4 * 18 + 1] == 0x00FFFFFFu);
    check(text_pixels[4 * 18 + 5] == 0x00FFFFFFu);

    text_pixels[0] = 0x00CCCCCCu;
    gfx_draw_glyph(&text_surface,
                   -2,
                   -1,
                   '@',
                   OS64_COLOR_RGB(0x11, 0x22, 0x33),
                   OS64_COLOR_RGB(0x44, 0x55, 0x66),
                   GFX_TEXT_FLAG_TRANSPARENT_BG);
    check(text_pixels[0] == 0x00CCCCCCu || text_pixels[0] == 0x00112233u);

    gfx_draw_text(&text_surface,
                  0,
                  9,
                  "A\nB",
                  OS64_COLOR_RGB(0x77, 0x88, 0x99),
                  OS64_COLOR_RGB(0x00, 0x00, 0x00),
                  0);
    check(text_pixels[9 * 18 + 1] == 0x00778899u);
    check(text_pixels[17 * 18 + 0] == 0x00778899u);
    check(text_pixels[17 * 18 + 3] == 0x00778899u);

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
            str(REPO_ROOT / "kernel/graphics/graphics_font.cpp"),
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
