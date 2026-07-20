#!/usr/bin/env python3
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SERIAL = ROOT / "logs/serial_thread_readiness.log"
QEMU_LOG = ROOT / "logs/qemu_thread_readiness.log"
RESULT = re.compile(
    r"\[READY\] work=(\d+),(\d+),(\d+) preempt=(\d+),(\d+),(\d+) failures=(\d+)"
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
        if needle in serial_bytes():
            return
        time.sleep(0.1)
    raise TimeoutError(f"timed out waiting for {pattern}")


def send(process: subprocess.Popen, command: str) -> None:
    assert process.stdin is not None
    process.stdin.write((command + "\n").encode("ascii"))
    process.stdin.flush()
    time.sleep(0.04)


def send_command(process: subprocess.Popen, command: str,
                 timeout: float = 40) -> str:
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
            return output[:prompt + 5].decode(errors="replace")
        time.sleep(0.1)
    raise TimeoutError(f"timed out waiting for command {command}")


def main() -> int:
    os.chdir(ROOT)
    SERIAL.unlink(missing_ok=True)
    QEMU_LOG.unlink(missing_ok=True)
    run_id = os.getpid()
    esp = Path(f"/tmp/os64_ready_{run_id}_esp.img")
    variables = Path(f"/tmp/os64_ready_{run_id}_vars.fd")
    shutil.copyfile(ROOT / "bin/uefi_esp.img", esp)
    shutil.copyfile("/usr/share/OVMF/OVMF_VARS_4M.fd", variables)
    qemu = [
        "qemu-system-x86_64", "-machine", "q35", "-m", "512M", "-cpu", "max",
        "-drive", "if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd",
        "-drive", f"if=pflash,format=raw,file={variables}",
        "-drive", f"if=none,id=esp,format=raw,file={esp}",
        "-device", "virtio-blk-pci,drive=esp,bootindex=1", "-boot", "menu=off",
        "-display", "none", "-serial", f"file:{SERIAL}", "-monitor", "stdio",
        "-no-reboot", "-d", "guest_errors,cpu_reset,int", "-D", str(QEMU_LOG),
    ]
    process = subprocess.Popen(qemu, stdin=subprocess.PIPE,
                               stdout=subprocess.DEVNULL,
                               stderr=subprocess.STDOUT)
    try:
        wait_for("OS64>", 20)
        output = send_command(process, "run uthread_ready_c.elf")
        match = RESULT.search(output)
        if "[READY] PASS" not in output or match is None or int(match.group(7)) != 0:
            raise RuntimeError("thread readiness smoke failed")
        work = tuple(int(match.group(i)) for i in range(1, 4))
        if min(work) == 0 or max(work) > min(work) * 6:
            raise RuntimeError(f"thread fairness bound failed: {work}")
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
    print(f"thread readiness smoke OK work={work}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, TimeoutError) as error:
        print(error, file=sys.stderr)
        raise SystemExit(1)
