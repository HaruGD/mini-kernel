#!/usr/bin/env python3
import os
import re
import shutil
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
SERIAL = ROOT / "logs/serial_display_present.log"
QEMU_LOG = ROOT / "logs/qemu_display_present.log"
SCREEN = ROOT / "logs/display_present.ppm"
RESOURCE_RE = re.compile(
    r"processes=0x([0-9A-Fa-f]+) mappings=0x([0-9A-Fa-f]+) "
    r"handles=0x([0-9A-Fa-f]+) mailboxes=0x([0-9A-Fa-f]+) services=0x([0-9A-Fa-f]+).*?"
    r"shared=0x([0-9A-Fa-f]+) surfaces=0x([0-9A-Fa-f]+).*?"
    r"pmm_free=0x([0-9A-Fa-f]+).*?heap_used=0x([0-9A-Fa-f]+) heap_mapped=0x([0-9A-Fa-f]+)",
    re.DOTALL,
)


@dataclass(frozen=True)
class PpmImage:
    width: int
    height: int
    pixels: bytes

    def rgb_at(self, x: int, y: int) -> tuple[int, int, int]:
        offset = (y * self.width + x) * 3
        return tuple(self.pixels[offset:offset + 3])


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


def send_present(process: subprocess.Popen,
                 capture: bool = False,
                 timeout: float = 40) -> str:
    command = "run usdk_c.elf display-present"
    key_map = {" ": "spc", ".": "dot", "_": "shift-minus", "-": "minus"}
    start = len(serial_bytes())
    for char in command:
        monitor_line(process, f"sendkey {key_map.get(char, char)}")
    monitor_line(process, "sendkey ret")
    first = wait_for("[displayd] accepted generation=", timeout, start)
    second = wait_for("[displayd] accepted generation=", timeout, first)
    if capture:
        monitor_line(process, f"screendump {SCREEN}", 0.3)
    complete = 0
    for _ in range(3):
        send_command(process, "service status display")
        data = serial_bytes()
        index = data.find(b"[display-client] present path OK", second)
        if index >= 0:
            complete = index + len(b"[display-client] present path OK")
            break
    if complete == 0:
        complete = wait_for("[display-client] present path OK", timeout, second)
    end = wait_for("OS64>", timeout, complete)
    return serial_bytes()[start:end].decode(errors="replace")


def parse_resources(text: str) -> tuple[int, ...]:
    matches = list(RESOURCE_RE.finditer(text))
    if not matches:
        raise RuntimeError("resource snapshot missing")
    return tuple(int(value, 16) for value in matches[-1].groups())


def require_transient_resources_released(baseline: tuple[int, ...],
                                         current: tuple[int, ...],
                                         context: str) -> None:
    # Returned-process image/page-table history is intentionally retained until
    # its process-table slot is reused. Present-owned mappings, handles, IPC,
    # services, surface objects, and heap state must still return immediately.
    stable_indices = (0, 1, 2, 3, 4, 5, 6, 8, 9)
    if any(baseline[index] != current[index] for index in stable_indices):
        raise RuntimeError(f"resource drift {context}: {baseline} -> {current}")
    if current[1] != baseline[1] or current[2] != 0 or current[6] != 0:
        raise RuntimeError(f"present resources not released {context}: {current}")


def read_ppm(path: Path) -> PpmImage:
    data = path.read_bytes()
    if not data.startswith(b"P6"):
        raise RuntimeError("display screendump is not binary PPM")
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
        raise RuntimeError("display screendump is truncated")
    return PpmImage(width, height, data[offset:offset + width * height * 3])


def near(rgb: tuple[int, int, int], expected: tuple[int, int, int]) -> bool:
    return all(abs(actual - wanted) <= 8 for actual, wanted in zip(rgb, expected))


def validate_screen(image: PpmImage) -> None:
    if image.width != 800 or image.height != 600:
        raise RuntimeError(f"unexpected display size {image.width}x{image.height}")
    base = (38, 70, 126)
    damage = (46, 184, 92)
    base_count = 0
    damage_count = 0
    for offset in range(0, len(image.pixels), 3):
        rgb = tuple(image.pixels[offset:offset + 3])
        base_count += near(rgb, base)
        damage_count += near(rgb, damage)
    if base_count < 80_000 or damage_count < 20_000:
        raise RuntimeError(
            f"presented colors missing base={base_count} damage={damage_count}"
        )


def require_present(output: str) -> None:
    required = [
        "[display-client] direct display denied",
        "[display-client] full ACK generation=",
        "[display-client] partial ACK generation=",
        "[display-client] present path OK",
        "[display-test] restricted client launched",
    ]
    missing = [marker for marker in required if marker not in output]
    if missing:
        raise RuntimeError(f"present client missing markers: {missing}")


def main() -> int:
    SERIAL.parent.mkdir(parents=True, exist_ok=True)
    for path in (SERIAL, QEMU_LOG, SCREEN):
        path.unlink(missing_ok=True)
    run_id = os.getpid()
    esp = Path(f"/tmp/os64_display_present_{run_id}_esp.img")
    vars_image = Path(f"/tmp/os64_display_present_{run_id}_vars.fd")
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
    process = subprocess.Popen(
        command, cwd=ROOT, stdin=subprocess.PIPE,
        stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT,
    )
    try:
        wait_for("OS64>", 25)
        output = send_command(process, "service start display")
        if "[displayd] ready pid=" not in output or "[usvcctl] start display OK" not in output:
            raise RuntimeError("display service did not start under supervision")
        require_present(send_present(process))
        baseline = parse_resources(send_command(process, "resources"))

        output = send_present(process, capture=True)
        require_present(output)
        first = parse_resources(send_command(process, "resources"))
        require_transient_resources_released(baseline, first, "after present")
        validate_screen(read_ppm(SCREEN))

        restart_offset = len(serial_bytes())
        output = send_command(process, "service crash display")
        if "[serviced] injected crash display" not in output or \
                "[usvcctl] crash display OK" not in output:
            raise RuntimeError("display crash injection failed")
        fallback = send_command(process, "resources")
        if "=== RESOURCES ===" not in fallback:
            raise RuntimeError("terminal fallback was not usable after display crash")
        restarted = 0
        for _ in range(12):
            time.sleep(0.1)
            send_command(process, "service status display")
            data = serial_bytes()
            index = data.find(b"[serviced] auto-restart display attempt=1",
                              restart_offset)
            if index >= 0:
                restarted = index
                break
        if restarted == 0:
            raise RuntimeError("display service was not auto-restarted")
        wait_for("[displayd] ready pid=", 10, restarted)
        output = send_present(process)
        require_present(output)
        final = parse_resources(send_command(process, "resources"))
        require_transient_resources_released(baseline, final, "after restart")

        output = send_command(process, "service exit")
        if "[serviced] stopped display" not in output or \
                "[usvcctl] exit service OK" not in output:
            raise RuntimeError("display service did not stop cleanly")
        log = serial_bytes().decode(errors="replace")
        if "KERNEL PANIC" in log or "Double fault" in log:
            raise RuntimeError("kernel fault observed")
    except (RuntimeError, TimeoutError) as error:
        print(f"display present smoke failed: {error}")
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
    print("display present smoke OK: full + partial + permission + restart + cleanup")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
