#!/usr/bin/env python3
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


def send_monitor_line(proc: subprocess.Popen, line: str) -> None:
    assert proc.stdin is not None
    proc.stdin.write((line + "\n").encode("ascii"))
    proc.stdin.flush()


def send_shell_command(proc: subprocess.Popen, command: str) -> None:
    for ch in command:
        send_monitor_line(proc, f"sendkey {KEY_MAP.get(ch, ch)}")
        time.sleep(0.04)
    send_monitor_line(proc, "sendkey ret")


def run_qemu(serial_log: Path) -> None:
    serial_log.unlink(missing_ok=True)
    esp_image = Path("bin/uefi_esp.gfxdemo.img")
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
        send_shell_command(proc, "run ugfxdemo_c.elf")
        time.sleep(4.0)
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
    serial_log = Path("logs/serial_graphics_demo.log")
    serial_log.parent.mkdir(parents=True, exist_ok=True)
    run_qemu(serial_log)

    log_text = serial_log.read_text(errors="replace") if serial_log.exists() else ""
    checks = [
        "Boot path: UEFI",
        "Running user program: ugfxdemo_c.elf",
        "ugfxdemo: drew",
        "Returned from user program",
        "state=returned",
    ]
    missing = [check for check in checks if check not in log_text]
    if missing:
        print("Graphics demo smoke missing:")
        for item in missing:
            print(item)
        return 1

    print("Graphics demo smoke OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
