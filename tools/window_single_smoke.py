#!/usr/bin/env python3
import os
import re
import shutil
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
SERIAL = ROOT / "logs/serial_window_single.log"
QEMU_LOG = ROOT / "logs/qemu_window_single.log"
SCREEN = ROOT / "logs/window_single.ppm"
RECONNECT_SCREEN = ROOT / "logs/window_reconnect.ppm"
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


def send_resources(process: subprocess.Popen, timeout: float = 30) -> str:
    command = "resources"
    start = len(serial_bytes())
    for char in command:
        monitor_line(process, f"sendkey {char}")
    monitor_line(process, "sendkey ret")
    body = wait_for("=== RESOURCES ===", timeout, start)
    end = wait_for("OS64>", timeout, body)
    return serial_bytes()[start:end].decode(errors="replace")


def send_service_command(process: subprocess.Popen,
                         command: str,
                         timeout: float = 30) -> str:
    key_map = {" ": "spc", ".": "dot", "_": "shift-minus", "-": "minus"}
    start = len(serial_bytes())
    for char in command:
        monitor_line(process, f"sendkey {key_map.get(char, char)}")
    monitor_line(process, "sendkey ret")
    reply = wait_for("[usvcctl]", timeout, start)
    end = wait_for("OS64>", timeout, reply)
    return serial_bytes()[start:end].decode(errors="replace")


def drive_guest(process: subprocess.Popen, timeout: float = 30) -> None:
    command = "run uyield_c.elf"
    key_map = {" ": "spc", ".": "dot", "_": "shift-minus", "-": "minus"}
    start = len(serial_bytes())
    for char in command:
        monitor_line(process, f"sendkey {key_map.get(char, char)}")
    monitor_line(process, "sendkey ret")
    body = wait_for("=== uyield_c.elf ===", timeout, start)
    returned = wait_for("Returned from user program", timeout, body)
    wait_for("OS64>", timeout, returned)


def drive_until(process: subprocess.Popen,
                marker: str,
                offset: int,
                timeout: float = 35) -> int:
    deadline = time.time() + timeout
    while time.time() < deadline:
        data = serial_bytes()
        index = data.find(marker.encode("ascii"), offset)
        if index >= 0:
            return index + len(marker)
        drive_guest(process)
        time.sleep(0.1)
    raise TimeoutError(f"timed out driving guest to {marker!r}")


def parse_resources(text: str) -> tuple[int, ...]:
    matches = list(RESOURCE_RE.finditer(text))
    if not matches:
        raise RuntimeError("resource snapshot missing")
    return tuple(int(value, 16) for value in matches[-1].groups())


def require_window_resources_released(baseline: tuple[int, ...],
                                      current: tuple[int, ...],
                                      context: str) -> None:
    # Returned process image/page-table pages remain cached until slot reuse.
    # Active mappings, handles, mailboxes, services, surface objects and heap
    # must return to the warmed window-service baseline immediately.
    stable_indices = (0, 1, 2, 3, 4, 5, 6, 8, 9)
    if any(baseline[index] != current[index] for index in stable_indices):
        raise RuntimeError(f"resource drift {context}: {baseline} -> {current}")


def read_ppm(path: Path) -> PpmImage:
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
    return PpmImage(width, height, data[offset:offset + width * height * 3])


def near(actual: tuple[int, int, int], expected: tuple[int, int, int]) -> bool:
    return all(abs(value - wanted) <= 8 for value, wanted in zip(actual, expected))


def validate_colors(path: Path,
                    expected: tuple[tuple[int, int, int], ...],
                    minimum: int) -> tuple[int, ...]:
    image = read_ppm(path)
    if image.width != 800 or image.height != 600:
        raise RuntimeError(f"unexpected display size {image.width}x{image.height}")
    counts = [0] * len(expected)
    for offset in range(0, len(image.pixels), 3):
        pixel = tuple(image.pixels[offset:offset + 3])
        for index, color in enumerate(expected):
            counts[index] += near(pixel, color)
    if any(count < minimum for count in counts):
        raise RuntimeError(f"deterministic window colors missing: {counts}")
    return tuple(counts)


def launch_demo(process: subprocess.Popen, mode: str) -> int:
    start = len(serial_bytes())
    output = send_command(process, f"run usdk_c.elf window-{mode}")
    if f"restricted window-{mode}-client launched" not in output:
        raise RuntimeError(f"{mode} client did not launch: {output}")
    return start


def main() -> int:
    SERIAL.parent.mkdir(parents=True, exist_ok=True)
    for path in (SERIAL, QEMU_LOG, SCREEN, RECONNECT_SCREEN):
        path.unlink(missing_ok=True)
    run_id = os.getpid()
    esp = Path(f"/tmp/os64_window_single_{run_id}_esp.img")
    vars_image = Path(f"/tmp/os64_window_single_{run_id}_vars.fd")
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
        output = send_service_command(process, "service start input")
        if "[inputd] ready pid=" not in output or "start input OK" not in output:
            raise RuntimeError("input service did not start under supervision")
        output = send_service_command(process, "service start window")
        required_start = (
            "[displayd] ready pid=", "[windowd] ready pid=",
            "[serviced] started display", "[serviced] started window",
            "start window OK",
        )
        if any(marker not in output for marker in required_start):
            raise RuntimeError(f"window pipeline did not start: {output}")

        warm = launch_demo(process, "exit")
        drive_until(process, "[window-client] exiting without DESTROY", warm)
        drive_until(process, "[windowd] owner exit cleanup", warm)
        baseline = parse_resources(send_resources(process))

        happy = launch_demo(process, "present")
        visible = drive_until(
            process, "[window-client] deterministic frame visible", happy, 40
        )
        monitor_line(process, f"screendump {SCREEN}", 0.3)
        happy_counts = validate_colors(
            SCREEN,
            ((62, 80, 156), (232, 188, 72), (224, 112, 48)),
            12_000,
        )
        complete = drive_until(process, "[window-client] lifecycle OK", visible)
        required_happy = (
            "[window-client] direct display denied",
            "[window-client] CREATE ACK",
            "[window-client] SET_SURFACE ACK",
            "[window-client] DAMAGE ACK",
            "[window-client] DESTROY ACK",
            "[windowd] created id=",
            "[windowd] surface replaced content=",
            "[windowd] damage accepted content=",
            "[windowd] destroyed id=",
        )
        happy_log = serial_bytes()[happy:complete].decode(errors="replace")
        if any(marker not in happy_log for marker in required_happy):
            raise RuntimeError("happy-path lifecycle markers are incomplete")
        require_window_resources_released(
            baseline,
            parse_resources(send_resources(process)),
            "after CREATE/SET_SURFACE/DAMAGE/DESTROY",
        )

        leaked = launch_demo(process, "exit")
        drive_until(process, "[window-client] exiting without DESTROY", leaked)
        drive_until(process, "[windowd] owner exit cleanup", leaked)
        require_window_resources_released(
            baseline,
            parse_resources(send_resources(process)),
            "after unexpected client exit",
        )

        log = serial_bytes().decode(errors="replace")
        if "KERNEL PANIC" in log or "Double fault" in log:
            raise RuntimeError("kernel fault observed")
    except (RuntimeError, TimeoutError) as error:
        print(f"single-window smoke failed: {error}")
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
    print(
        "single-window smoke OK: supervised services + lifecycle + pixels + "
        f"owner-exit cleanup; happy={happy_counts}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
