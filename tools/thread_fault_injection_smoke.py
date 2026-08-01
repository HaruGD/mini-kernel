#!/usr/bin/env python3
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SERIAL = ROOT / "logs/serial_thread_faults.log"
QEMU_LOG = ROOT / "logs/qemu_thread_faults.log"
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


def resources(process: subprocess.Popen) -> tuple[int, ...]:
    output = send_command(process, "resources")
    matches = list(RESOURCE_RE.finditer(output))
    if not matches:
        raise RuntimeError("thread fault resource snapshot missing")
    return tuple(int(value, 16) for value in matches[-1].groups())


def require(output: str, marker: str) -> None:
    if marker not in output:
        raise RuntimeError(f"missing marker {marker!r}:\n{output}")


def main() -> int:
    cpu_count = os.environ.get("OS64_QEMU_CPUS", "1")
    os.chdir(ROOT)
    SERIAL.parent.mkdir(parents=True, exist_ok=True)
    SERIAL.unlink(missing_ok=True)
    QEMU_LOG.unlink(missing_ok=True)
    run_id = os.getpid()
    esp = Path(f"/tmp/os64_thread_fault_{run_id}_esp.img")
    variables = Path(f"/tmp/os64_thread_fault_{run_id}_vars.fd")
    shutil.copyfile(ROOT / "bin/uefi_diag_esp.img", esp)
    shutil.copyfile("/usr/share/OVMF/OVMF_VARS_4M.fd", variables)
    qemu = [
        "qemu-system-x86_64", "-machine", "q35", "-m", "512M", "-cpu", "max",
        "-smp", cpu_count,
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
    cases = (
        ("thread_record", "run uthread_c.elf"),
        ("thread_kernel_stack", "run uthread_c.elf"),
        ("thread_user_stack", "run uthread_c.elf"),
        ("thread_mapping", "run uthread_c.elf"),
        ("thread_wait", "run uthread_c.elf"),
        ("sync_object", "run usync_c.elf"),
    )
    try:
        wait_for("OS64>", 25)
        for _ in range(17):
            require(send_command(process, "run uthread_c.elf"), "[THREAD] PASS")
        baseline = resources(process)

        for point, command in cases:
            send_command(process, "faultinject off")
            require(send_command(process, f"faultinject {point} 0"),
                    f"fault injection armed point={point}")
            output = send_command(process, command, 60)
            if "KERNEL PANIC" in output or "Double fault" in output:
                raise RuntimeError(f"kernel fault while injecting {point}")
            status = send_command(process, "faultinject status")
            pattern = re.compile(
                rf"{point} attempts=0x[0-9A-Fa-f]+ "
                rf"failures=0x0000000000000001 armed=0x00000000"
            )
            if pattern.search(status) is None:
                raise RuntimeError(f"fault point did not fire: {point}\n{status}")
            current = resources(process)
            if current != baseline:
                raise RuntimeError(
                    f"resource drift after {point}: {baseline} -> {current}"
                )

        send_command(process, "faultinject off")
        fault_output = send_command(process, "run uthread_fault_c.elf", 60)
        for marker in (
            "[THREAD-FAULT] tid=",
            "Faulting thread: tid=",
            "=== PAGE FAULT ===",
            "state=failed term=page_fault",
        ):
            require(fault_output, marker)
        if "process-wide fault policy failed" in fault_output:
            raise RuntimeError("faulting sibling returned to user mode")
        final = resources(process)
        if final != baseline:
            raise RuntimeError(
                f"fatal sibling fault resource drift: {baseline} -> {final}"
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

    print(f"thread fault injection QEMU test OK cases={len(cases)}")
    print(f"resource baseline={baseline}")
    print(f"resource final={final}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, TimeoutError) as error:
        print(error, file=sys.stderr)
        raise SystemExit(1)
