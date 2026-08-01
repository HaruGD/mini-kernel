#!/usr/bin/env python3
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SERIAL = ROOT / "logs/serial_thread_smoke.log"
QEMU_LOG = ROOT / "logs/qemu_thread_smoke.log"
RESOURCE_RE = re.compile(
    r"processes=0x([0-9A-Fa-f]+) mappings=0x([0-9A-Fa-f]+) "
    r"handles=0x([0-9A-Fa-f]+) mailboxes=0x([0-9A-Fa-f]+) services=0x([0-9A-Fa-f]+).*?"
    r"shared=0x([0-9A-Fa-f]+) surfaces=0x([0-9A-Fa-f]+).*?"
    r"pmm_free=0x([0-9A-Fa-f]+).*?heap_used=0x([0-9A-Fa-f]+) heap_mapped=0x([0-9A-Fa-f]+)",
    re.DOTALL,
)
SCHED_RE = re.compile(
    r"Queue count: 0x([0-9A-Fa-f]+).*?Thread records: 0x([0-9A-Fa-f]+)",
    re.DOTALL,
)


def serial_bytes() -> bytes:
    try:
        return SERIAL.read_bytes()
    except FileNotFoundError:
        return b""


def wait_for(pattern: str, timeout: float) -> None:
    deadline = time.time() + timeout
    needle = pattern.encode("ascii")
    while time.time() < deadline:
        try:
            if needle in SERIAL.read_bytes():
                return
        except FileNotFoundError:
            pass
        time.sleep(0.1)
    raise TimeoutError(f"timed out waiting for {pattern}")


def send(process: subprocess.Popen, command: str) -> None:
    assert process.stdin is not None
    process.stdin.write((command + "\n").encode("ascii"))
    process.stdin.flush()
    time.sleep(0.04)


def send_command(process: subprocess.Popen, command: str,
                 timeout: float = 30) -> str:
    key_map = {" ": "spc", ".": "dot", "_": "shift-minus", "-": "minus"}
    start = len(serial_bytes())
    for character in command:
        send(process, f"sendkey {key_map.get(character, character)}")
    send(process, "sendkey ret")
    deadline = time.time() + timeout
    while time.time() < deadline:
        output = serial_bytes()[start:]
        prompt = output.find(b"OS64>")
        if prompt >= 0:
            return output[:prompt + len(b"OS64>")].decode(errors="replace")
        time.sleep(0.1)
    raise TimeoutError(f"timed out waiting for command {command}")


def resource_snapshot(process: subprocess.Popen) -> tuple[int, ...]:
    matches = list(RESOURCE_RE.finditer(send_command(process, "resources")))
    if not matches:
        raise RuntimeError("thread resource snapshot missing")
    return tuple(int(value, 16) for value in matches[-1].groups())


def scheduler_snapshot(process: subprocess.Popen) -> tuple[int, int]:
    matches = list(SCHED_RE.finditer(send_command(process, "sched")))
    if not matches:
        raise RuntimeError("thread scheduler snapshot missing")
    return tuple(int(value, 16) for value in matches[-1].groups())


def main() -> int:
    os.chdir(ROOT)
    SERIAL.unlink(missing_ok=True)
    QEMU_LOG.unlink(missing_ok=True)
    run_id = os.getpid()
    esp = Path(f"/tmp/os64_thread_{run_id}_esp.img")
    variables = Path(f"/tmp/os64_thread_{run_id}_vars.fd")
    shutil.copyfile(ROOT / "bin/uefi_esp.img", esp)
    shutil.copyfile("/usr/share/OVMF/OVMF_VARS_4M.fd", variables)
    qemu = [
        "qemu-system-x86_64", "-machine", "q35", "-m", "512M",
        "-cpu", "max",
        "-drive", "if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd",
        "-drive", f"if=pflash,format=raw,file={variables}",
        "-drive", f"if=none,id=esp,format=raw,file={esp}",
        "-device", "virtio-blk-pci,drive=esp,bootindex=1",
        "-boot", "menu=off", "-display", "none",
        "-serial", f"file:{SERIAL}", "-monitor", "stdio",
        "-no-reboot", "-d", "guest_errors,cpu_reset,int", "-D", str(QEMU_LOG),
    ]
    process = subprocess.Popen(qemu, stdin=subprocess.PIPE,
                               stdout=subprocess.DEVNULL,
                               stderr=subprocess.STDOUT)
    try:
        wait_for("OS64>", 20)
        # Exercise every reusable process/address-space record before taking
        # the baseline now that the process table has 16 slots.
        for warm_run in range(17):
            output = send_command(process, "run uthread_c.elf")
            if "[THREAD] PASS" not in output or "failures=0" not in output:
                raise RuntimeError(
                    f"thread lifecycle warm run {warm_run + 1} failed"
                )
        baseline = resource_snapshot(process)
        baseline_sched = scheduler_snapshot(process)

        measured = send_command(process, "run uthread_c.elf")
        if "[THREAD] PASS" not in measured or "failures=0" not in measured:
            raise RuntimeError("measured thread lifecycle run failed")
        final = resource_snapshot(process)
        final_sched = scheduler_snapshot(process)
        if baseline != final:
            raise RuntimeError(f"thread resource drift: {baseline} -> {final}")
        if baseline_sched != final_sched or final_sched[0] != 0 or not (
            1 <= final_sched[1] <= 16
        ):
            raise RuntimeError(
                f"thread scheduler drift: {baseline_sched} -> {final_sched}"
            )
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
        esp.unlink(missing_ok=True)
        variables.unlink(missing_ok=True)

    text = SERIAL.read_text(errors="replace") if SERIAL.exists() else ""
    if text.count("[THREAD] PASS") != 18 or \
            text.count("status=64,65,66 failures=0") != 18:
        print(f"thread smoke failed; see {SERIAL}", file=sys.stderr)
        return 1
    for line in text.splitlines():
        if "[THREAD]" in line:
            print(line)
    print(f"thread resource baseline={baseline}")
    print(f"thread resource final={final}")
    print(f"thread scheduler baseline={baseline_sched} final={final_sched}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, TimeoutError) as error:
        print(error, file=sys.stderr)
        raise SystemExit(1)
