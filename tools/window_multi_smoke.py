#!/usr/bin/env python3
import os
import re
import shutil
import subprocess
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
SERIAL = ROOT / "logs/serial_window_multi.log"
QEMU_LOG = ROOT / "logs/qemu_window_multi.log"
RESOURCE_RE = re.compile(
    r"processes=0x([0-9A-Fa-f]+) mappings=0x([0-9A-Fa-f]+) "
    r"handles=0x([0-9A-Fa-f]+) mailboxes=0x([0-9A-Fa-f]+) services=0x([0-9A-Fa-f]+).*?"
    r"shared=0x([0-9A-Fa-f]+) surfaces=0x([0-9A-Fa-f]+).*?"
    r"pmm_free=0x([0-9A-Fa-f]+).*?heap_used=0x([0-9A-Fa-f]+) heap_mapped=0x([0-9A-Fa-f]+)",
    re.DOTALL,
)


def serial_bytes() -> bytes:
    try:
        return SERIAL.read_bytes()
    except FileNotFoundError:
        return b""


def wait_for(marker: str, timeout: float, offset: int = 0) -> int:
    needle = marker.encode("ascii")
    deadline = time.time() + timeout
    while time.time() < deadline:
        index = serial_bytes().find(needle, offset)
        if index >= 0:
            return index + len(needle)
        time.sleep(0.1)
    raise TimeoutError(f"timed out waiting for {marker!r}")


def monitor_line(process: subprocess.Popen, line: str, delay: float = 0.03) -> None:
    assert process.stdin is not None
    process.stdin.write((line + "\n").encode("ascii"))
    process.stdin.flush()
    time.sleep(delay)


def send_command(process: subprocess.Popen, command: str, timeout: float = 30) -> str:
    key_map = {" ": "spc", ".": "dot", "_": "shift-minus", "-": "minus"}
    start = len(serial_bytes())
    for char in command:
        monitor_line(process, f"sendkey {key_map.get(char, char)}")
    monitor_line(process, "sendkey ret")
    end = wait_for("OS64>", timeout, start)
    return serial_bytes()[start:end].decode(errors="replace")


def send_service_command(process: subprocess.Popen, command: str) -> str:
    start = len(serial_bytes())
    output = send_command(process, command)
    if "[usvcctl]" not in output:
        raise RuntimeError(f"service command failed: {output}")
    return serial_bytes()[start:].decode(errors="replace")


def drive_guest(process: subprocess.Popen, timeout: float = 30) -> None:
    start = len(serial_bytes())
    output = send_command(process, "run uyield_c.elf", timeout)
    if "=== uyield_c.elf ===" not in output or "Returned from user program" not in output:
        raise RuntimeError(f"guest scheduling drive failed: {output}")
    wait_for("OS64>", timeout, start)


def drive_until(process: subprocess.Popen,
                marker: str,
                offset: int,
                timeout: float = 45) -> int:
    deadline = time.time() + timeout
    while time.time() < deadline:
        data = serial_bytes()
        index = data.find(marker.encode("ascii"), offset)
        if index >= 0:
            return index + len(marker)
        drive_guest(process)
        time.sleep(0.05)
    raise TimeoutError(f"timed out driving guest to {marker!r}")


def send_resources(process: subprocess.Popen) -> tuple[int, ...]:
    output = send_command(process, "resources")
    matches = list(RESOURCE_RE.finditer(output))
    if not matches:
        raise RuntimeError("resource snapshot missing")
    return tuple(int(value, 16) for value in matches[-1].groups())


