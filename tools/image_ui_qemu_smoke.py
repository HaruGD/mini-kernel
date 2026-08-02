#!/usr/bin/env python3
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SERIAL = ROOT / "logs" / "serial_image_ui.log"
QEMU_LOG = ROOT / "logs" / "qemu_image_ui.log"
SCREEN = ROOT / "logs" / "image_ui.ppm"
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
    needle = marker.encode()
    while time.time() < deadline:
        at = serial_bytes().find(needle, offset)
        if at >= 0:
            return at + len(needle)
        time.sleep(0.1)
    raise TimeoutError(f"timed out waiting for {marker}")


def hmp(process: subprocess.Popen, line: str, delay: float = 0.02) -> None:
    assert process.stdin is not None
    process.stdin.write((line + "\n").encode())
    process.stdin.flush()
    time.sleep(delay)


def type_async(process: subprocess.Popen, command: str) -> int:
    start = len(serial_bytes())
    keys = {" ": "spc", ".": "dot", "_": "shift-minus", "-": "minus"}
    for char in command:
        hmp(process, f"sendkey {keys.get(char, char)}")
    hmp(process, "sendkey ret")
    return start


def shell_command(process: subprocess.Popen, command: str) -> str:
    start = type_async(process, command)
    end = wait_for("OS64>", 30, start)
    return serial_bytes()[start:end].decode(errors="replace")


def resources(process: subprocess.Popen) -> tuple[int, ...]:
    output = shell_command(process, "resources")
    matches = list(RESOURCE_RE.finditer(output))
    if not matches:
        raise RuntimeError("resource snapshot missing")
    return tuple(int(group, 16) for group in matches[-1].groups())


def read_ppm_pixel(path: Path, x: int, y: int) -> tuple[int, int, int]:
    data = path.read_bytes()
    if not data.startswith(b"P6"):
        raise RuntimeError("invalid screendump")
    offset = 2
    tokens = []
    while len(tokens) < 3:
        while data[offset:offset + 1] in b" \r\n\t":
            offset += 1
        start = offset
        while data[offset:offset + 1] not in b" \r\n\t":
            offset += 1
        tokens.append(int(data[start:offset]))
    while data[offset:offset + 1] in b" \r\n\t":
        offset += 1
    width, height, maximum = tokens
    if (width, height, maximum) != (1280, 800, 255):
        raise RuntimeError(f"unexpected framebuffer {tokens}")
    at = offset + (y * width + x) * 3
    return tuple(data[at:at + 3])


def stable(before: tuple[int, ...], after: tuple[int, ...]) -> None:
    for index in (0, 1, 2, 3, 4, 5, 6, 8, 9):
        if before[index] != after[index]:
            raise RuntimeError(f"image/UI resource drift {before} -> {after}")


def run() -> int:
    SERIAL.parent.mkdir(exist_ok=True)
    SERIAL.unlink(missing_ok=True)
    QEMU_LOG.unlink(missing_ok=True)
    SCREEN.unlink(missing_ok=True)
    run_id = os.getpid()
    esp = Path(f"/tmp/os64_image_ui_{run_id}.img")
    variables = Path(f"/tmp/os64_image_ui_{run_id}_vars.fd")
    shutil.copyfile(ROOT / "bin" / "uefi_esp.img", esp)
    shutil.copyfile("/usr/share/OVMF/OVMF_VARS_4M.fd", variables)
    command = [
        "qemu-system-x86_64", "-machine", "q35", "-m", "512M",
        "-cpu", "max", "-smp", "1",
        "-drive", "if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd",
        "-drive", f"if=pflash,format=raw,file={variables}",
        "-drive", f"if=none,id=esp,format=raw,file={esp}",
        "-device", "virtio-blk-pci,drive=esp,bootindex=1",
        "-boot", "menu=off", "-display", "none",
        "-serial", f"file:{SERIAL}", "-monitor", "stdio", "-no-reboot",
        "-d", "guest_errors,cpu_reset", "-D", str(QEMU_LOG),
    ]
    process = subprocess.Popen(command, cwd=ROOT, stdin=subprocess.PIPE,
                               stdout=subprocess.DEVNULL,
                               stderr=subprocess.STDOUT)
    try:
        wait_for("OS64>", 25)
        if "start window OK" not in shell_command(process, "service start window"):
            raise RuntimeError("window stack start failed")
        baseline = resources(process)
        start = type_async(process, "run uimage_launch_c.elf")
        wait_for("[image-ui-launch] restricted public-SDK application", 20, start)
        ready = wait_for("[image-ui] ready native=16x16 bmp=8x8 png=16x16 widgets=8", 30, start)
        hmp(process, f"screendump {SCREEN}", 0.1)
        pixel = read_ppm_pixel(SCREEN, 395, 148)
        if any(abs(actual - expected) > 5
               for actual, expected in zip(pixel, (60, 180, 30))):
            raise RuntimeError(f"decoded BMP presentation pixel mismatch {pixel}")

        hmp(process, "sendkey tab")
        wait_for("[image-ui] action=3 widget=102", 15, ready)
        hmp(process, "sendkey spc")
        activated = wait_for("[image-ui] action=1 widget=102", 15, ready)
        hmp(process, "sendkey tab")
        hmp(process, "sendkey a")
        edited = wait_for("[image-ui] action=2 widget=103", 15, activated)
        hmp(process, "sendkey tab")
        hmp(process, "sendkey spc")
        checked = wait_for("[image-ui] action=2 widget=104 value=1", 15, edited)
        hmp(process, "sendkey tab")
        wait_for("[image-ui] action=3 widget=105", 15, checked)
        hmp(process, "sendkey spc")
        listed = wait_for("[image-ui] action=1 widget=105", 15, checked)
        hmp(process, "sendkey tab")
        wait_for("[image-ui] action=3 widget=106", 15, listed)
        hmp(process, "sendkey spc")
        menu = wait_for("[image-ui] action=1 widget=106", 15, listed)
        hmp(process, "sendkey tab")
        wait_for("[image-ui] action=3 widget=107", 15, menu)
        hmp(process, "sendkey spc")
        scrolled = wait_for("[image-ui] action=1 widget=107", 15, menu)
        hmp(process, "sendkey esc")
        done = wait_for("[image-ui] cleanup OK", 20, scrolled)
        wait_for("[windowd] GUI session released", 20, scrolled)
        final = resources(process)
        stable(baseline, final)
        hmp(process, "sendkey l")
        hmp(process, "sendkey o")
        hmp(process, "sendkey c")
        hmp(process, "sendkey k")
        hmp(process, "sendkey s")
        hmp(process, "sendkey ret")
        locks = wait_for("=== CONCURRENCY ===", 10, done)
        wait_for("order_violations=0x0000000000000000 recursion_violations=0x0000000000000000 release_violations=0x0000000000000000", 10, locks)
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill(); process.wait(timeout=3)
        if process.stdin is not None:
            process.stdin.close()
        esp.unlink(missing_ok=True)
        variables.unlink(missing_ok=True)
    text = SERIAL.read_text(errors="replace")
    if "KERNEL PANIC" in text or "CPU EMERGENCY" in text:
        raise RuntimeError("fatal kernel path observed")
    print(f"image/widget QEMU presentation OK pixel={pixel}")
    print(f"resource baseline={baseline}")
    print(f"resource final={final}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(run())
    except (TimeoutError, RuntimeError) as error:
        print(error, file=sys.stderr)
        raise SystemExit(1)
