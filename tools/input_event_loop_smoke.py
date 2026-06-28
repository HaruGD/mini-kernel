#!/usr/bin/env python3
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
SERIAL = ROOT / "logs" / "serial_input_event_loop.log"
QEMU_LOG = ROOT / "logs" / "qemu_input_event_loop.log"
MONITOR_OUTPUT = Path("/tmp/os64_input_event_loop_monitor.txt")


def wait_for_serial(pattern: str, timeout_seconds: float) -> None:
    deadline = time.time() + timeout_seconds
    needle = pattern.encode("ascii")
    while time.time() < deadline:
        try:
            if needle in SERIAL.read_bytes():
                return
        except FileNotFoundError:
            pass
        time.sleep(0.1)
    raise TimeoutError(f"timed out waiting for serial marker: {pattern}")


def send_monitor_line(process: subprocess.Popen, line: str, delay: float = 0.05) -> None:
    assert process.stdin is not None
    process.stdin.write((line + "\n").encode("ascii"))
    process.stdin.flush()
    time.sleep(delay)


def send_keys(process: subprocess.Popen, keys: list[str]) -> None:
    for key in keys:
        send_monitor_line(process, f"sendkey {key}")


def run() -> int:
    os.chdir(ROOT)
    (ROOT / "logs").mkdir(exist_ok=True)
    SERIAL.unlink(missing_ok=True)
    QEMU_LOG.unlink(missing_ok=True)
    MONITOR_OUTPUT.unlink(missing_ok=True)

    run_id = os.getpid()
    esp = Path(f"/tmp/os64_input_event_loop_{run_id}_esp.img")
    vars_image = Path(f"/tmp/os64_input_event_loop_{run_id}_vars.fd")
    shutil.copyfile(ROOT / "bin" / "uefi_esp.img", esp)
    shutil.copyfile("/usr/share/OVMF/OVMF_VARS_4M.fd", vars_image)

    qemu = [
        "qemu-system-x86_64",
        "-machine", "q35",
        "-m", "512M",
        "-cpu", "max",
        "-drive", "if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd",
        "-drive", f"if=pflash,format=raw,file={vars_image}",
        "-drive", f"if=none,id=esp,format=raw,file={esp}",
        "-device", "virtio-blk-pci,drive=esp,bootindex=1",
        "-boot", "menu=off",
        "-display", "none",
        "-serial", f"file:{SERIAL}",
        "-monitor", "stdio",
        "-no-reboot",
        "-d", "guest_errors,cpu_reset,int",
        "-D", str(QEMU_LOG),
    ]

    with MONITOR_OUTPUT.open("wb") as output:
        process = subprocess.Popen(
            qemu,
            stdin=subprocess.PIPE,
            stdout=output,
            stderr=subprocess.STDOUT,
        )

    try:
        wait_for_serial("OS64>", 20)
        send_keys(process, [
            "r", "u", "n", "spc",
            "u", "e", "v", "e", "n", "t", "shift-minus", "c",
            "dot", "e", "l", "f", "ret",
        ])
        wait_for_serial("[uevent] waiting for key event", 20)
        send_monitor_line(process, "sendkey q")
        wait_for_serial("[uevent] exit key received", 10)
        wait_for_serial("state=returned term=exit code=0x00000000", 10)
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
        vars_image.unlink(missing_ok=True)

    serial_text = SERIAL.read_text(errors="replace")
    required = [
        "=== OS64 event loop sample ===",
        "[uevent] waiting for key event",
        "event key type=1",
        "[uevent] exit key received",
        "state=returned term=exit code=0x00000000",
    ]
    missing = [item for item in required if item not in serial_text]
    if missing:
        print("Input event loop smoke missing:", file=sys.stderr)
        for item in missing:
            print(item, file=sys.stderr)
        return 1

    print("Input event loop smoke OK")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(run())
    except TimeoutError as error:
        print(error, file=sys.stderr)
        raise SystemExit(1)
