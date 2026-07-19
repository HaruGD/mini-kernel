#!/usr/bin/env python3
import os
import shutil
import subprocess
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SERIAL = ROOT / "logs/serial_drive_free_scheduler.log"
QEMU_LOG = ROOT / "logs/qemu_drive_free_scheduler.log"


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
        time.sleep(0.05)
    raise TimeoutError(f"timed out waiting for {marker!r}")


def monitor_line(process: subprocess.Popen, line: str) -> None:
    assert process.stdin is not None
    process.stdin.write((line + "\n").encode("ascii"))
    process.stdin.flush()
    time.sleep(0.03)


def shell_command(process: subprocess.Popen, text: str,
                  timeout: float = 30) -> str:
    key_map = {" ": "spc", ".": "dot", "_": "shift-minus", "-": "minus"}
    start = len(serial_bytes())
    for char in text:
        monitor_line(process, f"sendkey {key_map.get(char, char)}")
    monitor_line(process, "sendkey ret")
    end = wait_for("OS64>", timeout, start)
    output = serial_bytes()[start:end].decode(errors="replace")
    return output


def command(process: subprocess.Popen, text: str, timeout: float = 30) -> str:
    output = shell_command(process, text, timeout)
    if output.count("Running user program: usvcctl_c.elf") != 1 or \
            output.count("Returned from user program") != 1:
        raise RuntimeError(f"non-terminal foreground service command: {output}")
    return output


def main() -> int:
    SERIAL.parent.mkdir(parents=True, exist_ok=True)
    SERIAL.unlink(missing_ok=True)
    QEMU_LOG.unlink(missing_ok=True)
    run_id = os.getpid()
    esp = Path(f"/tmp/os64_drive_free_{run_id}_esp.img")
    vars_image = Path(f"/tmp/os64_drive_free_{run_id}_vars.fd")
    shutil.copyfile(ROOT / "bin/uefi_esp.img", esp)
    shutil.copyfile("/usr/share/OVMF/OVMF_VARS_4M.fd", vars_image)
    qemu = [
        "qemu-system-x86_64", "-machine", "q35", "-m", "512M", "-cpu", "max",
        "-drive", "if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd",
        "-drive", f"if=pflash,format=raw,file={vars_image}",
        "-drive", f"if=none,id=esp,format=raw,file={esp}",
        "-device", "virtio-blk-pci,drive=esp,bootindex=1", "-boot", "menu=off",
        "-vga", "none", "-device", "VGA,xres=800,yres=600", "-display", "none",
        "-serial", f"file:{SERIAL}", "-monitor", "stdio", "-no-reboot",
        "-d", "guest_errors,cpu_reset,int", "-D", str(QEMU_LOG),
    ]
    process = subprocess.Popen(qemu, cwd=ROOT, stdin=subprocess.PIPE,
                               stdout=subprocess.DEVNULL,
                               stderr=subprocess.STDOUT)
    try:
        wait_for("OS64>", 25)
        if "start window OK" not in command(process, "service start window", 40):
            raise RuntimeError("window stack did not start")
        for _ in range(3):
            if "ping service OK" not in command(process, "service", 30):
                raise RuntimeError("bare service ping failed")
            if "status window OK" not in command(
                    process, "service status window", 30):
                raise RuntimeError("window status failed")
        log = serial_bytes().decode(errors="replace")
        if "udrive_c.elf" in log or "wait failed" in log:
            raise RuntimeError("drive helper or foreground wait failure observed")
        print("drive-free scheduler/service-control QEMU test OK")
        return 0
    finally:
        if process.poll() is None:
            try:
                monitor_line(process, "quit")
                process.wait(timeout=3)
            except (BrokenPipeError, subprocess.TimeoutExpired):
                process.kill()
                process.wait(timeout=3)
        esp.unlink(missing_ok=True)
        vars_image.unlink(missing_ok=True)


if __name__ == "__main__":
    raise SystemExit(main())
