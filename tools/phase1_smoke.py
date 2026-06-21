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
    serial_log = Path("logs/serial_phase1_smoke.log")
    serial_log.parent.mkdir(parents=True, exist_ok=True)
    serial_log.unlink(missing_ok=True)
    esp_image = Path("bin/uefi_diag_esp.smoke.img")
    vars_image = Path("bin/OVMF_VARS_4M.phase1.fd")
    shutil.copyfile("bin/uefi_diag_esp.img", esp_image)
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

    try:
        time.sleep(8.0)
        for command in ("acpi", "intctl", "klog stats", "run ufault_c.elf"):
            shell_command(process, command)
            time.sleep(1.0)
        shell_command(process, "panic test")
        time.sleep(2.0)
    finally:
        process.kill()
        process.wait(timeout=3.0)
        if process.stdin is not None:
            process.stdin.close()
        esp_image.unlink(missing_ok=True)
        vars_image.unlink(missing_ok=True)

    text = serial_log.read_text(errors="replace")
    required = [
        "Diagnostic mode: 0x00000001",
        "Driver autoloaded: 0x00000000",
        "ACPI ready: 0x00000001",
        "Interrupt controller: APIC ready=0x00000001",
        "=== ACPI ===",
        "mode=APIC",
        "=== KLOG ===",
        "=== PAGE FAULT ===",
        "state=failed term=page_fault",
        "OS64 KERNEL PANIC",
        "Reason: manual panic test",
        "Stack trace:",
        "System halted. Inspect serial output and klog.",
    ]
    missing = [item for item in required if item not in text]
    if missing:
        print("Phase 1 smoke missing:")
        for item in missing:
            print(item)
        return 1

    print("Phase 1 smoke OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