def require_stable(baseline: tuple[int, ...], final: tuple[int, ...]) -> None:
    stable_indices = (0, 1, 2, 3, 4, 5, 6, 8, 9)
    if any(baseline[index] != final[index] for index in stable_indices):
        raise RuntimeError(f"multiwindow resource drift: {baseline} -> {final}")


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    if not data.startswith(b"P6"):
        raise RuntimeError("screendump is not binary PPM")
    offset = 2
    tokens: list[bytes] = []
    while len(tokens) < 3:
        while data[offset:offset + 1] in b" \r\n\t":
            offset += 1
        if data[offset:offset + 1] == b"#":
            offset = data.index(b"\n", offset) + 1
            continue
        start = offset
        while data[offset:offset + 1] not in b" \r\n\t":
            offset += 1
        tokens.append(data[start:offset])
    while data[offset:offset + 1] in b" \r\n\t":
        offset += 1
    width, height, maximum = map(int, tokens)
    if maximum != 255 or len(data) - offset < width * height * 3:
        raise RuntimeError("screendump is truncated")
    return width, height, data[offset:offset + width * height * 3]


def near(actual: tuple[int, int, int], expected: tuple[int, int, int]) -> bool:
    return all(abs(value - wanted) <= 8 for value, wanted in zip(actual, expected))


def require_pixel(path: Path,
                  x: int,
                  y: int,
                  expected: tuple[int, int, int],
                  name: str) -> None:
    width, height, pixels = read_ppm(path)
    if width != 800 or height != 600:
        raise RuntimeError(f"unexpected display size {width}x{height}")
    offset = (y * width + x) * 3
    actual = tuple(pixels[offset:offset + 3])
    if not near(actual, expected):
        raise RuntimeError(f"{name} pixel mismatch at {x},{y}: {actual}")


def require_color_count(path: Path,
                        expected: tuple[int, int, int],
                        minimum: int,
                        name: str) -> int:
    width, height, pixels = read_ppm(path)
    if width != 800 or height != 600:
        raise RuntimeError(f"unexpected display size {width}x{height}")
    count = sum(
        near(tuple(pixels[offset:offset + 3]), expected)
        for offset in range(0, len(pixels), 3)
    )
    if count < minimum:
        raise RuntimeError(f"{name} color count too small: {count}")
    return count


def require_color_max(path: Path,
                      expected: tuple[int, int, int],
                      maximum: int,
                      name: str) -> int:
    width, height, pixels = read_ppm(path)
    count = sum(
        near(tuple(pixels[offset:offset + 3]), expected)
        for offset in range(0, len(pixels), 3)
    )
    if count > maximum:
        raise RuntimeError(f"{name} color unexpectedly remains: {count}")
    return count


def require_color_max_region(path: Path,
                             expected: tuple[int, int, int],
                             maximum: int,
                             bounds: tuple[int, int, int, int],
                             name: str) -> int:
    width, height, pixels = read_ppm(path)
    left, top, right, bottom = bounds
    count = 0
    for y in range(max(0, top), min(height, bottom)):
        for x in range(max(0, left), min(width, right)):
            offset = (y * width + x) * 3
            count += near(tuple(pixels[offset:offset + 3]), expected)
    if count > maximum:
        raise RuntimeError(f"{name} color unexpectedly remains: {count}")
    return count


def screenshot(process: subprocess.Popen, name: str) -> Path:
    path = ROOT / f"logs/window_multi_{name}.ppm"
    path.unlink(missing_ok=True)
    monitor_line(process, f"screendump {path}", 0.05)
    if not path.exists():
        raise RuntimeError(f"missing {name} screendump")
    return path


