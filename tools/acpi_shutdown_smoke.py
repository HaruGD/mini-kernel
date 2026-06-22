#!/usr/bin/env python3
import shutil
import subprocess
import time
from pathlib import Path


KEY_MAP = {
    " ": "spc",
    "_": "shift-minus",
}


def monitor_line(process: subprocess.Popen, line: str) -> None:
    assert process.stdin is not None
    process.stdin.write((line + "\n").encode("ascii"))
    process.stdin.flush()


def shell_command(process: subprocess.Popen, command: str) -> None:
    for character in command:
        monitor_line(process, f"sendkey {KEY_MAP.get(character, character)}")
        time.sleep(0.025)
    monitor_line(process, "sendkey ret")


def main() -> int:
    serial_log = Path("logs/serial_acpi_shutdown_smoke.log")
    esp_image = Path("bin/uefi_shutdown.smoke.img")
    vars_image = Path("bin/OVMF_VARS_4M.shutdown.fd")
    serial_log.parent.mkdir(parents=True, exist_ok=True)
    serial_log.unlink(missing_ok=True)
    esp_image.unlink(missing_ok=True)
    vars_image.unlink(missing_ok=True)
    shutil.copyfile("bin/uefi_esp.img", esp_image)
    shutil.copyfile("/usr/share/OVMF/OVMF_VARS_4M.fd", vars_image)

    qemu = [
        "qemu-system-x86_64",
        "-machine", "q35",
        "-m", "512M",
        "-cpu", "max",
        "-drive", "if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd",
        "-drive", f"if=pflash,format=raw,file={vars_image}",
        "-drive", f"if=none,id=esp,format=raw,file={esp_image}",
        "-device", "virtio-blk-pci,drive=esp,bootindex=1",
        "-boot", "menu=off",
        "-display", "none",
        "-serial", f"file:{serial_log}",
        "-monitor", "stdio",
        "-no-reboot",
    ]
    process = subprocess.Popen(
        qemu,
        stdin=subprocess.PIPE,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.STDOUT,
    )
    exited = False
    try:
        time.sleep(8.0)
        shell_command(process, "shutdown")
        try:
            process.wait(timeout=8.0)
            exited = True
        except subprocess.TimeoutExpired:
            pass
    finally:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=3.0)
        if process.stdin is not None:
            process.stdin.close()
        esp_image.unlink(missing_ok=True)
        vars_image.unlink(missing_ok=True)

    text = serial_log.read_text(errors="replace") if serial_log.exists() else ""
    required = [
        "ACPI S5 power-off ready",
        "Powering off through ACPI S5...",
        "[INFO][power] entering ACPI S5",
    ]
    missing = [item for item in required if item not in text]
    if missing or not exited:
        print("ACPI shutdown smoke failures:")
        for item in missing:
            print(f"missing: {item}")
        if not exited:
            print("QEMU did not exit after the ACPI S5 request")
        return 1

    print("ACPI shutdown smoke OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
