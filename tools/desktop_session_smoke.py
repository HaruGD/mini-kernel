#!/usr/bin/env python3
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SERIAL = ROOT / "logs" / "serial_desktop_session.log"
QEMU_LOG = ROOT / "logs" / "qemu_desktop_session.log"


def wait_for(pattern: str, timeout: float, offset: int = 0) -> int:
    deadline = time.time() + timeout
    needle = pattern.encode("ascii")
    while time.time() < deadline:
        try:
            data = SERIAL.read_bytes()
            index = data.find(needle, offset)
            if index >= 0:
                return index + len(needle)
        except FileNotFoundError:
            pass
        time.sleep(0.1)
    raise TimeoutError(f"timed out waiting for {pattern}")


def monitor(process: subprocess.Popen, command: str) -> None:
    assert process.stdin is not None
    keys = {" ": "spc", ".": "dot", "_": "shift-minus", "-": "minus"}
    for char in command:
        process.stdin.write(f"sendkey {keys.get(char, char)}\n".encode())
    process.stdin.write(b"sendkey ret\n")
    process.stdin.flush()


def run() -> int:
    os.chdir(ROOT)
    (ROOT / "logs").mkdir(exist_ok=True)
    SERIAL.unlink(missing_ok=True)
    QEMU_LOG.unlink(missing_ok=True)
    run_id = os.getpid()
    esp = Path(f"/tmp/os64_phase5a_{run_id}_esp.img")
    variables = Path(f"/tmp/os64_phase5a_{run_id}_vars.fd")
    shutil.copyfile(ROOT / "bin/uefi_esp.img", esp)
    shutil.copyfile("/usr/share/OVMF/OVMF_VARS_4M.fd", variables)
    command = [
        "qemu-system-x86_64", "-machine", "q35", "-m", "512M",
        # This test owns the GUI lifecycle, not SMP scheduling.  Keep it on
        # one vCPU so failures identify session/layer regressions precisely;
        # SMP has a dedicated Phase 4.6/4.7 suite.
        "-cpu", "max", "-smp", "1",
        "-drive", "if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd",
        "-drive", f"if=pflash,format=raw,file={variables}",
        "-drive", f"if=none,id=esp,format=raw,file={esp}",
        "-device", "virtio-blk-pci,drive=esp,bootindex=1",
        "-boot", "menu=off", "-display", "none",
        "-serial", f"file:{SERIAL}", "-monitor", "stdio", "-no-reboot",
        "-d", "guest_errors,cpu_reset", "-D", str(QEMU_LOG),
    ]
    process = subprocess.Popen(command, stdin=subprocess.PIPE,
                               stdout=subprocess.DEVNULL,
                               stderr=subprocess.STDOUT)
    try:
        wait_for("OS64>", 25)
        monitor(process, "run usession_launch_c.elf")
        start = wait_for("[serviced] started display", 25)
        wait_for("[serviced] started input", 20, start)
        wait_for("[serviced] started window", 20, start)
        wait_for("[serviced] started session", 20, start)
        ready = wait_for("[sessiond] desktop ready generation=1", 25, start)
        wait_for("layer=0", 10, ready)
        denied = wait_for("[ulayer] privileged layers denied", 20, ready)
        crash = wait_for("[session-test] injected crash session", 20, denied)
        restart = wait_for("[serviced] auto-restart session attempt=1", 30, crash)
        ready2 = wait_for("[sessiond] desktop ready generation=1", 25, restart)
        wait_for("[windowd] created", 10, restart)
        passed = wait_for("[session-test] PASS", 30, ready2)
        wait_for("[windowd] GUI session released", 20, crash)
        # The launcher has already returned the kernel shell before GUI input
        # ownership is acquired, so its existing prompt becomes usable again
        # after release; a second prompt is not emitted.
        monitor(process, "locks")
        locks = wait_for("=== CONCURRENCY ===", 10, passed)
        wait_for("order_violations=0x0000000000000000 recursion_violations=0x0000000000000000 release_violations=0x0000000000000000", 10, locks)
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

    text = SERIAL.read_text(errors="replace")
    if "KERNEL PANIC" in text or "CPU EMERGENCY" in text:
        raise RuntimeError("fatal kernel path observed")
    print("desktop session QEMU smoke OK")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(run())
    except (TimeoutError, RuntimeError) as error:
        print(error, file=sys.stderr)
        raise SystemExit(1)
