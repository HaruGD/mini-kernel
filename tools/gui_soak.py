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
SERIAL = ROOT / "logs/serial_gui_soak.log"
QEMU_LOG = ROOT / "logs/qemu_gui_soak.log"
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
                 timeout: float = 45) -> str:
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
        raise RuntimeError("resource snapshot missing")
    return tuple(int(value, 16) for value in matches[-1].groups())


def gui_cycle(process: subprocess.Popen) -> None:
    output = send_command(process, "run ugui_cycle_c.elf", 60)
    require(output, "[ugui-cycle] lifecycle OK cycles=4")
    if "operation failed" in output or "create failed" in output:
        raise RuntimeError(f"GUI cycle failed:\n{output}")


def churn_window_stack(process: subprocess.Popen, full: bool) -> None:
    require(send_command(process, "service stop window"),
            "[usvcctl] stop window OK")
    if full:
        require(send_command(process, "service restart display"),
                "[usvcctl] restart display OK")
    require(send_command(process, "service start window"),
            "[usvcctl] start window OK")


def run(duration: float) -> int:
    SERIAL.parent.mkdir(parents=True, exist_ok=True)
    SERIAL.unlink(missing_ok=True)
    QEMU_LOG.unlink(missing_ok=True)
    run_id = os.getpid()
    esp = Path(f"/tmp/os64_gui_soak_{run_id}_esp.img")
    vars_image = Path(f"/tmp/os64_gui_soak_{run_id}_vars.fd")
    shutil.copyfile(ROOT / "bin/uefi_esp.img", esp)
    shutil.copyfile("/usr/share/OVMF/OVMF_VARS_4M.fd", vars_image)
    command = [
        "qemu-system-x86_64", "-machine", "q35", "-m", "512M", "-cpu", "max",
        "-drive", "if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd",
        "-drive", f"if=pflash,format=raw,file={vars_image}",
        "-drive", f"if=none,id=esp,format=raw,file={esp}",
        "-device", "virtio-blk-pci,drive=esp,bootindex=1", "-boot", "menu=off",
        "-vga", "none", "-device", "VGA,xres=800,yres=600", "-display", "none",
        "-serial", f"file:{SERIAL}", "-monitor", "stdio", "-no-reboot",
        "-d", "guest_errors,cpu_reset,int", "-D", str(QEMU_LOG),
    ]
    process = subprocess.Popen(command, cwd=ROOT, stdin=subprocess.PIPE,
                               stdout=subprocess.DEVNULL,
                               stderr=subprocess.STDOUT)
    cycles = 0
    restarts = 0
    try:
        wait_for("OS64>", 25)
        require(send_command(process, "service start input"),
                "[usvcctl] start input OK")
        require(send_command(process, "service start window"),
                "[usvcctl] start window OK")

        # Fill bounded process-result history and warm persistent GUI/service
        # allocations before the measured baseline.
        for _ in range(10):
            gui_cycle(process)
        churn_window_stack(process, True)
        for _ in range(2):
            gui_cycle(process)
        baseline = resources(process)

        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            gui_cycle(process)
            cycles += 1
            if cycles % 5 == 0:
                churn_window_stack(process, cycles % 10 == 0)
                restarts += 1
            if cycles % 3 == 0:
                require(send_command(process, "service health window"),
                        "[usvcctl] health window OK")
                require(send_command(process, "service health display"),
                        "[usvcctl] health display OK")

        final = resources(process)
        if baseline != final:
            raise RuntimeError(f"GUI soak resource drift: {baseline} -> {final}")
        locks = send_command(process, "locks")
        require(locks,
                "order_violations=0x0000000000000000 recursion_violations=0x0000000000000000 release_violations=0x0000000000000000")
        log = serial_bytes().decode(errors="replace")
        if "KERNEL PANIC" in log or "Double fault" in log or \
                "[ugui-cycle] operation failed" in log:
            raise RuntimeError("kernel or GUI fault observed during soak")
        print(f"GUI soak OK duration={duration:.0f}s cycles={cycles} "
              f"window_cycles={cycles * 4} restarts={restarts}")
        print(f"resource baseline={baseline}")
        print(f"resource final={final}")
        return 0
    finally:
        if process.poll() is None:
            try:
                monitor_line(process, "quit")
                process.wait(timeout=3)
            except (BrokenPipeError, subprocess.TimeoutExpired):
                process.kill()
                process.wait(timeout=3)
        esp.unlink(missing_ok=True)
        vars_image.unlink(missing_ok=True)


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
