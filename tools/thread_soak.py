#!/usr/bin/env python3
import argparse
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SERIAL = ROOT / "logs/serial_thread_soak.log"
QEMU_LOG = ROOT / "logs/qemu_thread_soak.log"
RESOURCE_RE = re.compile(
    r"processes=0x([0-9A-Fa-f]+) mappings=0x([0-9A-Fa-f]+) "
    r"handles=0x([0-9A-Fa-f]+) mailboxes=0x([0-9A-Fa-f]+) services=0x([0-9A-Fa-f]+).*?"
    r"shared=0x([0-9A-Fa-f]+) surfaces=0x([0-9A-Fa-f]+).*?"
    r"pmm_free=0x([0-9A-Fa-f]+).*?heap_used=0x([0-9A-Fa-f]+) heap_mapped=0x([0-9A-Fa-f]+)",
    re.DOTALL,
)


def serial_bytes() -> bytes:
    try:
        return SERIAL.read_bytes()
    except FileNotFoundError:
        return b""


def wait_for(marker: str, timeout: float, offset: int = 0) -> int:
    deadline = time.time() + timeout
    needle = marker.encode("ascii")
    while time.time() < deadline:
        index = serial_bytes().find(needle, offset)
        if index >= 0:
            return index + len(needle)
        time.sleep(0.05)
    raise TimeoutError(f"timed out waiting for {marker!r}")


def monitor_line(process: subprocess.Popen, line: str) -> None:
    assert process.stdin is not None
    process.stdin.write((line + "\n").encode("ascii"))
    process.stdin.flush()
    time.sleep(0.025)


def send_command(process: subprocess.Popen, command: str,
                 timeout: float = 60) -> str:
    key_map = {" ": "spc", ".": "dot", "_": "shift-minus", "-": "minus"}
    start = len(serial_bytes())
    for character in command:
        monitor_line(process, f"sendkey {key_map.get(character, character)}")
    monitor_line(process, "sendkey ret")
    end = wait_for("OS64>", timeout, start)
    return serial_bytes()[start:end].decode(errors="replace")


def require(output: str, marker: str) -> None:
    if marker not in output:
        raise RuntimeError(f"missing marker {marker!r}:\n{output}")


def resources(process: subprocess.Popen) -> tuple[int, ...]:
    output = send_command(process, "resources")
    matches = list(RESOURCE_RE.finditer(output))
    if not matches:
        raise RuntimeError("thread soak resource snapshot missing")
    return tuple(int(value, 16) for value in matches[-1].groups())


def thread_cycle(process: subprocess.Popen) -> None:
    require(send_command(process, "run uthread_c.elf"), "[THREAD] PASS")
    require(send_command(process, "run usync_c.elf"), "[SYNC] PASS")
    require(send_command(process, "run uthread_ready_c.elf"), "[READY] PASS")


def gui_cycle(process: subprocess.Popen) -> None:
    require(send_command(process, "run ugui_cycle_c.elf"),
            "[ugui-cycle] lifecycle OK cycles=4")


def run(duration: float) -> int:
    os.chdir(ROOT)
    SERIAL.parent.mkdir(parents=True, exist_ok=True)
    SERIAL.unlink(missing_ok=True)
    QEMU_LOG.unlink(missing_ok=True)
    run_id = os.getpid()
    esp = Path(f"/tmp/os64_thread_soak_{run_id}_esp.img")
    variables = Path(f"/tmp/os64_thread_soak_{run_id}_vars.fd")
    shutil.copyfile(ROOT / "bin/uefi_esp.img", esp)
    shutil.copyfile("/usr/share/OVMF/OVMF_VARS_4M.fd", variables)
    qemu = [
        "qemu-system-x86_64", "-machine", "q35", "-m", "512M", "-cpu", "max",
        "-drive", "if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd",
        "-drive", f"if=pflash,format=raw,file={variables}",
        "-drive", f"if=none,id=esp,format=raw,file={esp}",
        "-device", "virtio-blk-pci,drive=esp,bootindex=1", "-boot", "menu=off",
        "-vga", "none", "-device", "VGA,xres=800,yres=600", "-display", "none",
        "-serial", f"file:{SERIAL}", "-monitor", "stdio", "-no-reboot",
        "-d", "guest_errors,cpu_reset,int", "-D", str(QEMU_LOG),
    ]
    process = subprocess.Popen(qemu, stdin=subprocess.PIPE,
                               stdout=subprocess.DEVNULL,
                               stderr=subprocess.STDOUT)
    cycles = 0
    gui_cycles = 0
    try:
        wait_for("OS64>", 25)
        require(send_command(process, "service start input"),
                "[usvcctl] start input OK")
        require(send_command(process, "service start window"),
                "[usvcctl] start window OK")

        for _ in range(9):
            thread_cycle(process)
        for _ in range(3):
            gui_cycle(process)
        baseline = resources(process)

        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            thread_cycle(process)
            cycles += 1
            gui_cycle(process)
            gui_cycles += 4
            require(send_command(process, "service health window"),
                    "[usvcctl] health window OK")
            require(send_command(process, "service health display"),
                    "[usvcctl] health display OK")

        final = resources(process)
        if final != baseline:
            raise RuntimeError(
                f"thread soak resource drift: {baseline} -> {final}"
            )
        scheduler = send_command(process, "sched")
        require(scheduler, "Queue count: 0x00000000")
        locks = send_command(process, "locks")
        require(
            locks,
            "order_violations=0x0000000000000000 "
            "recursion_violations=0x0000000000000000 "
            "release_violations=0x0000000000000000",
        )
        log = serial_bytes().decode(errors="replace")
        if "KERNEL PANIC" in log or "Double fault" in log or \
                "[THREAD] FAIL" in log or "[SYNC] FAIL" in log or \
                "[READY] FAIL" in log or "[ugui-cycle] operation failed" in log:
            raise RuntimeError("fault marker observed during thread soak")
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

    print(
        f"thread soak OK duration={duration:.0f}s cycles={cycles} "
        f"thread_programs={cycles * 3} gui_windows={gui_cycles}"
    )
    print(f"resource baseline={baseline}")
    print(f"resource final={final}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--duration", type=float, default=60.0)
    args = parser.parse_args()
    return run(args.duration)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, TimeoutError) as error:
        print(error, file=sys.stderr)
        raise SystemExit(1)
