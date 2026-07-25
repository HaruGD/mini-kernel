#!/usr/bin/env python3
import re
import shutil
import subprocess
import time
import os
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SERIAL = ROOT / "logs/serial_smp_execution.log"
TRACE = ROOT / "logs/qemu_smp_execution_trace.log"
RESULT = re.compile(
    r"\[SMPX\] mask=(\d+) max_active=(\d+) preemptions=(\d+) failures=(\d+)"
)
CPU_LOCAL = re.compile(
    r"cpu\[0x([0-9A-Fa-f]{8})\] valid=.*?"
    r"sched=0x([0-9A-Fa-f]{8}).*?"
    r"timer_ok=0x([0-9A-Fa-f]{8}).*?"
    r"timer_hz=0x([0-9A-Fa-f]{16}).*?"
    r"error_bps=0x([0-9A-Fa-f]{8}).*?"
    r"local_ticks=0x([0-9A-Fa-f]{16}).*?"
    r"claims=0x([0-9A-Fa-f]{16}).*?"
    r"user_entries=0x([0-9A-Fa-f]{16}).*?"
    r"rs_recv=0x([0-9A-Fa-f]{16})"
)


def serial_bytes() -> bytes:
    try:
        return SERIAL.read_bytes()
    except FileNotFoundError:
        return b""


def wait_for(text: str, timeout: float) -> None:
    deadline = time.monotonic() + timeout
    marker = text.encode("ascii")
    while time.monotonic() < deadline:
        if marker in serial_bytes():
            return
        time.sleep(0.1)
    raise TimeoutError(f"timed out waiting for {text}")


def monitor(process: subprocess.Popen, command: str) -> None:
    assert process.stdin is not None
    process.stdin.write((command + "\n").encode("ascii"))
    process.stdin.flush()
    time.sleep(0.03)


def shell_command(process: subprocess.Popen, command: str,
                  timeout: float = 60) -> str:
    key_map = {" ": "spc", ".": "dot", "_": "shift-minus", "-": "minus"}
    start = len(serial_bytes())
    for character in command:
        monitor(process, f"sendkey {key_map.get(character, character)}")
    monitor(process, "sendkey ret")
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        output = serial_bytes()[start:]
        prompt = output.find(b"OS64>")
        if prompt >= 0:
            return output[:prompt + 5].decode(errors="replace")
        time.sleep(0.1)
    raise TimeoutError(f"timed out waiting for command {command}")


def shell_command_until(process: subprocess.Popen, command: str,
                        marker: str, timeout: float = 60) -> str:
    key_map = {" ": "spc", ".": "dot", "_": "shift-minus", "-": "minus"}
    start = len(serial_bytes())
    for character in command:
        monitor(process, f"sendkey {key_map.get(character, character)}")
    monitor(process, "sendkey ret")
    deadline = time.monotonic() + timeout
    encoded_marker = marker.encode("ascii")
    while time.monotonic() < deadline:
        output = serial_bytes()[start:]
        if encoded_marker in output:
            time.sleep(1)
            return serial_bytes()[start:].decode(errors="replace")
        time.sleep(0.1)
    raise TimeoutError(f"timed out waiting for {marker} from {command}")


def main() -> int:
    SERIAL.unlink(missing_ok=True)
    run_id = int(time.time() * 1000)
    image = Path(f"/tmp/os64_smp_execution_{run_id}.img")
    variables = Path(f"/tmp/os64_smp_execution_{run_id}.fd")
    shutil.copyfile(ROOT / "bin/uefi_diag_esp.img", image)
    shutil.copyfile("/usr/share/OVMF/OVMF_VARS_4M.fd", variables)
    qemu_args = [
        "qemu-system-x86_64", "-machine", "q35", "-m", "512M",
        "-cpu", "max", "-smp", "4",
        "-drive", "if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd",
        "-drive", f"if=pflash,format=raw,file={variables}",
        "-drive", f"if=none,id=esp,format=raw,file={image}",
        "-device", "virtio-blk-pci,drive=esp,bootindex=1",
        "-boot", "menu=off", "-display", "none",
        "-serial", f"file:{SERIAL}", "-monitor", "stdio", "-no-reboot",
    ]
    if os.environ.get("OS64_QEMU_TRACE") == "1":
        TRACE.unlink(missing_ok=True)
        qemu_args.extend(["-d", "int", "-D", str(TRACE)])
    process = subprocess.Popen(qemu_args, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
       stderr=subprocess.STDOUT)
    try:
        wait_for("OS64>", 25)
        output = shell_command_until(
            process, "run usmp_c.elf", "[SMPX] mask=", 75
        )
        cpu_output = shell_command(process, "cpus", 20)
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

    failures: list[str] = []
    result = RESULT.search(output)
    if result is None or "[SMPX] PASS" not in output:
        failures.append("user SMP workload did not report PASS")
    else:
        mask, maximum, preemptions, user_failures = (
            int(value) for value in result.groups()
        )
        if mask != 0x0E or maximum < 3 or preemptions < 3 or user_failures != 0:
            failures.append(
                f"unexpected workload result mask={mask:#x} max={maximum} "
                f"preemptions={preemptions} failures={user_failures}"
            )
    records = {
        int(match.group(1), 16): tuple(
            int(match.group(index), 16) for index in range(2, 10)
        )
        for match in CPU_LOCAL.finditer(cpu_output)
    }
    if set(records) != {0, 1, 2, 3}:
        failures.append(f"missing per-CPU diagnostics: {sorted(records)}")
    for logical_id, values in records.items():
        sched, timer_ok, timer_hz, error_bps, ticks, claims, entries, received = values
        if sched != 1 or timer_ok != 1 or timer_hz == 0 or error_bps > 1500:
            failures.append(
                f"CPU {logical_id} calibration invalid: "
                f"sched={sched} timer={timer_ok} hz={timer_hz} error={error_bps}"
            )
        if ticks == 0:
            failures.append(f"CPU {logical_id} local timer made no progress")
        if logical_id != 0 and (claims == 0 or entries == 0 or received == 0):
            failures.append(
                f"AP {logical_id} execution missing: claims={claims} "
                f"entries={entries} resched={received}"
            )
    combined = output + cpu_output
    if "OS64 KERNEL PANIC" in combined or "CPU EMERGENCY FAILURE" in combined:
        failures.append("kernel entered a fatal path")

    if failures:
        print("SMP execution smoke failures:")
        for failure in failures:
            print(f"- {failure}")
        return 1
    print(
        "SMP execution smoke OK "
        f"(mask={mask:#x}, max_active={maximum}, preemptions={preemptions})"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, TimeoutError) as error:
        print(error)
        raise SystemExit(1)
