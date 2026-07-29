#!/usr/bin/env python3
import os
import re
import shutil
import subprocess
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SERIAL = ROOT / "logs/serial_tlb_shootdown.log"
QEMU_LOG = ROOT / "logs/qemu_tlb_shootdown.log"
RESULT = re.compile(r"\[TLBX\] cycles=(\d+) reads=(\d+) failures=(\d+)")
CPU_TLB = re.compile(
    r"cpu\[0x([0-9A-Fa-f]{8})\].*?"
    r"tlb_sent=0x([0-9A-Fa-f]{16}).*?"
    r"tlb_recv=0x([0-9A-Fa-f]{16}).*?"
    r"tlb_ack=0x([0-9A-Fa-f]{16}).*?"
    r"tlb_stale=0x([0-9A-Fa-f]{16}).*?"
    r"tlb_flush=0x([0-9A-Fa-f]{16})"
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


def shell_command(process: subprocess.Popen,
                  command: str,
                  timeout: float = 120) -> str:
    key_map = {" ": "spc", ".": "dot", "_": "shift-minus", "-": "minus"}
    start = len(serial_bytes())
    for character in command:
        monitor(process, f"sendkey {key_map.get(character, character)}")
    monitor(process, "sendkey ret")
    end = wait_for(b"OS64>", timeout, start)
    return serial_bytes()[start:end].decode(errors="replace")


def main() -> int:
    SERIAL.parent.mkdir(parents=True, exist_ok=True)
    SERIAL.unlink(missing_ok=True)
    QEMU_LOG.unlink(missing_ok=True)
    run_id = int(time.time() * 1000)
    image = Path(f"/tmp/os64_tlb_{run_id}.img")
    variables = Path(f"/tmp/os64_tlb_{run_id}.fd")
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
        "-d", "int,cpu_reset", "-D", str(QEMU_LOG),
    ], stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
       stderr=subprocess.STDOUT)
    try:
        wait_for(b"OS64>", 30)
        output = shell_command(qemu, "run utlb_c.elf", 150)
        cpu_output = shell_command(qemu, "cpus", 30)
        lock_output = shell_command(qemu, "locks", 30)
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

    failures: list[str] = []
    result = RESULT.search(output)
    if result is None or "[TLBX] PASS" not in output:
        failures.append("shared-address-space churn did not pass")
        cycles = reads = user_failures = 0
    else:
        cycles, reads, user_failures = (int(value) for value in result.groups())
        if cycles != 64 or reads == 0 or user_failures != 0:
            failures.append(
                f"unexpected result cycles={cycles} reads={reads} "
                f"failures={user_failures}"
            )

    records = {
        int(match.group(1), 16): tuple(
            int(match.group(index), 16) for index in range(2, 7)
        )
        for match in CPU_TLB.finditer(cpu_output)
    }
    if set(records) != {0, 1, 2, 3}:
        failures.append(f"missing TLB CPU diagnostics: {sorted(records)}")
    for cpu, (sent, received, acknowledged, stale, flushes) in records.items():
        if cpu == 0 and sent == 0:
            failures.append("initiator CPU sent no shootdowns")
        if cpu != 0 and (received == 0 or acknowledged != received):
            failures.append(
                f"CPU {cpu} invalid receive/ack {received}/{acknowledged}"
            )
        if flushes == 0:
            failures.append(f"CPU {cpu} recorded no local TLB flush")
        if stale > acknowledged + 256:
            failures.append(f"CPU {cpu} stale counter is unbounded: {stale}")

    combined = output + cpu_output + lock_output
    if ("OS64 KERNEL PANIC" in combined or
            "CPU EMERGENCY FAILURE" in combined or
            "tlb_wait_violations=0x0000000000000000" not in lock_output):
        failures.append("fatal path or TLB_WAIT lock violation observed")
    if failures:
        print("TLB shootdown smoke failures:")
        for failure in failures:
            print(f"- {failure}")
        return 1
    print(
        "TLB shootdown smoke OK "
        f"(cycles={cycles}, reads={reads}, AP acks="
        f"{records[1][2]}/{records[2][2]}/{records[3][2]})"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, TimeoutError) as error:
        print(error)
        raise SystemExit(1)
