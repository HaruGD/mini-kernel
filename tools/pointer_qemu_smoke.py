#!/usr/bin/env python3
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SERIAL = ROOT / "logs" / "serial_pointer.log"
QEMU_LOG = ROOT / "logs" / "qemu_pointer.log"


def wait_for(pattern: str, timeout: float, offset: int = 0) -> int:
    deadline = time.time() + timeout
    needle = pattern.encode()
    while time.time() < deadline:
        try:
            data = SERIAL.read_bytes()
            at = data.find(needle, offset)
            if at >= 0:
                return at + len(needle)
        except FileNotFoundError:
            pass
        time.sleep(0.1)
    raise TimeoutError(f"timed out waiting for {pattern}")


def hmp(process: subprocess.Popen, command: str) -> None:
    assert process.stdin is not None
    process.stdin.write((command + "\n").encode())
    process.stdin.flush()


def type_command(process: subprocess.Popen, command: str) -> None:
    keys = {" ": "spc", ".": "dot", "_": "shift-minus", "-": "minus"}
    for char in command:
        hmp(process, f"sendkey {keys.get(char, char)}")
    hmp(process, "sendkey ret")


def run() -> int:
    os.chdir(ROOT)
    (ROOT / "logs").mkdir(exist_ok=True)
    SERIAL.unlink(missing_ok=True)
    QEMU_LOG.unlink(missing_ok=True)
    run_id = os.getpid()
    esp = Path(f"/tmp/os64_pointer_{run_id}.img")
    variables = Path(f"/tmp/os64_pointer_{run_id}_vars.fd")
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
    process = subprocess.Popen(command, stdin=subprocess.PIPE,
                               stdout=subprocess.DEVNULL,
                               stderr=subprocess.STDOUT)
    try:
        wait_for("OS64>", 25)
        type_command(process, "service start window")
        start = wait_for("[serviced] started display", 25)
        wait_for("[serviced] started input", 20, start)
        wait_for("[serviced] started window", 20, start)
        wait_for("OS64>", 15, start)
        type_command(process, "run ugui_launch_c.elf")
        app = wait_for("[ugui] initial frame", 25, start)
        wait_for("[ugui] focused ready", 15, app)

        # The built-in cursor starts at 640,400. Move into the title bar,
        # capture it, drag the server-owned frame, then release.
        for _ in range(5):
            hmp(process, "mouse_move -60 -62")
        time.sleep(0.3)
        hmp(process, "mouse_button 1")
        for _ in range(4):
            hmp(process, "mouse_move 20 10")
        configured = wait_for("[ugui] configured", 20, app)
        hmp(process, "mouse_button 0")
        time.sleep(0.2)
        hmp(process, "mouse_move 0 1")
        time.sleep(0.2)

        # Tear the captured client down through its normal keyboard exit. This
        # also proves capture cancellation when the target disappears; close
        # control hit/action semantics are covered by the host state test.
        hmp(process, "sendkey esc")
        closed = wait_for("[ugui] lifecycle OK", 20, configured)
        wait_for("[windowd] GUI session released", 20, configured)
        type_command(process, "locks")
        locks = wait_for("=== CONCURRENCY ===", 10, closed)
        wait_for("order_violations=0x0000000000000000 recursion_violations=0x0000000000000000 release_violations=0x0000000000000000", 10, locks)
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
    print("PS/2 pointer drag/capture-teardown QEMU smoke OK")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(run())
    except (TimeoutError, RuntimeError) as error:
        print(error, file=sys.stderr)
        raise SystemExit(1)
