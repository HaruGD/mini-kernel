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


def session(name: str, action) -> str:
    log = Path(f"logs/serial_smp_emergency_{name}.log")
    monitor_log = Path(f"logs/qemu_monitor_smp_emergency_{name}.log")
    image = Path(f"bin/uefi_diag_emergency_{name}.img")
    variables = Path(f"bin/OVMF_VARS_4M.emergency_{name}.fd")
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
            raise RuntimeError(f"{name}: shell prompt did not appear")
        time.sleep(1)
        action(process)
        time.sleep(5)
    finally:
        process.kill()
        process.wait(timeout=3)
        if process.stdin is not None:
            process.stdin.close()
        monitor_output.close()
        image.unlink(missing_ok=True)
        variables.unlink(missing_ok=True)
    return log.read_text(errors="replace")


def main() -> int:
    nmi_text = session("nmi", lambda process: (
        monitor(process, "nmi"), time.sleep(1), shell_command(process, "cpus")
    ))
    df_text = session("df", lambda process: shell_command(process, "debugfault df"))
    failures: list[str] = []
    if "nmi=0x0000000000000001" not in nmi_text:
        failures.append("BSP NMI did not return through the per-CPU IST path")
    if "CPU EMERGENCY FAILURE" in nmi_text or "OS64 KERNEL PANIC" in nmi_text:
        failures.append("BSP NMI entered an emergency failure path")
    for marker in (
        "=== CPU EMERGENCY DOUBLE FAULT ===",
        "logical=0x00000000 apic=0x00000000",
        "System halted.",
    ):
        if marker not in df_text:
            failures.append(f"double-fault session missing: {marker}")
    if failures:
        print("SMP emergency entry smoke failures:")
        for failure in failures:
            print(f"- {failure}")
        return 1
    print("SMP emergency entry smoke OK (BSP NMI + diagnostic Double Fault)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
