#!/usr/bin/env python3
import re
import shutil
import subprocess
import time
from pathlib import Path


def serial_bytes(path: Path) -> bytes:
    try:
        return path.read_bytes()
    except FileNotFoundError:
        return b""


def wait_for(path: Path, marker: bytes, count: int, timeout: float) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if serial_bytes(path).count(marker) >= count:
            return
        time.sleep(0.05)
    raise TimeoutError(f"timed out waiting for {marker!r}")


def monitor(process: subprocess.Popen, command: str) -> None:
    assert process.stdin is not None
    process.stdin.write((command + "\n").encode("ascii"))
    process.stdin.flush()
    time.sleep(0.03)


def shell_command(process: subprocess.Popen, command: str) -> None:
    key_map = {" ": "spc", ".": "dot", "_": "shift-minus", "-": "minus"}
    for character in command:
        monitor(process, f"sendkey {key_map.get(character, character)}")
    monitor(process, "sendkey ret")


def run(cpu_count: int) -> tuple[str, int]:
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
        "-serial", f"file:{log}", "-monitor", "stdio", "-no-reboot",
    ]
    process = subprocess.Popen(command,
                               stdin=subprocess.PIPE,
                               stdout=subprocess.DEVNULL,
                               stderr=subprocess.STDOUT)
    try:
        wait_for(log, b"OS64>", 1, 20)
        shell_command(process, "uptime")
        wait_for(log, b"Tick: ", 1, 5)
        time.sleep(1)
        shell_command(process, "uptime")
        wait_for(log, b"Tick: ", 2, 5)
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=3)
        if process.stdin is not None:
            process.stdin.close()
        image.unlink(missing_ok=True)
        variables.unlink(missing_ok=True)
    text = log.read_text(errors="replace")
    ticks = [
        int(value, 16)
        for value in re.findall(r"Tick: 0x([0-9A-Fa-f]{8})", text)
    ]
    return text, ticks[-1] - ticks[-2] if len(ticks) >= 2 else -1


def main() -> int:
    failures: list[str] = []
    wall_time_deltas: dict[int, int] = {}
    for count in (1, 2, 4):
        text, wall_delta = run(count)
        wall_time_deltas[count] = wall_delta
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
        if "SMP execution ready: 0x00000001" not in text:
            failures.append(f"-smp {count}: scheduler execution was not released")
        execution = (
            f"release=0x00000001 scheduler_cpus=0x{count:08X} "
            "calibration_failed=0x00000000"
        )
        if execution not in text:
            failures.append(f"-smp {count}: local timer calibration summary invalid")
        for logical in range(count):
            local_marker = f"cpu[0x{logical:08X}] valid=0x00000001"
            offset = text.find(local_marker)
            record = text[offset:offset + 900] if offset >= 0 else ""
            if ("sched=0x00000001" not in record or
                    "timer_ok=0x00000001" not in record):
                failures.append(
                    f"-smp {count}: CPU {logical} scheduler timer not enabled"
                )
            match = re.search(r"error_bps=0x([0-9A-Fa-f]{8})", record)
            if match is None or int(match.group(1), 16) > 1500:
                failures.append(
                    f"-smp {count}: CPU {logical} calibration error out of bounds"
                )
        if "OS64 KERNEL PANIC" in text:
            failures.append(f"-smp {count}: kernel panic")
        if wall_delta <= 0 or wall_delta > 1000:
            failures.append(
                f"-smp {count}: PIT comparison interval invalid ({wall_delta})"
            )
    valid_deltas = [delta for delta in wall_time_deltas.values() if delta >= 0]
    if (valid_deltas and
            max(valid_deltas) - min(valid_deltas) >
            max(5, max(valid_deltas) // 10)):
        failures.append(
            "PIT wall-time delta changes with CPU count: "
            f"{wall_time_deltas}"
        )
    if failures:
        print("SMP topology smoke failures:")
        print("\n".join(failures))
        return 1
    print(
        "SMP topology smoke OK "
        f"(1/2/4 vCPUs, exact online + 3 pings/AP, PIT deltas={wall_time_deltas})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
