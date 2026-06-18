#!/usr/bin/env python3
import argparse
import shutil
import subprocess
import time
from pathlib import Path


KEY_MAP = {
    " ": "spc",
    "/": "slash",
    ".": "dot",
    "-": "minus",
    "_": "shift-minus",
}
for _ch in "ABCDEFGHIJKLMNOPQRSTUVWXYZ":
    KEY_MAP[_ch] = "shift-" + _ch.lower()


DEFAULT_COMMANDS = [
    ("ushellc", 4.0),
    ("pwd", 1.0),
    ("version", 1.5),
    ("uptime", 1.0),
    ("mounts", 3.0),
    ("ls /", 2.0),
    ("sleep 1", 1.0),
    ("save /mem/uefi.txt hello", 1.0),
    ("cat /mem/uefi.txt", 1.0),
    ("exit", 2.0),
]


def send_monitor_line(proc: subprocess.Popen, line: str) -> None:
    assert proc.stdin is not None
    proc.stdin.write((line + "\n").encode("ascii"))
    proc.stdin.flush()


def send_shell_command(proc: subprocess.Popen, command: str) -> None:
    for ch in command:
        key = KEY_MAP.get(ch, ch)
        send_monitor_line(proc, f"sendkey {key}")
        time.sleep(0.05)
    send_monitor_line(proc, "sendkey ret")


def run_qemu(serial_log: Path, commands: list[tuple[str, float]]) -> None:
    serial_log.unlink(missing_ok=True)
    esp_image = Path("bin/uefi_esp.userland.img")
    esp_image.unlink(missing_ok=True)
    shutil.copyfile("bin/uefi_esp.img", esp_image)

    qemu = [
        "qemu-system-x86_64",
        "-drive", "if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd",
        "-drive", "if=pflash,format=raw,file=./bin/OVMF_VARS_4M.fd",
        "-drive", f"if=none,id=uefi_esp,format=raw,file={esp_image}",
        "-device", "virtio-blk-pci,drive=uefi_esp,bootindex=1",
        "-serial", f"file:{serial_log}",
        "-monitor", "stdio",
        "-display", "none",
    ]

    proc = subprocess.Popen(qemu, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    try:
        time.sleep(10.0)
        for command, delay in commands:
            send_shell_command(proc, command)
            time.sleep(delay)
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


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--serial-log", default="logs/serial_uefi_userland.log")
    args = parser.parse_args()

    serial_log = Path(args.serial_log)
    serial_log.parent.mkdir(parents=True, exist_ok=True)
    run_qemu(serial_log, DEFAULT_COMMANDS)

    log_text = serial_log.read_text(errors="replace") if serial_log.exists() else ""
    checks = [
        "Boot path: UEFI",
        "Ramdisk:",
        "Root source: ramdisk",
        "Running user program: ushell_c.elf",
        "=== ushell_c.elf ===",
        "csh> pwd",
        "csh> version",
        "PIT hz:",
        "Approx ms:",
        "Showing VFS mounts from C userland.",
        "sleep ticks=1 approx_ms=10",
        "mount[0x00000000] / fs=fat32 backend=0x00000001",
        "hello.txt",
        "kernel64.bin",
        "Saved: /mem/uefi.txt",
        "hello",
        "Leaving C user shell...",
        "Returned from user program [pid=0x00000001] state=returned",
        "OS64>",
    ]
    missing = [check for check in checks if check not in log_text]
    if missing:
        print("UEFI userland smoke missing:")
        for item in missing:
            print(item)
        return 1

    print("UEFI userland smoke OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
