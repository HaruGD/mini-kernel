#!/usr/bin/env python3
import os
import re
import shutil
import subprocess
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
SERIAL = ROOT / "logs/serial_gui_app.log"
QEMU_LOG = ROOT / "logs/qemu_gui_app.log"
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
    deadline = time.time() + timeout
    needle = marker.encode("ascii")
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


def send_command_async(process: subprocess.Popen, command: str) -> int:
    key_map = {" ": "spc", ".": "dot", "_": "shift-minus", "-": "minus"}
    start = len(serial_bytes())
    for char in command:
        monitor_line(process, f"sendkey {key_map.get(char, char)}")
    monitor_line(process, "sendkey ret")
    return start


def send_command(process: subprocess.Popen, command: str, timeout: float = 30) -> str:
    start = send_command_async(process, command)
    end = wait_for("OS64>", timeout, start)
    return serial_bytes()[start:end].decode(errors="replace")


def wait_program_return(name: str, timeout: float, offset: int) -> int:
    deadline = time.time() + timeout
    pattern = re.compile(
        rb"Running user program: " + re.escape(name.encode("ascii")) +
        rb" \[pid=0x([0-9A-Fa-f]+)")
    while time.time() < deadline:
        match = pattern.search(serial_bytes(), offset)
        if match is not None:
            marker = ("Returned from user program [pid=0x" +
                      match.group(1).decode("ascii") + "]")
            return wait_for(marker, max(0.1, deadline - time.time()), match.end())
        time.sleep(0.1)
    raise TimeoutError(f"timed out waiting for {name!r} to start")


def resources(process: subprocess.Popen) -> tuple[int, ...]:
    output = send_command(process, "resources")
    matches = list(RESOURCE_RE.finditer(output))
    if not matches:
        raise RuntimeError("resource snapshot missing")
    return tuple(int(value, 16) for value in matches[-1].groups())


def require_stable(before: tuple[int, ...], after: tuple[int, ...]) -> None:
    stable = (0, 1, 2, 3, 4, 5, 6, 8, 9)
    if any(before[index] != after[index] for index in stable):
        raise RuntimeError(f"GUI app resource drift: {before} -> {after}")


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    if not data.startswith(b"P6"):
        raise RuntimeError("screendump is not binary PPM")
    offset = 2
    tokens: list[bytes] = []
    while len(tokens) < 3:
        while data[offset:offset + 1] in b" \r\n\t":
            offset += 1
        start = offset
        while data[offset:offset + 1] not in b" \r\n\t":
            offset += 1
        tokens.append(data[start:offset])
    while data[offset:offset + 1] in b" \r\n\t":
        offset += 1
    width, height, maximum = map(int, tokens)
    if maximum != 255:
        raise RuntimeError("unexpected PPM range")
    return width, height, data[offset:offset + width * height * 3]


def near(actual: tuple[int, int, int], expected: tuple[int, int, int]) -> bool:
    return all(abs(value - wanted) <= 8 for value, wanted in zip(actual, expected))


def require_color(path: Path, expected: tuple[int, int, int], minimum: int) -> int:
    width, height, pixels = read_ppm(path)
    if (width, height) != (800, 600):
        raise RuntimeError(f"unexpected display {width}x{height}")
    count = sum(near(tuple(pixels[i:i + 3]), expected)
                for i in range(0, len(pixels), 3))
    if count < minimum:
        raise RuntimeError(f"color {expected} count too small: {count}")
    return count


def require_pixel(path: Path, x: int, y: int,
                  expected: tuple[int, int, int]) -> None:
    width, height, pixels = read_ppm(path)
    if (width, height) != (800, 600):
        raise RuntimeError(f"unexpected display {width}x{height}")
    offset = (y * width + x) * 3
    actual = tuple(pixels[offset:offset + 3])
    if not near(actual, expected):
        raise RuntimeError(f"pixel {x},{y}: {actual} != {expected}")


def screenshot(process: subprocess.Popen, name: str) -> Path:
    path = ROOT / f"logs/gui_app_{name}.ppm"
    path.unlink(missing_ok=True)
    monitor_line(process, f"screendump {path}", 0.05)
    if not path.exists():
        raise RuntimeError(f"missing {name} screenshot")
    return path


def main() -> int:
    SERIAL.parent.mkdir(parents=True, exist_ok=True)
    SERIAL.unlink(missing_ok=True)
    QEMU_LOG.unlink(missing_ok=True)
    run_id = os.getpid()
    esp = Path(f"/tmp/os64_gui_app_{run_id}_esp.img")
    vars_image = Path(f"/tmp/os64_gui_app_{run_id}_vars.fd")
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
        if "start input OK" not in send_command(process, "service start input"):
            raise RuntimeError("input service start failed")
        if "start window OK" not in send_command(process, "service start window"):
            raise RuntimeError("window service start failed")
        baseline = resources(process)

        start = send_command_async(process, "run ugui_launch_c.elf")
        wait_for("[ugui-launch] restricted app", 20, start)
        permission = wait_for("[ugui] permission boundary OK", 20, start)
        wait_program_return("ugui_launch_c.elf", 20, start)
        ready = wait_for("[ugui] focused ready", 20, permission)
        initial = screenshot(process, "initial")
        initial_base = require_color(initial, (24, 36, 61), 10_000)
        initial_accent = require_color(initial, (54, 211, 153), 500)

        key_start = len(serial_bytes())
        monitor_line(process, "sendkey f1")
        redraw = wait_for("[ugui] redraw key=F1 rects=6", 30, key_start)
        redrawn = screenshot(process, "redraw")
        redraw_accent = require_color(redrawn, (236, 72, 153), 500)

        key_start = len(serial_bytes())
        monitor_line(process, "sendkey f2")
        resized = wait_for("[ugui] resized 420x280", 30, key_start)
        resized_screen = screenshot(process, "resized")
        require_pixel(resized_screen, 500, 130, (88, 45, 104))

        key_start = len(serial_bytes())
        monitor_line(process, "sendkey esc")
        done = wait_for("[ugui] lifecycle OK", 30, key_start)
        segment = serial_bytes()[start:done].decode(errors="replace")
        forbidden = ("failed", "event failure", "sequence failure")
        if any(marker in segment for marker in forbidden):
            raise RuntimeError(f"GUI app failure marker: {segment}")
        final = resources(process)
        require_stable(baseline, final)
        print("window SDK GUI application QEMU test OK")
        print(f"pixels initial_base={initial_base} initial_accent={initial_accent} "
              f"redraw_accent={redraw_accent}")
        print(f"resource baseline={baseline}")
        print(f"resource final={final}")
        return 0
    finally:
        if process.poll() is None:
            try:
                monitor_line(process, "quit", 0.05)
                process.wait(timeout=3)
            except (BrokenPipeError, subprocess.TimeoutExpired):
                process.kill()
                process.wait(timeout=3)
        esp.unlink(missing_ok=True)
        vars_image.unlink(missing_ok=True)


if __name__ == "__main__":
    raise SystemExit(main())
