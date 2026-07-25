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
        expected = f"records=0x{count:08X} online=0x{count:08X}"
        if expected not in text:
            failures.append(f"-smp {count}: missing {expected}")
        for logical in range(count):
            marker = f"cpu[0x{logical:08X}] logical=0x{logical:08X}"
            offset = text.find(marker)
            if offset < 0:
                failures.append(f"-smp {count}: missing logical CPU {logical}")
            elif "state=online" not in text[offset:offset + 180]:
                failures.append(f"-smp {count}: logical CPU {logical} not online")
        ap_count = count - 1
        expected_startup = (
            f"attempted=0x{ap_count:08X} online=0x{ap_count:08X} "
            f"failed=0x00000000 "
            f"ping_sent=0x{ap_count * 3:08X} "
            f"ping_ack=0x{ap_count * 3:08X}"
        )
        if expected_startup not in text:
            failures.append(f"-smp {count}: missing startup counters")
        for logical in range(1, count):
            local_marker = f"cpu[0x{logical:08X}] valid=0x00000001"
            offset = text.find(local_marker)
            record = text[offset:offset + 420] if offset >= 0 else ""
            if "online=0x00000001" not in record:
                failures.append(f"-smp {count}: AP {logical} local state not online")
            if f"ping=0x{3:016X}" not in record:
                failures.append(
                    f"-smp {count}: AP {logical} did not acknowledge 3 pings"
                )
        if "CPU topology ready: 0x00000001" not in text:
            failures.append(f"-smp {count}: topology was not accepted")
        if "OS64 KERNEL PANIC" in text:
            failures.append(f"-smp {count}: kernel panic")
    if failures:
        print("SMP topology smoke failures:")
        print("\n".join(failures))
        return 1
    print("SMP topology smoke OK (1/2/4 vCPUs, exact online + 3 pings/AP)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
