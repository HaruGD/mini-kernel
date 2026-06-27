#!/usr/bin/env python3
import shutil
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path


KEY_MAP = {
    " ": "spc",
    "/": "slash",
    ".": "dot",
    "-": "minus",
    "_": "shift-minus",
}

OVMF_VARS_TEMPLATE = Path("/usr/share/OVMF/OVMF_VARS_4M.fd")


@dataclass(frozen=True)
class PpmImage:
    width: int
    height: int
    pixels: bytes

    def rgb_at(self, x: int, y: int) -> tuple[int, int, int]:
        offset = (y * self.width + x) * 3
        return self.pixels[offset], self.pixels[offset + 1], self.pixels[offset + 2]


@dataclass(frozen=True)
class Scenario:
    name: str
    expected_width: int
    expected_height: int
    video_args: list[str]


def read_ppm(path: Path) -> PpmImage:
    data = path.read_bytes()
    if not data.startswith(b"P6\n"):
        raise SystemExit(f"{path} is not a binary PPM")

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
    if len(pixels) < width * height * 3:
        raise SystemExit(f"{path} pixel data is truncated")
    return PpmImage(width, height, pixels[:width * height * 3])


def is_outer_color(rgb: tuple[int, int, int]) -> bool:
    r, g, b = rgb
    return r > 180 and 50 < g < 140 and b < 90


def is_inner_color(rgb: tuple[int, int, int]) -> bool:
    r, g, b = rgb
    return r < 90 and g > 120 and b > 180


def is_partial_color(rgb: tuple[int, int, int]) -> bool:
    r, g, b = rgb
    return r < 120 and g > 150 and b < 120


def is_white(rgb: tuple[int, int, int]) -> bool:
    r, g, b = rgb
    return r > 180 and g > 180 and b > 180


def count_matching(image: PpmImage, predicate) -> int:
    count = 0
    pixels = image.pixels
    for i in range(0, len(pixels), 3):
        if predicate((pixels[i], pixels[i + 1], pixels[i + 2])):
            count += 1
    return count


def send_monitor_line(proc: subprocess.Popen, line: str) -> None:
    assert proc.stdin is not None
    proc.stdin.write((line + "\n").encode("ascii"))
    proc.stdin.flush()


def send_shell_command(proc: subprocess.Popen, command: str) -> None:
    for ch in command:
        send_monitor_line(proc, f"sendkey {KEY_MAP.get(ch, ch)}")
        time.sleep(0.04)
    send_monitor_line(proc, "sendkey ret")


