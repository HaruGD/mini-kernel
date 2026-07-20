#!/usr/bin/env python3
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SERIAL = ROOT / "logs/serial_gui_recovery.log"
QEMU_LOG = ROOT / "logs/qemu_gui_recovery.log"
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
        time.sleep(0.05)
    raise TimeoutError(f"timed out waiting for {marker!r}")


def monitor_line(process: subprocess.Popen, line: str) -> None:
    assert process.stdin is not None
    process.stdin.write((line + "\n").encode("ascii"))
    process.stdin.flush()
    time.sleep(0.03)


def send_command(process: subprocess.Popen, command: str,
                 timeout: float = 40) -> str:
    key_map = {" ": "spc", ".": "dot", "_": "shift-minus", "-": "minus"}
    start = len(serial_bytes())
    for character in command:
        monitor_line(process, f"sendkey {key_map.get(character, character)}")
    monitor_line(process, "sendkey ret")
    end = wait_for("OS64>", timeout, start)
    return serial_bytes()[start:end].decode(errors="replace")


def resources(process: subprocess.Popen) -> tuple[int, ...]:
    output = send_command(process, "resources")
    matches = list(RESOURCE_RE.finditer(output))
    if not matches:
        raise RuntimeError("resource snapshot missing")
    return tuple(int(value, 16) for value in matches[-1].groups())


def require_stable(before: tuple[int, ...], after: tuple[int, ...]) -> None:
    # PMM history is bounded but can retain recently returned process images.
    stable = (0, 1, 2, 3, 4, 5, 6, 8, 9)
    if any(before[index] != after[index] for index in stable):
        raise RuntimeError(f"GUI recovery resource drift: {before} -> {after}")


def main() -> int:
    SERIAL.parent.mkdir(parents=True, exist_ok=True)
    SERIAL.unlink(missing_ok=True)
    QEMU_LOG.unlink(missing_ok=True)
    run_id = os.getpid()
    esp = Path(f"/tmp/os64_gui_recovery_{run_id}_esp.img")
    vars_image = Path(f"/tmp/os64_gui_recovery_{run_id}_vars.fd")
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
            raise RuntimeError("window stack start failed")
        baseline = resources(process)
        output = send_command(process, "run ugui_recovery_c.elf", 80)
        required = (
            "[ugui-recovery] initial generation=",
            "[ugui-recovery] display crash restored console generation=",
            "[ugui-recovery] display recovered generation=",
            "[ugui-recovery] window crash restored console generation=",
            "[ugui-recovery] window recovered generation=",
            "[ugui-recovery] lifecycle OK",
            "Returned from user program",
        )
        missing = [marker for marker in required if marker not in output]
        if missing:
            raise RuntimeError(f"missing recovery markers: {missing}\n{output}")
        if "failed" in output or "KERNEL PANIC" in output:
            raise RuntimeError(f"GUI recovery failure marker:\n{output}")
        if "status display OK" not in send_command(process, "service status display"):
            raise RuntimeError("display status failed after recovery")
        if "status window OK" not in send_command(process, "service status window"):
            raise RuntimeError("window status failed after recovery")
        final = resources(process)
        require_stable(baseline, final)
        log = serial_bytes().decode(errors="replace")
        if "KERNEL PANIC" in log or "Double fault" in log:
            raise RuntimeError("kernel fault observed during GUI recovery")
        print("GUI service crash/recovery QEMU test OK")
        print(f"resource baseline={baseline}")
        print(f"resource final={final}")
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
    try:
        raise SystemExit(main())
    except (RuntimeError, TimeoutError) as error:
        print(error, file=sys.stderr)
        raise SystemExit(1)
