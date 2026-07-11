#!/usr/bin/env python3
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
SERIAL = ROOT / "logs" / "serial_service_manager_smoke.log"
QEMU_LOG = ROOT / "logs" / "qemu_service_manager_smoke.log"
MONITOR_OUTPUT = Path("/tmp/os64_service_manager_smoke_monitor.txt")


def wait_for_serial(pattern: str, timeout_seconds: float, start_offset: int = 0) -> int:
    deadline = time.time() + timeout_seconds
    needle = pattern.encode("ascii")
    while time.time() < deadline:
        try:
            data = SERIAL.read_bytes()
            index = data.find(needle, start_offset)
            if index >= 0:
                return index + len(needle)
        except FileNotFoundError:
            pass
        time.sleep(0.1)
    raise TimeoutError(f"timed out waiting for serial marker: {pattern}")


def send_monitor_line(process: subprocess.Popen, line: str, delay: float = 0.05) -> None:
    assert process.stdin is not None
    process.stdin.write((line + "\n").encode("ascii"))
    process.stdin.flush()
    time.sleep(delay)


def send_command(process: subprocess.Popen, command: str) -> None:
    key_map = {
        " ": "spc",
        ".": "dot",
        "_": "shift-minus",
        "-": "minus",
        "/": "slash",
    }
    for ch in command:
        send_monitor_line(process, f"sendkey {key_map.get(ch, ch)}")
    send_monitor_line(process, "sendkey ret")


def run() -> int:
    os.chdir(ROOT)
    (ROOT / "logs").mkdir(exist_ok=True)
    SERIAL.unlink(missing_ok=True)
    QEMU_LOG.unlink(missing_ok=True)
    MONITOR_OUTPUT.unlink(missing_ok=True)

    run_id = os.getpid()
    esp = Path(f"/tmp/os64_service_manager_{run_id}_esp.img")
    vars_image = Path(f"/tmp/os64_service_manager_{run_id}_vars.fd")
    shutil.copyfile(ROOT / "bin" / "uefi_esp.img", esp)
    shutil.copyfile("/usr/share/OVMF/OVMF_VARS_4M.fd", vars_image)

    qemu = [
        "qemu-system-x86_64",
        "-machine", "q35",
        "-m", "512M",
        "-cpu", "max",
        "-drive", "if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd",
        "-drive", f"if=pflash,format=raw,file={vars_image}",
        "-drive", f"if=none,id=esp,format=raw,file={esp}",
        "-device", "virtio-blk-pci,drive=esp,bootindex=1",
        "-boot", "menu=off",
        "-display", "none",
        "-serial", f"file:{SERIAL}",
        "-monitor", "stdio",
        "-no-reboot",
        "-d", "guest_errors,cpu_reset,int",
        "-D", str(QEMU_LOG),
    ]

    with MONITOR_OUTPUT.open("wb") as output:
        process = subprocess.Popen(
            qemu,
            stdin=subprocess.PIPE,
            stdout=output,
            stderr=subprocess.STDOUT,
        )

    try:
        wait_for_serial("OS64>", 20)
        send_command(process, "service ping")
        wait_for_serial("[serviced] ready pid=", 20)
        marker = wait_for_serial("[usvcctl] ping service OK", 20)
        wait_for_serial("OS64>", 10, marker)

        send_command(process, "service start demo")
        wait_for_serial("[serviced] started base", 20)
        wait_for_serial("[serviced] started demo", 20)
        marker = wait_for_serial("[usvcctl] start demo OK", 20)
        wait_for_serial("OS64>", 10, marker)
        send_command(process, "services")
        wait_for_serial("name=service", 10)
        wait_for_serial("name=base", 10)
        marker = wait_for_serial("name=demo", 10)
        wait_for_serial("OS64>", 10, marker)

        send_command(process, "service health demo")
        marker = wait_for_serial("[usvcctl] health demo OK", 20)
        wait_for_serial("health=1 failure=0", 10, marker)
        wait_for_serial("OS64>", 10, marker)

        send_command(process, "service stop base")
        marker = wait_for_serial("[usvcctl] stop base failed", 20)
        wait_for_serial("OS64>", 10, marker)

        send_command(process, "service restart demo")
        wait_for_serial("[serviced] stopped demo", 20)
        marker = wait_for_serial("[usvcctl] restart demo OK", 20)
        wait_for_serial("OS64>", 10, marker)

        send_command(process, "service stop demo")
        marker = wait_for_serial("[usvcctl] stop demo OK", 20)
        wait_for_serial("OS64>", 10, marker)

        send_command(process, "service start restricted")
        wait_for_serial("[svc_perm] denied display, discovery, and child launch as expected", 20)
        marker = wait_for_serial("[usvcctl] start restricted OK", 20)
        wait_for_serial("OS64>", 10, marker)
        send_command(process, "service health restricted")
        marker = wait_for_serial("[usvcctl] health restricted OK", 20)
        wait_for_serial("OS64>", 10, marker)
        send_command(process, "service stop restricted")
        marker = wait_for_serial("[usvcctl] stop restricted OK", 20)
        wait_for_serial("OS64>", 10, marker)

        send_command(process, "service start crash")
        marker = 0
        for attempt in range(1, 4):
            attempt_marker = wait_for_serial(
                f"[serviced] auto-restart crash attempt={attempt}", 30, marker
            )
            marker = attempt_marker
        marker = wait_for_serial("[usvcctl] start crash failed", 30, marker)
        wait_for_serial("OS64>", 10, marker)
        send_command(process, "service status crash")
        marker = wait_for_serial("[usvcctl] status crash OK", 20, marker)
        wait_for_serial("state=4", 10, marker)
        wait_for_serial("failure=7", 10, marker)
        wait_for_serial("OS64>", 10, marker)

        send_command(process, "service exit")
        wait_for_serial("[serviced] exit", 20)
        marker = wait_for_serial("[usvcctl] exit service OK", 20)
        wait_for_serial("OS64>", 10, marker)
        send_command(process, "services")
        marker = wait_for_serial("count=0x00000000", 10)
        wait_for_serial("OS64>", 10, marker)
        send_command(process, "locks")
        wait_for_serial("=== CONCURRENCY ===", 10)
        wait_for_serial(
            "order_violations=0x0000000000000000 recursion_violations=0x0000000000000000 release_violations=0x0000000000000000",
            10,
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
        vars_image.unlink(missing_ok=True)

    serial_text = SERIAL.read_text(errors="replace")
    required = [
        "[usvcctl] ping service OK",
        "[serviced] started base",
        "[serviced] started demo",
        "[usvcctl] start demo OK",
        "name=service",
        "name=base",
        "name=demo",
        "[usvcctl] health demo OK",
        "[usvcctl] stop base failed",
        "[usvcctl] restart demo OK",
        "[usvcctl] stop demo OK",
        "[svc_perm] denied display, discovery, and child launch as expected",
        "[usvcctl] health restricted OK",
        "[serviced] auto-restart crash attempt=3",
        "[usvcctl] status crash OK",
        "[serviced] exit",
        "count=0x00000000",
        "=== CONCURRENCY ===",
    ]
    missing = [item for item in required if item not in serial_text]
    if missing:
        print("service manager smoke missing:", file=sys.stderr)
        for item in missing:
            print(item, file=sys.stderr)
        return 1

    print("service manager smoke OK")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(run())
    except TimeoutError as error:
        print(error, file=sys.stderr)
        raise SystemExit(1)
