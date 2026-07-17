#!/usr/bin/env python3
import os
import re
import shutil
import subprocess
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
SERIAL = ROOT / "logs/serial_window_input.log"
QEMU_LOG = ROOT / "logs/qemu_window_input.log"
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
            return wait_for(marker, max(0.1, deadline - time.time()),
                            match.end())
        time.sleep(0.1)
    raise TimeoutError(f"timed out waiting for {name!r} to start")


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


def send_command_async(process: subprocess.Popen, command: str) -> int:
    key_map = {" ": "spc", ".": "dot", "_": "shift-minus", "-": "minus"}
    start = len(serial_bytes())
    for char in command:
        monitor_line(process, f"sendkey {key_map.get(char, char)}")
    monitor_line(process, "sendkey ret")
    return start


def service_command(process: subprocess.Popen, command: str, expected: str) -> str:
    start = len(serial_bytes())
    output = send_command(process, command)
    if expected not in output:
        end = drive_until(process, expected, start, 30)
        output = serial_bytes()[start:end].decode(errors="replace")
        monitor_line(process, "sendkey f12")
        wait_for("OS64>", 30, end)
    return output


def drive_until(process: subprocess.Popen,
                marker: str,
                offset: int,
                timeout: float = 45) -> int:
    deadline = time.time() + timeout
    prompt_offset = offset
    needle = marker.encode("ascii")
    while time.time() < deadline:
        data = serial_bytes()
        index = data.find(needle, offset)
        if index >= 0:
            return index + len(marker)
        prompt = data.find(b"OS64>", prompt_offset)
        if prompt >= 0:
            send_command_async(process, "run udrive_c.elf")
            prompt_offset = prompt + len("OS64>")
        time.sleep(0.05)
    raise TimeoutError(f"timed out driving guest to {marker!r}")


def resources(process: subprocess.Popen) -> tuple[int, ...]:
    output = send_command(process, "resources")
    matches = list(RESOURCE_RE.finditer(output))
    if not matches:
        raise RuntimeError("resource snapshot missing")
    return tuple(int(value, 16) for value in matches[-1].groups())


def require_stable(before: tuple[int, ...], after: tuple[int, ...]) -> None:
    stable = (0, 1, 2, 3, 4, 5, 6, 8, 9)
    if any(before[index] != after[index] for index in stable):
        raise RuntimeError(f"window input resource drift: {before} -> {after}")


def main() -> int:
    SERIAL.parent.mkdir(parents=True, exist_ok=True)
    SERIAL.unlink(missing_ok=True)
    QEMU_LOG.unlink(missing_ok=True)
    run_id = os.getpid()
    esp = Path(f"/tmp/os64_window_input_{run_id}_esp.img")
    vars_image = Path(f"/tmp/os64_window_input_{run_id}_vars.fd")
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
        service_command(process, "service start input", "start input OK")
        service_command(process, "service start window", "start window OK")
        baseline = resources(process)

        start = send_command_async(process, "run usdk_c.elf window-input")
        wait_for("restricted input clients launched", 20, start)
        ready_b = wait_for("[window-input-b] background ready", 20, start)
        monitor_line(process, "sendkey f12")
        wait_for("OS64>", 10, ready_b)
        focused_server = drive_until(process, "[windowd] focused id=", ready_b)
        ready_a = drive_until(process, "[window-input-a] focused ready",
                              focused_server)

        wait_for("Returned from user program", 20, ready_a)
        key_a_start = len(serial_bytes())
        monitor_line(process, "sendkey f1")
        first_drive = send_command_async(process, "run udrive_c.elf")
        got_a = wait_for("[window-input-a] key F1 received", 30, key_a_start)
        focused_b = wait_for("[window-input-b] fallback focused", 30, got_a)
        out_a = wait_for("[window-input-a] hidden focus-out", 30, got_a)
        done_a = wait_for("[window-input-a] lifecycle OK", 30, got_a)
        wait_program_return("udrive_c.elf", 20, first_drive)
        key_b_start = len(serial_bytes())
        monitor_line(process, "sendkey f2")
        second_drive = send_command_async(process, "run udrive_c.elf")
        got_b = wait_for("[window-input-b] key F2 received", 30, key_b_start)
        done_b = wait_for("[window-input-b] lifecycle OK", 30, got_b)
        wait_program_return("udrive_c.elf", 20, second_drive)
        done = max(done_a, done_b)
        segment = serial_bytes()[start:done].decode(errors="replace")
        forbidden = ("background key leak", "hidden key leak", "sequence failure")
        if any(marker in segment for marker in forbidden):
            raise RuntimeError(f"focus isolation failure: {segment}")
        wait_for("OS64>", 20, done)

        final = resources(process)
        require_stable(baseline, final)
        print("window input/focus QEMU test OK")
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
