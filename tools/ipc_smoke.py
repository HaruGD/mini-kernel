#!/usr/bin/env python3
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
SERIAL = ROOT / "logs" / "serial_ipc_smoke.log"
QEMU_LOG = ROOT / "logs" / "qemu_ipc_smoke.log"
MONITOR_OUTPUT = Path("/tmp/os64_ipc_smoke_monitor.txt")


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


def send_command(process: subprocess.Popen, command: str) -> None:
    key_map = {
        " ": "spc",
        ".": "dot",
        "_": "shift-minus",
        "-": "minus",
        "/": "slash",
    }
    for ch in command:
        send_monitor_line(process, f"sendkey {key_map.get(ch, ch)}")
    send_monitor_line(process, "sendkey ret")


def run() -> int:
    os.chdir(ROOT)
    (ROOT / "logs").mkdir(exist_ok=True)
    SERIAL.unlink(missing_ok=True)
    QEMU_LOG.unlink(missing_ok=True)
    MONITOR_OUTPUT.unlink(missing_ok=True)

    run_id = os.getpid()
    esp = Path(f"/tmp/os64_ipc_smoke_{run_id}_esp.img")
    vars_image = Path(f"/tmp/os64_ipc_smoke_{run_id}_vars.fd")
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
        send_command(process, "run uping_c.elf")
        wait_for_serial("[upong] ready pid=", 20)
        wait_for_serial("[uping] sent request target=", 20)
        wait_for_serial("[upong] reply sent", 20)
        wait_for_serial("[uping] reply from pid=", 20)
        wait_for_serial("[uping] IPC roundtrip OK", 20)
        wait_for_serial("state=returned term=exit code=0x00000000", 20)
        send_command(process, "ipc")
        wait_for_serial("=== IPC ===", 10)
        wait_for_serial("mailbox_capacity=", 10)
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
        "=== OS64 IPC ping sample ===",
        "[upong] ready pid=",
        "[uping] sent request target=",
        "[upong] reply sent",
        "[uping] reply from pid=",
        "[uping] IPC roundtrip OK",
        "=== IPC ===",
        "mailbox_capacity=",
    ]
    missing = [item for item in required if item not in serial_text]
    if missing:
        print("IPC smoke missing:", file=sys.stderr)
        for item in missing:
            print(item, file=sys.stderr)
        return 1

    print("IPC smoke OK")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(run())
    except TimeoutError as error:
        print(error, file=sys.stderr)
        raise SystemExit(1)