def partial_rect(width: int, height: int) -> tuple[int, int, int, int]:
    rect_width = min(width // 4, 72)
    rect_height = min(height // 5, 48)
    rect_width = max(rect_width, 16)
    rect_height = max(rect_height, 16)
    x = width - rect_width - 12 if width > rect_width + 12 else 0
    y = height - rect_height - 12 if height > rect_height + 12 else 0
    return x, y, rect_width, rect_height


def run_qemu(scenario: Scenario, serial_log: Path, full_screen: Path, partial_screen: Path) -> None:
    serial_log.unlink(missing_ok=True)
    full_screen.unlink(missing_ok=True)
    partial_screen.unlink(missing_ok=True)
    esp_image = Path(f"bin/uefi_esp.gop_present.{scenario.name}.img")
    vars_image = Path(f"bin/OVMF_VARS.gop_present.{scenario.name}.fd")
    esp_image.unlink(missing_ok=True)
    vars_image.unlink(missing_ok=True)
    shutil.copyfile("bin/uefi_esp.img", esp_image)
    shutil.copyfile(OVMF_VARS_TEMPLATE, vars_image)

    qemu = [
        "qemu-system-x86_64",
        "-drive", "if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd",
        "-drive", f"if=pflash,format=raw,file={vars_image}",
        "-drive", f"if=none,id=uefi_esp,format=raw,file={esp_image}",
        "-device", "virtio-blk-pci,drive=uefi_esp,bootindex=1",
        *scenario.video_args,
        "-serial", f"file:{serial_log}",
        "-monitor", "stdio",
        "-display", "none",
    ]

    proc = subprocess.Popen(qemu, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    try:
        time.sleep(10.0)
        send_shell_command(proc, "gop test")
        time.sleep(2.0)
        send_monitor_line(proc, f"screendump {full_screen}")
        time.sleep(0.5)
        send_shell_command(proc, "gop partial")
        time.sleep(2.0)
        send_monitor_line(proc, f"screendump {partial_screen}")
        time.sleep(0.5)
        send_monitor_line(proc, "quit")
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
        vars_image.unlink(missing_ok=True)


def validate_full_frame(scenario: Scenario, image: PpmImage) -> None:
    if image.width != scenario.expected_width or image.height != scenario.expected_height:
        raise AssertionError(
            f"{scenario.name}: expected {scenario.expected_width}x{scenario.expected_height}, "
            f"got {image.width}x{image.height}"
        )

    outer = count_matching(image, is_outer_color)
    inner = count_matching(image, is_inner_color)
    white = count_matching(image, is_white)
    if outer < 100 or inner < 100 or white < 20:
        raise AssertionError(
            f"{scenario.name}: bad full-frame pixels outer={outer} inner={inner} white={white}"
        )

    box = min(image.width, image.height, 96)
    if not is_outer_color(image.rgb_at(max(1, box // 8), max(1, box // 2))):
        raise AssertionError(f"{scenario.name}: outer test pattern is misplaced")
    if not is_inner_color(image.rgb_at(max(1, box // 3), max(1, box // 2))):
        raise AssertionError(f"{scenario.name}: inner test pattern is misplaced")


def validate_partial_frame(scenario: Scenario, image: PpmImage) -> None:
    if image.width != scenario.expected_width or image.height != scenario.expected_height:
        raise AssertionError(
            f"{scenario.name}: expected partial {scenario.expected_width}x{scenario.expected_height}, "
            f"got {image.width}x{image.height}"
        )

    x, y, width, height = partial_rect(image.width, image.height)
    sample_x = x + max(1, width // 3)
    sample_y = y + max(1, height // 2) + 1
    if not is_partial_color(image.rgb_at(sample_x, sample_y)):
        raise AssertionError(f"{scenario.name}: partial rect is missing at {sample_x},{sample_y}")
    if not is_white(image.rgb_at(x, y)):
        raise AssertionError(f"{scenario.name}: partial diagonal is missing at {x},{y}")

    box = min(image.width, image.height, 96)
    if not is_outer_color(image.rgb_at(max(1, box // 8), max(1, box // 2))):
        raise AssertionError(f"{scenario.name}: full-frame region was damaged by partial present")


def validate_serial(scenario: Scenario, serial_log: Path) -> None:
    log_text = serial_log.read_text(errors="replace") if serial_log.exists() else ""
    required = [
        "GOP back buffer: 0x00000001",
        "GOP test pattern drawn. present=back-buffer.",
        "GOP partial pattern drawn. present=back-buffer.",
    ]
    missing = [item for item in required if item not in log_text]
    if missing:
        raise AssertionError(f"{scenario.name}: serial log missing {missing}")


def run_scenario(scenario: Scenario) -> str:
    serial_log = Path(f"logs/serial_gop_present_{scenario.name}.log")
    full_screen = Path(f"logs/gop_present_{scenario.name}_full.ppm")
    partial_screen = Path(f"logs/gop_present_{scenario.name}_partial.ppm")
    serial_log.parent.mkdir(parents=True, exist_ok=True)

    run_qemu(scenario, serial_log, full_screen, partial_screen)
    validate_serial(scenario, serial_log)
    if not full_screen.exists() or not partial_screen.exists():
        raise AssertionError(f"{scenario.name}: missing screendump")

    full = read_ppm(full_screen)
    partial = read_ppm(partial_screen)
    validate_full_frame(scenario, full)
    validate_partial_frame(scenario, partial)
    return f"{scenario.name} {full.width}x{full.height}"


def main() -> int:
    scenarios = [
        Scenario("default", 1280, 800, ["-vga", "none", "-device", "virtio-vga,xres=1280,yres=800"]),
        Scenario("small", 800, 600, ["-vga", "none", "-device", "VGA,xres=800,yres=600"]),
    ]

    try:
        results = [run_scenario(scenario) for scenario in scenarios]
    except AssertionError as exc:
        print(f"GOP present smoke failed: {exc}")
        return 1

    print("GOP present smoke OK: " + ", ".join(results))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
