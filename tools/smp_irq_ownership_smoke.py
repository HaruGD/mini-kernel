#!/usr/bin/env python3
import re
import shutil
import subprocess
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SERIAL = ROOT / "logs/serial_smp_irq_ownership.log"
IRQ = re.compile(
    r"irq=0x([0-9A-Fa-f]{8}).*?owner_cpu=0x([0-9A-Fa-f]{8})"
    r".*?accepted=0x([0-9A-Fa-f]{16})"
)


def serial_bytes() -> bytes:
    try:
        return SERIAL.read_bytes()
    except FileNotFoundError:
        return b""


def wait_for(marker: bytes, timeout: float, offset: int = 0) -> int:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        index = serial_bytes().find(marker, offset)
        if index >= 0:
            return index + len(marker)
        time.sleep(0.1)
    raise TimeoutError(f"timed out waiting for {marker!r}")


def monitor(process: subprocess.Popen, command: str) -> None:
    assert process.stdin is not None
    process.stdin.write((command + "\n").encode("ascii"))
    process.stdin.flush()
    time.sleep(0.02)


def shell_command(process: subprocess.Popen, command: str) -> str:
    key_map = {" ": "spc"}
    start = len(serial_bytes())
    for character in command:
        monitor(process, f"sendkey {key_map.get(character, character)}")
    monitor(process, "sendkey ret")
    end = wait_for(b"OS64>", 30, start)
    return serial_bytes()[start:end].decode(errors="replace")


def main() -> int:
    SERIAL.parent.mkdir(parents=True, exist_ok=True)
    SERIAL.unlink(missing_ok=True)
    run_id = int(time.time() * 1000)
    image = Path(f"/tmp/os64_irq_owner_{run_id}.img")
    variables = Path(f"/tmp/os64_irq_owner_{run_id}.fd")
    shutil.copyfile(ROOT / "bin/uefi_diag_esp.img", image)
    shutil.copyfile("/usr/share/OVMF/OVMF_VARS_4M.fd", variables)
    qemu = subprocess.Popen([
        "qemu-system-x86_64", "-machine", "q35", "-m", "512M",
        "-cpu", "max", "-smp", "4",
        "-drive", "if=pflash,format=raw,readonly=on,"
                  "file=/usr/share/OVMF/OVMF_CODE_4M.fd",
        "-drive", f"if=pflash,format=raw,file={variables}",
        "-drive", f"if=none,id=esp,format=raw,file={image}",
        "-device", "virtio-blk-pci,drive=esp,bootindex=1",
        "-boot", "menu=off", "-display", "none",
        "-serial", f"file:{SERIAL}", "-monitor", "stdio", "-no-reboot",
    ], stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
       stderr=subprocess.STDOUT)
    try:
        wait_for(b"OS64>", 30)
        output = shell_command(qemu, "intctl")
    finally:
        if qemu.poll() is None:
            qemu.terminate()
            try:
                qemu.wait(timeout=3)
            except subprocess.TimeoutExpired:
                qemu.kill()
                qemu.wait(timeout=3)
        if qemu.stdin is not None:
            qemu.stdin.close()
        image.unlink(missing_ok=True)
        variables.unlink(missing_ok=True)

    records = {
        int(match.group(1), 16): (
            int(match.group(2), 16),
            int(match.group(3), 16),
        )
        for match in IRQ.finditer(output)
    }
    failures = []
    for irq in (0, 1):
        owner, accepted = records.get(irq, (0xFFFFFFFF, 0))
        if owner != 0:
            failures.append(f"IRQ {irq} owner is {owner}, expected BSP CPU 0")
        if accepted == 0:
            failures.append(f"IRQ {irq} recorded no accepted delivery")
    if "owner_violations=0x0000000000000000" not in output:
        failures.append("external IRQ ownership violation recorded")
    if failures:
        print("SMP interrupt ownership failures:")
        for failure in failures:
            print(f"- {failure}")
        return 1
    print(
        "SMP interrupt ownership OK "
        f"(timer={records[0][1]}, keyboard={records[1][1]}, owner=CPU0)"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, TimeoutError) as error:
        print(error)
        raise SystemExit(1)
