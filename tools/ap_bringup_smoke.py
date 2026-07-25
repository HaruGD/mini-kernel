#!/usr/bin/env python3
import shutil
import subprocess
import time
from pathlib import Path


KEY_MAP = {" ": "spc"}


def monitor(process: subprocess.Popen, command: str) -> None:
    assert process.stdin is not None
    process.stdin.write((command + "\n").encode("ascii"))
    process.stdin.flush()


def shell_command(process: subprocess.Popen, command: str) -> None:
    for character in command:
        monitor(process, f"sendkey {KEY_MAP.get(character, character)}")
        time.sleep(0.02)
    monitor(process, "sendkey ret")


def main() -> int:
    log = Path("logs/serial_ap_bringup.log")
    monitor_log = Path("logs/qemu_monitor_ap_bringup.log")
    image = Path("bin/uefi_diag_ap_bringup.img")
    variables = Path("bin/OVMF_VARS_4M.ap_bringup.fd")
    log.parent.mkdir(parents=True, exist_ok=True)
    log.unlink(missing_ok=True)
    monitor_log.unlink(missing_ok=True)
    shutil.copyfile("bin/uefi_diag_esp.img", image)
    shutil.copyfile("/usr/share/OVMF/OVMF_VARS_4M.fd", variables)
    monitor_output = monitor_log.open("wb")
    process = subprocess.Popen([
        "qemu-system-x86_64", "-machine", "q35", "-m", "512M",
        "-cpu", "max", "-smp", "4",
        "-drive", "if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd",
        "-drive", f"if=pflash,format=raw,file={variables}",
        "-drive", f"if=none,id=esp,format=raw,file={image}",
        "-device", "virtio-blk-pci,drive=esp,bootindex=1",
        "-boot", "menu=off", "-display", "none",
        "-serial", f"file:{log}", "-monitor", "stdio", "-no-reboot",
    ], stdin=subprocess.PIPE, stdout=monitor_output, stderr=subprocess.STDOUT)
    try:
        deadline = time.monotonic() + 16
        while time.monotonic() < deadline:
            if log.exists() and "OS64> " in log.read_text(errors="replace"):
                break
            time.sleep(0.25)
        else:
            raise RuntimeError("AP bring-up shell prompt did not appear")

        shell_command(process, "cpunmi 1")
        time.sleep(1)
        shell_command(process, "cpus")
        time.sleep(4)
    finally:
        process.kill()
        process.wait(timeout=3)
        if process.stdin is not None:
            process.stdin.close()
        monitor_output.close()
        image.unlink(missing_ok=True)
        variables.unlink(missing_ok=True)

    text = log.read_text(errors="replace")
    failures: list[str] = []
    ap_marker = "cpu[0x00000001] valid=0x00000001"
    offset = text.rfind(ap_marker)
    if offset < 0:
        failures.append("AP 1 CPU-local diagnostic is missing")
    else:
        record = text[offset:offset + 440]
        for marker in (
            "online=0x00000001",
            "nmi=0x0000000000000001",
            "ping=0x0000000000000003",
        ):
            if marker not in record:
                failures.append(f"AP 1 diagnostic missing {marker}")
    if "records=0x00000004 online=0x00000004" not in text:
        failures.append("not all four requested CPUs remained online")
    if "AP NMI logical=0x00000001 result=0x00000001" not in text:
        failures.append("direct Local APIC NMI delivery did not acknowledge")
    if offset < 0 or "OS64> " not in text[offset:]:
        failures.append("BSP shell did not remain responsive after AP NMI")
    if "CPU EMERGENCY FAILURE" in text or "OS64 KERNEL PANIC" in text:
        failures.append("AP NMI entered a fatal path")

    if failures:
        print("AP bring-up smoke failures:")
        for failure in failures:
            print(f"- {failure}")
        return 1
    print("AP bring-up smoke OK (4 online, repeated ping, AP1 NMI, BSP shell)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
