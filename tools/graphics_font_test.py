#!/usr/bin/env python3
import subprocess
import tempfile
import textwrap
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


TEST_SOURCE = r"""
#include <stdint.h>
#include "kernel/graphics/graphics_font.h"

static int failures = 0;

static void check(int condition) {
    if (!condition) {
        failures++;
    }
}

int main() {
    check(GFX_FONT_WIDTH == 5);
    check(GFX_FONT_HEIGHT == 7);
    check(GFX_FONT_ADVANCE == 6);
    check(GFX_FONT_LINE_HEIGHT == 8);

    check(gfx_font_glyph_row('A', 0) == 0x0E);
    check(gfx_font_glyph_row('A', 3) == 0x1F);
    check(gfx_font_glyph_row('a', 2) == 0x0E);
    check(gfx_font_glyph_row('0', 0) == 0x0E);
    check(gfx_font_glyph_row('-', 3) == 0x1F);
    check(gfx_font_glyph_row(' ', 4) == 0x00);
    check(gfx_font_glyph_row('A', 99) == 0x00);

    check(gfx_font_has_direct_glyph('Z') == 1);
    check(gfx_font_has_direct_glyph('z') == 1);
    check(gfx_font_has_direct_glyph('9') == 1);
    check(gfx_font_has_direct_glyph('?') == 1);
    check(gfx_font_has_direct_glyph('@') == 0);

    for (char ch = 32; ch <= 126; ch++) {
        uint8_t row0 = gfx_font_glyph_row(ch, 0);
        uint8_t row0_again = gfx_font_glyph_row(ch, 0);
        check(row0 == row0_again);
    }

    check(gfx_font_glyph_row('@', 0) == gfx_font_glyph_row('?', 0));
    check(gfx_font_glyph_row('@', 6) == gfx_font_glyph_row('?', 6));

    return failures == 0 ? 0 : 1;
}
"""


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="os64_gfx_font_") as temp_dir:
        temp_path = Path(temp_dir)
        source_path = temp_path / "graphics_font_test.cpp"
        binary_path = temp_path / "graphics_font_test"
        source_path.write_text(textwrap.dedent(TEST_SOURCE), encoding="utf-8")

        compile_cmd = [
            "g++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(REPO_ROOT / "include"),
            str(REPO_ROOT / "kernel/graphics/graphics_font.cpp"),
            str(source_path),
            "-o",
            str(binary_path),
        ]
        subprocess.run(compile_cmd, check=True)
        subprocess.run([str(binary_path)], check=True)

    print("graphics font test OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
