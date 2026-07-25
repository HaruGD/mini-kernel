#!/usr/bin/env python3
import shutil
import subprocess
import time
from pathlib import Path


def run(cpu_count: int) -> str:
    log = Path(f"logs/serial_smp_topology_{cpu_count}.log")
    image = Path(f"bin/uefi_diag_smp_{cpu_count}.img")
    variables = Path(f"bin/OVMF_VARS_4M.smp_{cpu_count}.fd")
    log.parent.mkdir(parents=True, exist_ok=True)
    log.unlink(missing_ok=True)
    shutil.copyfile("bin/uefi_diag_esp.img", image)
    shutil.copyfile("/usr/share/OVMF/OVMF_VARS_4M.fd", variables)
    command = [
        "qemu-system-x86_64", "-machine", "q35", "-m", "512M",
        "-cpu", "max", "-smp", str(cpu_count),
        "-drive", "if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd",
        "-drive", f"if=pflash,format=raw,file={variables}",
        "-drive", f"if=none,id=esp,format=raw,file={image}",
        "-device", "virtio-blk-pci,drive=esp,bootindex=1",
        "-boot", "menu=off", "-display", "none",
        "-serial", f"file:{log}", "-monitor", "none", "-no-reboot",
    ]
    process = subprocess.Popen(command)
    try:
        time.sleep(8)
    finally:
        process.kill()
        process.wait(timeout=3)
        image.unlink(missing_ok=True)
        variables.unlink(missing_ok=True)
    return log.read_text(errors="replace")


def main() -> int:
    failures: list[str] = []
    for count in (1, 2, 4):
        text = run(count)
        expected = f"records=0x{count:08X} online=0x00000001"
        if expected not in text:
            failures.append(f"-smp {count}: missing {expected}")
        for logical in range(count):
            marker = f"cpu[0x{logical:08X}] logical=0x{logical:08X}"
            if marker not in text:
                failures.append(f"-smp {count}: missing logical CPU {logical}")
        if "CPU topology ready: 0x00000001" not in text:
            failures.append(f"-smp {count}: topology was not accepted")
        if "OS64 KERNEL PANIC" in text:
            failures.append(f"-smp {count}: kernel panic")
    if failures:
        print("SMP topology smoke failures:")
        print("\n".join(failures))
        return 1
    print("SMP topology smoke OK (1/2/4 vCPUs, BSP-only online)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
