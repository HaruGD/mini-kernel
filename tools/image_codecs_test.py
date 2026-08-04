#!/usr/bin/env python3
import binascii
import struct
import subprocess
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "build" / "assets"

HARNESS = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <os64/image.h>
#include <os64/os64.h>

void* os_malloc(size_t size) { return malloc(size); }
void os_free(void* value) { free(value); }
void* os_realloc(void* value, size_t size) { return realloc(value, size); }
void* os_memset(void* out, int value, size_t size) { return memset(out, value, size); }
void* os_memcpy(void* out, const void* in, size_t size) { return memcpy(out, in, size); }
void* os_read_file_alloc(const char* path, uint32_t* size_out) {
    (void)path; if (size_out) *size_out = 0; return 0;
}

static uint8_t* read_file(const char* path, uint32_t* size) {
    FILE* file = fopen(path, "rb");
    assert(file);
    assert(fseek(file, 0, SEEK_END) == 0);
    long length = ftell(file);
    assert(length > 0 && length <= 0x7fffffff);
    rewind(file);
    uint8_t* data = malloc((size_t)length);
    assert(data && fread(data, 1, (size_t)length, file) == (size_t)length);
    fclose(file);
    *size = (uint32_t)length;
    return data;
}

int main(int argc, char** argv) {
    assert(argc == 8);
    uint32_t native_size, bmp_size, png_size;
    uint8_t* native = read_file(argv[1], &native_size);
    uint8_t* bmp = read_file(argv[2], &bmp_size);
    uint8_t* png = read_file(argv[3], &png_size);
    OsImage native_image, bmp_image, png_image;
    os_image_init(&native_image);
    os_image_init(&bmp_image);
    os_image_init(&png_image);
    assert(os_image_decode(native, native_size, OS_IMAGE_DECODE_AUTO,
                           &native_image) == 0);
    assert(os_image_decode(bmp, bmp_size, OS_IMAGE_DECODE_AUTO,
                           &bmp_image) == 0);
    assert(os_image_decode(png, png_size, OS_IMAGE_DECODE_AUTO,
                           &png_image) == 0);
    assert(native_image.width == 16 && native_image.height == 16);
    assert(png_image.width == 16 && png_image.height == 16);
    assert(bmp_image.width == 8 && bmp_image.height == 8);
    assert(memcmp(native_image.pixels, png_image.pixels,
                  native_image.allocation_bytes) == 0);

    native[32] ^= 0x80;
    OsImage bad;
    os_image_init(&bad);
    assert(os_image_decode(native, native_size, OS_IMAGE_DECODE_OSIMG, &bad) < 0);
    assert(bad.pixels == 0);
    assert(os_image_decode(bmp, 53, OS_IMAGE_DECODE_BMP, &bad) < 0);
    assert(bad.pixels == 0);
    png[png_size - 1] ^= 1;
    assert(os_image_decode(png, png_size, OS_IMAGE_DECODE_PNG, &bad) < 0);
    assert(bad.pixels == 0);
    for (int index = 4; index < argc; index++) {
        uint32_t malformed_size;
        uint8_t* malformed = read_file(argv[index], &malformed_size);
        assert(os_image_decode(malformed, malformed_size,
                               OS_IMAGE_DECODE_PNG, &bad) < 0);
        assert(bad.pixels == 0);
        free(malformed);
    }

    uint32_t canvas_pixels[16];
    for (uint32_t i = 0; i < 16; i++) canvas_pixels[i] = OS_RGB(20, 40, 60);
    OsSurfaceCanvas canvas = {canvas_pixels, 4, 4, 4, OS64_PIXEL_FORMAT_RGB};
    OsImage alpha;
    os_image_init(&alpha);
    alpha.width = alpha.height = alpha.stride_pixels = 1;
    alpha.allocation_bytes = 4;
    alpha.pixels = malloc(4);
    assert(alpha.pixels);
    alpha.pixels[0] = 0x80640000u; /* premultiplied half-alpha red */
    OsRect damage;
    assert(os_surface_canvas_draw_image(&canvas, &alpha, (OsRect){0,0,1,1},
        (OsRect){1,1,2,2}, OS_IMAGE_SCALE_NEAREST, &damage) == 0);
    assert(damage.x == 1 && damage.y == 1 && damage.width == 2 && damage.height == 2);
    assert(canvas_pixels[1 * 4 + 1] == OS_RGB(110, 20, 30));
    assert(canvas_pixels[0] == OS_RGB(20, 40, 60));

    OsImage wide;
    os_image_init(&wide);
    wide.width = 4; wide.height = 2; wide.stride_pixels = 4;
    wide.allocation_bytes = 32;
    wide.pixels = calloc(1, 32);
    assert(wide.pixels);
    OsRect fitted;
    assert(os_image_fit_rect(&wide, (OsRect){10,20,100,100}, &fitted) == 0);
    assert(fitted.x == 10 && fitted.y == 45 &&
           fitted.width == 100 && fitted.height == 50);
    assert(os_image_fit_rect(&wide, (OsRect){INT32_MAX,0,2,2}, &fitted) ==
           OS_ERR_OUT_OF_RANGE);

    os_image_destroy(&alpha);
    os_image_destroy(&wide);
    os_image_destroy(&native_image);
    os_image_destroy(&bmp_image);
    os_image_destroy(&png_image);
    free(native); free(bmp); free(png);
    puts("native/BMP/PNG decode and alpha scaling tests OK");
    return 0;
}
'''


def main() -> int:
    subprocess.run(["python3", str(ROOT / "tools" / "build_image_fixtures.py"),
                    "--output-dir", str(ASSETS)], check=True, cwd=ROOT)
    with tempfile.TemporaryDirectory(prefix="os64-image-") as directory_name:
        directory = Path(directory_name)
        source = directory / "test.c"
        binary = directory / "test"
        source.write_text(HARNESS)
        def chunk(kind: bytes, payload: bytes) -> bytes:
            return (struct.pack(">I", len(payload)) + kind + payload +
                    struct.pack(">I", binascii.crc32(kind + payload) & 0xFFFFFFFF))

        def png_fixture(name: str, width: int, height: int, color: int,
                        raw: bytes) -> Path:
            path = directory / name
            path.write_bytes(
                b"\x89PNG\r\n\x1a\n" +
                chunk(b"IHDR", struct.pack(">IIBBBBB", width, height,
                                             8, color, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))
            return path

        oversized = png_fixture("oversized.png", 5000, 1, 6, b"\0")
        invalid_filter = png_fixture("invalid-filter.png", 1, 1, 6,
                                     bytes((5, 1, 2, 3, 255)))
        unsupported = png_fixture("grayscale.png", 1, 1, 0,
                                  bytes((0, 128)))
        expanded = png_fixture("expanded.png", 1, 1, 6, bytes(8192))
        subprocess.run([
            "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
            "-I", str(ROOT / "include"),
            "-I", str(ROOT / "user" / "sdk" / "include"),
            "-I", str(ROOT / "user" / "sdk" / "src"),
            str(source), str(ROOT / "user" / "sdk" / "src" / "image.c"),
            str(ROOT / "user" / "sdk" / "src" / "image_png.c"),
            "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary), str(ASSETS / "image_demo.osimg"),
                        str(ASSETS / "image_demo.bmp"),
                        str(ASSETS / "image_demo.png"), str(oversized),
                        str(invalid_filter), str(unsupported), str(expanded)],
                       check=True)
    converted = Path(tempfile.gettempdir()) / "os64_image_repeat.osimg"
    subprocess.run(["python3", str(ROOT / "tools" / "png_to_osimg.py"),
                    str(ASSETS / "image_demo.png"), str(converted)], check=True)
    assert converted.read_bytes() == (ASSETS / "image_demo.osimg").read_bytes()
    converted.unlink(missing_ok=True)
    print("PNG-to-osimg deterministic conversion OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
