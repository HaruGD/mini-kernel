#!/usr/bin/env python3
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
SERIAL = ROOT / "logs" / "serial_usdk_test.log"
QEMU_LOG = ROOT / "logs" / "qemu_usdk_test.log"
MONITOR_OUTPUT = Path("/tmp/os64_usdk_test_monitor.txt")


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
    raise TimeoutError(f"Timed out waiting for serial marker: {pattern}")


def send_monitor_line(process: subprocess.Popen, line: str, delay: float = 0.05) -> None:
    assert process.stdin is not None
    process.stdin.write((line + "\n").encode("ascii"))
    process.stdin.flush()
    time.sleep(delay)


def send_key_sequence(process: subprocess.Popen, keys: list[str]) -> None:
    for key in keys:
        send_monitor_line(process, f"sendkey {key}")


def run() -> int:
    os.chdir(ROOT)
    (ROOT / "logs").mkdir(exist_ok=True)
    SERIAL.unlink(missing_ok=True)
    QEMU_LOG.unlink(missing_ok=True)
    MONITOR_OUTPUT.unlink(missing_ok=True)

    run_id = os.getpid()
    esp = Path(f"/tmp/os64_usdk_test_{run_id}_esp.img")
    vars_image = Path(f"/tmp/os64_usdk_test_{run_id}_vars.fd")
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
        "-vga", "std",
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
        time.sleep(8)
        send_key_sequence(process, [
            "r", "u", "n", "spc",
            "u", "s", "d", "k", "shift-minus", "t", "e", "s", "t",
            "dot", "e", "l", "f", "ret",
        ])

        wait_for_serial("[INFO] waiting for injected input event", 20)
        send_monitor_line(process, "sendkey z")
        wait_for_serial("[INFO] waiting for injected key event", 10)
        send_monitor_line(process, "sendkey x")
        time.sleep(5)
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

    serial_text = SERIAL.read_text(errors="replace") if SERIAL.exists() else ""
    if "=== result: passed=" not in serial_text:
        print(f"SDK test did not complete. See {SERIAL}", file=sys.stderr)
        return 1
    if "failed=0 ===" not in serial_text:
        print(f"SDK test reported a failure. See {SERIAL}", file=sys.stderr)
        return 1

    for line in serial_text.splitlines():
        if "[PASS]" in line or "[FAIL]" in line or "=== result:" in line:
            print(line)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(run())
    except TimeoutError as error:
        print(error, file=sys.stderr)
        raise SystemExit(1)
