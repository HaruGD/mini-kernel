#!/usr/bin/env python3
import re
import shutil
import subprocess
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
LOG_DIR = ROOT / "logs"
MODES = ("condition", "timer", "ipc", "input", "unbound")


def serial_bytes(path: Path) -> bytes:
    try:
        return path.read_bytes()
    except FileNotFoundError:
        return b""


def wait_for(path: Path, marker: bytes, timeout: float) -> bytes:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        output = serial_bytes(path)
        if marker in output:
            return output
        time.sleep(0.05)
    raise TimeoutError(f"timed out waiting for {marker!r} in {path.name}")


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


def run_mode(mode: str) -> str:
    log = LOG_DIR / f"serial_smp_remote_{mode}.log"
    run_id = int(time.time() * 1000000)
    image = Path(f"/tmp/os64_smp_remote_{mode}_{run_id}.img")
    variables = Path(f"/tmp/os64_smp_remote_{mode}_{run_id}.fd")
    log.unlink(missing_ok=True)
    shutil.copyfile(ROOT / "bin/uefi_diag_esp.img", image)
    shutil.copyfile("/usr/share/OVMF/OVMF_VARS_4M.fd", variables)
    command = [
        "qemu-system-x86_64", "-machine", "q35", "-m", "512M",
        "-cpu", "max", "-smp", "4",
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
        wait_for(log, b"OS64>", 25)
        if mode == "condition":
            shell_command(process, "cpuresched 1")
            burst_output = wait_for(log, b"Reschedule burst logical=", 10)
            burst_match = re.search(
                rb"Reschedule burst logical=0x00000001 "
                rb"requested=0x00000100 coalesced=0x([0-9A-Fa-f]{16})",
                burst_output,
            )
            if burst_match is None or int(burst_match.group(1), 16) == 0:
                raise RuntimeError("duplicate reschedule requests did not coalesce")
        shell_command(process, f"run usmp_wake_c.elf {mode}")
        if mode == "input":
            wait_for(log, b"[SMPW] input ready", 30)
            monitor(process, "sendkey z")
        output = wait_for(log, f"[SMPW] {mode}".encode("ascii"), 60)
        marker = {
            "condition": b"[SMPW] condition cpu=1 mask=2 PASS",
            "timer": b"[SMPW] timer cpu=2 mask=4 PASS",
            "ipc": b"[SMPW] ipc cpu=3 mask=8 PASS",
            "input": b"[SMPW] input cpu=1 mask=2 PASS",
        }.get(mode)
        if marker is not None:
            wait_for(log, marker, 20)
        else:
            wait_for(log, b"[SMPW] unbound", 20)
            output = serial_bytes(log)
            match = re.search(
                rb"\[SMPW\] unbound mask=(\d+) cpus=(\d+) "
                rb"max_active=(\d+) PASS",
                output,
            )
            if match is None or int(match.group(2)) < 2 or int(match.group(3)) < 2:
                raise RuntimeError("unbound distribution did not span two CPUs")
        text = serial_bytes(log).decode(errors="replace")
        if "OS64 KERNEL PANIC" in text or "CPU EMERGENCY FAILURE" in text:
            raise RuntimeError(f"{mode} entered a kernel fatal path")
        return text
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


def main() -> int:
    results = []
    for mode in MODES:
        text = run_mode(mode)
        line = next(
            line for line in text.splitlines()
            if line.startswith(f"[SMPW] {mode}") and "ready" not in line
        )
        results.append(line)
    print("SMP remote wake smoke OK")
    for line in results:
        print(f"- {line}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, TimeoutError, StopIteration) as error:
        print(error)
        raise SystemExit(1)
