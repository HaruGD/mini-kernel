#!/usr/bin/env python3
import os
import re
import shutil
import subprocess
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SERIAL = ROOT / "logs" / "serial_gui_terminal.log"
QEMU_LOG = ROOT / "logs" / "qemu_gui_terminal.log"
SCREEN = ROOT / "logs" / "gui_terminal.ppm"
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


def hmp(process: subprocess.Popen, command: str, delay: float = 0.03) -> None:
    assert process.stdin is not None
    process.stdin.write((command + "\n").encode())
    process.stdin.flush()
    time.sleep(delay)


def type_text(process: subprocess.Popen, text: str) -> None:
    keys = {" ": "spc", "-": "minus", ".": "dot", "_": "shift-minus"}
    for character in text:
        hmp(process, f"sendkey {keys.get(character, character)}")


def type_async(process: subprocess.Popen, command: str) -> int:
    start = len(serial_bytes())
    type_text(process, command)
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


def stable(before: tuple[int, ...], after: tuple[int, ...]) -> None:
    for index in (0, 1, 2, 3, 4, 5, 6, 8, 9):
        if before[index] != after[index]:
            raise RuntimeError(f"GUI terminal resource drift {before} -> {after}")


def foreground_pixels(path: Path) -> int:
    data = path.read_bytes()
    if not data.startswith(b"P6"):
        raise RuntimeError("invalid terminal screendump")
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
    count = 0
    for y in range(108, 530):
        for x in range(168, 870):
            at = offset + (y * width + x) * 3
            pixel = tuple(data[at:at + 3])
            if pixel != (18, 20, 25):
                count += 1
    return count


def run_cycle(process: subprocess.Popen, cycle: int, resize: bool) -> int:
    start = type_async(process, "run terminal_launch_c.elf")
    wait_for("[terminal-launch] GUI terminal started", 25, start)
    ready = wait_for("[terminal] ready child=", 30, start)
    type_text(process, "echo phase5d-ok")
    hmp(process, "sendkey ret")
    marker = wait_for("[terminal] transcript marker=phase5d-ok", 20, ready)
    if resize:
        hmp(process, "mouse_move 240 140", 0.1)
        hmp(process, "mouse_button 1", 0.1)
        hmp(process, "mouse_move 60 40", 0.1)
        resized = wait_for(
            "[terminal] resized columns=120 rows=58 surface=780x480", 20, marker)
        hmp(process, "mouse_button 0", 0.1)
        marker = resized
    hmp(process, f"screendump {SCREEN}", 0.1)
    pixels = foreground_pixels(SCREEN)
    if pixels < 100:
        raise RuntimeError(f"terminal text was not visibly rendered: {pixels}")
    type_text(process, "exit")
    hmp(process, "sendkey ret")
    exited = wait_for("[terminal] child exit status=0", 20, marker)
    cleaned = wait_for("[terminal] child reaped=1 forced=0 cleanup OK", 20, exited)
    wait_for("[windowd] GUI session released", 20, exited)
    print(f"cycle={cycle} foreground_pixels={pixels}")
    return pixels


def run_hangup_cycle(process: subprocess.Popen) -> None:
    start = type_async(process, "run terminal_launch_c.elf")
    wait_for("[terminal-launch] GUI terminal started", 25, start)
    ready = wait_for("[terminal] ready child=", 30, start)
    hmp(process, "sendkey ctrl-shift-q")
    exited = wait_for("[terminal] child exit status=0", 20, ready)
    wait_for("[terminal] child reaped=1 forced=0 cleanup OK", 20, exited)
    wait_for("[windowd] GUI session released", 20, ready)
    print("hangup_cycle=clean")


def run_fault_cycle(process: subprocess.Popen) -> None:
    start = type_async(process, "run terminal_launch_c.elf")
    wait_for("[terminal-launch] GUI terminal started", 25, start)
    ready = wait_for("[terminal] ready child=", 30, start)
    hmp(process, "sendkey ctrl-shift-k")
    lost = wait_for("[terminal] child lost status=-18", 20, ready)
    wait_for("[terminal] child reaped=1 forced=0 cleanup OK", 20, lost)
    wait_for("[windowd] GUI session released", 20, ready)
    print("fault_cycle=clean")


def main() -> int:
    for path in (SERIAL, QEMU_LOG, SCREEN):
        path.parent.mkdir(exist_ok=True)
        path.unlink(missing_ok=True)
    run_id = os.getpid()
    esp = Path(f"/tmp/os64_gui_terminal_{run_id}.img")
    variables = Path(f"/tmp/os64_gui_terminal_{run_id}_vars.fd")
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
        pixels_first = run_cycle(process, 1, True)
        pixels_second = run_cycle(process, 2, False)
        run_hangup_cycle(process)
        run_fault_cycle(process)
        final = resources(process)
        stable(baseline, final)
        locks_start = type_async(process, "locks")
        locks = wait_for("=== CONCURRENCY ===", 10, locks_start)
        wait_for("order_violations=0x0000000000000000 recursion_violations=0x0000000000000000 release_violations=0x0000000000000000",
                 10, locks)
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=3)
        if process.stdin is not None:
            process.stdin.close()
        esp.unlink(missing_ok=True)
        variables.unlink(missing_ok=True)
    text = SERIAL.read_text(errors="replace")
    if "KERNEL PANIC" in text or "CPU EMERGENCY" in text:
        raise RuntimeError("fatal kernel path observed")
    print(f"GUI terminal QEMU lifecycle OK pixels={pixels_first},{pixels_second}")
    print(f"resource baseline={baseline}")
    print(f"resource final={final}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
