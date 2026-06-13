#!/usr/bin/env python3
import shutil
import subprocess
import time
from pathlib import Path


def count_nonblack_ppm(path: Path) -> tuple[int, int, int]:
    data = path.read_bytes()
    if not data.startswith(b"P6\n"):
        raise SystemExit("screendump is not a binary PPM")

    offset = 3
    parts: list[bytes] = []
    while len(parts) < 3:
        while data[offset:offset + 1] in (b" ", b"\n", b"\r", b"\t"):
            offset += 1
        if data[offset:offset + 1] == b"#":
            while data[offset:offset + 1] != b"\n":
                offset += 1
            continue
        start = offset
        while data[offset:offset + 1] not in (b" ", b"\n", b"\r", b"\t"):
            offset += 1
        parts.append(data[start:offset])

    while data[offset:offset + 1] in (b" ", b"\n", b"\r", b"\t"):
        offset += 1

    width = int(parts[0])
    height = int(parts[1])
    pixels = data[offset:]
    nonblack = 0
    whiteish = 0
    for i in range(0, len(pixels), 3):
        r, g, b = pixels[i], pixels[i + 1], pixels[i + 2]
        if r != 0 or g != 0 or b != 0:
            nonblack += 1
        if r > 180 and g > 180 and b > 180:
            whiteish += 1
    return width, height, min(nonblack, whiteish)


def main() -> int:
    screen = Path("uefi_screen.ppm")
    screen.unlink(missing_ok=True)
    esp_image = Path("bin/uefi_esp.screen.img")
    root_image = Path("bin/os64.screen.img")
    esp_image.unlink(missing_ok=True)
    root_image.unlink(missing_ok=True)
    shutil.copyfile("bin/uefi_esp.img", esp_image)
    shutil.copyfile("bin/os64.bin", root_image)

    qemu = [
        "qemu-system-x86_64",
        "-drive", "if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd",
        "-drive", "if=pflash,format=raw,file=./bin/OVMF_VARS_4M.fd",
        "-drive", f"if=none,id=uefi_esp,format=raw,file={esp_image}",
        "-device", "virtio-blk-pci,drive=uefi_esp,bootindex=1",
        "-drive", f"format=raw,file={root_image},if=ide,index=0",
        "-serial", "none",
        "-monitor", "stdio",
        "-display", "none",
    ]

    proc = subprocess.Popen(qemu, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    try:
        time.sleep(12.0)
        assert proc.stdin is not None
        proc.stdin.write(b"screendump uefi_screen.ppm\n")
        proc.stdin.flush()
        time.sleep(1.0)
        proc.stdin.write(b"quit\n")
        proc.stdin.flush()
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
    finally:
        if proc.stdin is not None:
            try:
                proc.stdin.close()
            except BrokenPipeError:
                pass
        esp_image.unlink(missing_ok=True)
        root_image.unlink(missing_ok=True)

    if not screen.exists():
        print("UEFI screen smoke missing screendump")
        return 1

    width, height, visible_pixels = count_nonblack_ppm(screen)
    if width == 0 or height == 0 or visible_pixels < 100:
        print(f"UEFI screen smoke blank: {width}x{height} visible={visible_pixels}")
        return 1

    print(f"UEFI screen smoke OK: {width}x{height} visible={visible_pixels}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
