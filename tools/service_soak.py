#!/usr/bin/env python3
import argparse
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
SERIAL = ROOT / "logs" / "serial_service_soak.log"
QEMU_LOG = ROOT / "logs" / "qemu_service_soak.log"
MONITOR_OUTPUT = Path("/tmp/os64_service_soak_monitor.txt")

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


def wait_for(pattern: str, timeout: float, offset: int = 0) -> int:
    needle = pattern.encode("ascii")
    deadline = time.time() + timeout
    while time.time() < deadline:
        data = serial_bytes()
        index = data.find(needle, offset)
        if index >= 0:
            return index + len(needle)
        time.sleep(0.1)
    raise TimeoutError(f"timed out waiting for {pattern!r}")


def monitor_line(process: subprocess.Popen, line: str, delay: float = 0.03) -> None:
    assert process.stdin is not None
    process.stdin.write((line + "\n").encode("ascii"))
    process.stdin.flush()
    time.sleep(delay)


def send_command(process: subprocess.Popen, command: str, timeout: float = 30) -> str:
    key_map = {" ": "spc", ".": "dot", "_": "shift-minus", "-": "minus", "/": "slash"}
    start = len(serial_bytes())
    for char in command:
        monitor_line(process, f"sendkey {key_map.get(char, char)}")
    monitor_line(process, "sendkey ret")
    end = wait_for("OS64>", timeout, start)
    return serial_bytes()[start:end].decode(errors="replace")


def parse_resources(text: str) -> tuple[int, ...]:
    matches = list(RESOURCE_RE.finditer(text))
    if not matches:
        raise RuntimeError("resource snapshot missing")
    return tuple(int(value, 16) for value in matches[-1].groups())


def require(text: str, marker: str) -> None:
    if marker not in text:
        raise RuntimeError(f"missing marker {marker!r}")


def run(duration: float) -> int:
    os.chdir(ROOT)
    (ROOT / "logs").mkdir(exist_ok=True)
    SERIAL.unlink(missing_ok=True)
    QEMU_LOG.unlink(missing_ok=True)
    MONITOR_OUTPUT.unlink(missing_ok=True)
    run_id = os.getpid()
    esp = Path(f"/tmp/os64_service_soak_{run_id}_esp.img")
    vars_image = Path(f"/tmp/os64_service_soak_{run_id}_vars.fd")
    shutil.copyfile(ROOT / "bin" / "uefi_esp.img", esp)
    shutil.copyfile("/usr/share/OVMF/OVMF_VARS_4M.fd", vars_image)
    qemu = [
        "qemu-system-x86_64", "-machine", "q35", "-m", "512M", "-cpu", "max",
        "-drive", "if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd",
        "-drive", f"if=pflash,format=raw,file={vars_image}",
        "-drive", f"if=none,id=esp,format=raw,file={esp}",
        "-device", "virtio-blk-pci,drive=esp,bootindex=1", "-boot", "menu=off",
        "-display", "none", "-serial", f"file:{SERIAL}", "-monitor", "stdio",
        "-no-reboot", "-d", "guest_errors,cpu_reset,int", "-D", str(QEMU_LOG),
    ]
    with MONITOR_OUTPUT.open("wb") as output:
        process = subprocess.Popen(qemu, stdin=subprocess.PIPE, stdout=output, stderr=subprocess.STDOUT)

    cycles = 0
    try:
        wait_for("OS64>", 25)
        require(send_command(process, "run usoak_c.elf"), "[usoak] scheduler churn OK")
        require(send_command(process, "run uping_c.elf"), "[uping] IPC roundtrip OK")
        for _ in range(2):
            require(send_command(process, "service start input"), "[usvcctl] start input OK")
            require(send_command(process, "service start display"), "[usvcctl] start display OK")
            require(send_command(process, "service health input"), "[usvcctl] health input OK")
            require(send_command(process, "service exit"), "[usvcctl] exit service OK")
        baseline = parse_resources(send_command(process, "resources"))

        require(send_command(process, "service start input"), "[usvcctl] start input OK")
        require(send_command(process, "service start display"), "[usvcctl] start display OK")
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            require(send_command(process, "service health input"), "[usvcctl] health input OK")
            require(send_command(process, "service health display"), "[usvcctl] health display OK")
            cycles += 1
            if cycles % 10 == 0:
                require(send_command(process, "service restart display"), "[usvcctl] restart display OK")

        require(send_command(process, "service exit"), "[usvcctl] exit service OK")
        final = parse_resources(send_command(process, "resources"))
        locks = send_command(process, "locks")
        require(locks, "order_violations=0x0000000000000000 recursion_violations=0x0000000000000000 release_violations=0x0000000000000000")
        if baseline != final:
            raise RuntimeError(f"resource drift baseline={baseline} final={final}")
        text = serial_bytes().decode(errors="replace")
        if "KERNEL PANIC" in text or "Double fault" in text:
            raise RuntimeError("kernel fault observed during soak")
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
        vars_image.unlink(missing_ok=True)

    print(f"service soak OK duration={duration:.0f}s cycles={cycles}")
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