def main() -> int:
    SERIAL.parent.mkdir(parents=True, exist_ok=True)
    SERIAL.unlink(missing_ok=True)
    QEMU_LOG.unlink(missing_ok=True)
    run_id = os.getpid()
    esp = Path(f"/tmp/os64_window_multi_{run_id}_esp.img")
    vars_image = Path(f"/tmp/os64_window_multi_{run_id}_vars.fd")
    shutil.copyfile(ROOT / "bin/uefi_esp.img", esp)
    shutil.copyfile("/usr/share/OVMF/OVMF_VARS_4M.fd", vars_image)
    command = [
        "qemu-system-x86_64", "-machine", "q35", "-m", "512M", "-cpu", "max",
        "-drive", "if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd",
        "-drive", f"if=pflash,format=raw,file={vars_image}",
        "-drive", f"if=none,id=esp,format=raw,file={esp}",
        "-device", "virtio-blk-pci,drive=esp,bootindex=1", "-boot", "menu=off",
        "-vga", "none", "-device", "VGA,xres=800,yres=600", "-display", "none",
        "-serial", f"file:{SERIAL}", "-monitor", "stdio", "-no-reboot",
        "-d", "guest_errors,cpu_reset,int", "-D", str(QEMU_LOG),
    ]
    process = subprocess.Popen(command, cwd=ROOT, stdin=subprocess.PIPE,
                               stdout=subprocess.DEVNULL,
                               stderr=subprocess.STDOUT)
    try:
        wait_for("OS64>", 25)
        output = send_service_command(process, "service start input")
        if "start input OK" not in output:
            raise RuntimeError("input service did not start")
        output = send_service_command(process, "service start window")
        if "[windowd] ready" not in output or "start window OK" not in output:
            raise RuntimeError("window pipeline did not start")
        baseline = send_resources(process)

        start = len(serial_bytes())
        output = send_command(process, "run usdk_c.elf window-multi")
        if "restricted multiwindow clients launched" not in output:
            raise RuntimeError(f"multiwindow clients did not launch: {output}")

        back = drive_until(process, "[window-multi-back] visible", start)
        overlap = drive_until(process, "[window-multi-front] overlap visible", back)
        overlap_screen = screenshot(process, "overlap")
        require_color_count(overlap_screen, (36, 72, 164), 20_000, "back-only")
        require_color_count(overlap_screen, (184, 64, 48), 50_000,
                            "front-overlap")

        hidden = drive_until(process,
                             "[window-multi-front] hidden background revealed",
                             overlap)
        hidden_screen = screenshot(process, "hidden")
        require_color_count(hidden_screen, (36, 72, 164), 10_000,
                            "hide reveal")
        require_color_max_region(hidden_screen, (184, 64, 48), 100,
                                 (260, 180, 620, 460), "hidden front")

        shown = drive_until(process, "[window-multi-front] shown and raised", hidden)
        moved = drive_until(process,
                            "[window-multi-front] moved with edge clipping", shown)
        moved_screen = screenshot(process, "moved")
        require_color_count(moved_screen, (184, 64, 48), 30_000,
                            "left-clipped front")

        resized = drive_until(process, "[window-multi-front] resized atomically", moved)
        partial = drive_until(process, "[window-multi-front] partial damage visible",
                              resized)
        partial_screen = screenshot(process, "partial")
        require_color_count(partial_screen, (232, 72, 152), 8_000,
                            "partial damage")
        require_color_count(partial_screen, (48, 152, 92), 7_000,
                            "resized surface")

        front_done = drive_until(process, "[window-multi-front] lifecycle OK", partial)
        back_marker = b"[window-multi-back] lifecycle OK"
        back_index = serial_bytes().find(back_marker, start)
        back_done = (back_index + len(back_marker)) if back_index >= 0 else \
            drive_until(process, "[window-multi-back] lifecycle OK", front_done, 120)
        complete = max(front_done, back_done)
        final = send_resources(process)
        require_stable(baseline, final)

        log = serial_bytes()[start:complete].decode(errors="replace")
        required = (
            "[windowd] created id=1",
            "[windowd] created id=2",
            "[windowd] hidden id=2",
            "[windowd] shown id=2",
            "[windowd] moved id=2",
            "[windowd] resized id=2",
            "rects=5",
            "[windowd] destroyed id=1",
            "[windowd] destroyed id=2",
        )
        if any(marker not in log for marker in required):
            raise RuntimeError("multiwindow lifecycle markers are incomplete")
        if "KERNEL PANIC" in log or "Double fault" in log:
            raise RuntimeError("kernel fault observed")
    except (RuntimeError, TimeoutError) as error:
        print(f"multiwindow smoke failed: {error}")
        return 1
    finally:
        if process.poll() is None:
            monitor_line(process, "quit")
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        if process.stdin is not None:
            process.stdin.close()
        esp.unlink(missing_ok=True)
        vars_image.unlink(missing_ok=True)
    print("multiwindow smoke OK: overlap + z-order + hide/show + move/resize + "
          "chunked damage + arbitrary destroy order + zero stable drift")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
