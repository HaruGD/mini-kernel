#!/usr/bin/env python3
import re
import shutil
import subprocess
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SERIAL = ROOT / "logs" / "serial_driver_faults.log"


def serial_bytes() -> bytes:
    try:
        return SERIAL.read_bytes()
    except FileNotFoundError:
        return b""


def monitor(process: subprocess.Popen, line: str) -> None:
    assert process.stdin is not None
    process.stdin.write((line + "\n").encode("ascii"))
    process.stdin.flush()
    time.sleep(0.025)


def wait_for(marker: str, timeout: float, offset: int = 0) -> int:
    needle = marker.encode("ascii")
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        index = serial_bytes().find(needle, offset)
        if index >= 0:
            return index + len(needle)
        time.sleep(0.05)
    raise TimeoutError(f"timed out waiting for {marker!r}")


def command(process: subprocess.Popen, text: str) -> str:
    keys = {" ": "spc", ".": "dot", "_": "shift-minus", "-": "minus"}
    start = len(serial_bytes())
    for character in text:
        monitor(process, f"sendkey {keys.get(character, character)}")
    monitor(process, "sendkey ret")
    end = wait_for("OS64>", 45, start)
    return serial_bytes()[start:end].decode(errors="replace")


def require(output: str, marker: str) -> None:
    if marker not in output:
        raise RuntimeError(f"missing {marker!r} in:\n{output}")


def arm(process: subprocess.Popen, point: str) -> None:
    require(command(process, f"faultinject {point} 0"),
            f"fault injection armed point={point}")


def main() -> int:
    SERIAL.parent.mkdir(exist_ok=True)
    SERIAL.unlink(missing_ok=True)
    stamp = int(time.time() * 1000)
    esp = Path(f"/tmp/os64_driver_faults_{stamp}.img")
    variables = Path(f"/tmp/os64_driver_faults_{stamp}.fd")
    shutil.copyfile(ROOT / "bin" / "uefi_diag_esp.img", esp)
    shutil.copyfile("/usr/share/OVMF/OVMF_VARS_4M.fd", variables)
    qemu = [
        "qemu-system-x86_64", "-machine", "q35", "-m", "512M",
        "-cpu", "max", "-smp", "4",
        "-drive", "if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd",
        "-drive", f"if=pflash,format=raw,file={variables}",
        "-drive", f"if=none,id=esp,format=raw,file={esp}",
        "-device", "virtio-blk-pci,drive=esp,bootindex=1",
        "-device", "edu,dma_mask=0xffffffff", "-boot", "menu=off",
        "-display", "none", "-serial", f"file:{SERIAL}",
        "-monitor", "stdio", "-no-reboot",
    ]
    process = subprocess.Popen(qemu, cwd=ROOT, stdin=subprocess.PIPE,
                               stdout=subprocess.DEVNULL,
                               stderr=subprocess.STDOUT)
    try:
        wait_for("OS64>", 30)
        arm(process, "driver_va_record")
        require(command(process, "drvload hello.drv"),
                "DRV load FAILED result=out_of_memory")
        require(command(process, "drvload hello.drv"), "DRV load OK")
        require(command(process, "drvunload hello"), "DRV unload OK")

        arm(process, "driver_image_page")
        require(command(process, "drvload hello.drv"),
                "DRV load FAILED result=out_of_memory")
        require(command(process, "drvload hello.drv"), "DRV load OK")
        arm(process, "driver_quiesce")
        require(command(process, "drvunload hello"),
                "DRV unload FAILED result=quiesce_timeout")
        require(command(process, "drvunload hello"), "DRV unload OK")

        # Probe failures are intentionally local to one device operation. A
        # retry load must still be possible and final owner cleanup exact.
        probe_points = (
            "driver_mmio_record", "driver_page_map", "driver_dma_record",
            "driver_dma_domain", "driver_page_run",
        )
        for point in probe_points:
            arm(process, point)
            require(command(process, "drvload pci_probe_c.drv"), "DRV load OK")
            require(command(process, "drvunload pci_probe_c"), "DRV unload OK")

        status = command(process, "faultinject status")
        for point in ("driver_va_record", "driver_image_page",
                      "driver_mmio_record", "driver_page_map",
                      "driver_dma_record", "driver_dma_domain",
                      "driver_page_run", "driver_quiesce"):
            match = re.search(
                rf"{point} attempts=0x[0-9A-Fa-f]+ failures=0x([0-9A-Fa-f]+)",
                status,
            )
            if match is None or int(match.group(1), 16) != 1:
                raise RuntimeError(f"fault point did not fire exactly once: {point}")
        drivers = command(process, "drivers")
        require(drivers, "drv_inflight=0x00000000")
        require(drivers, "dma_streaming=0x00000000 dma_pinned=0x00000000")
        log = serial_bytes().decode(errors="replace")
        if "KERNEL PANIC" in log or "CPU EMERGENCY" in log:
            raise RuntimeError("fatal path observed during driver fault injection")
    finally:
        if process.poll() is None:
            try:
                monitor(process, "quit")
                process.wait(timeout=3)
            except (BrokenPipeError, subprocess.TimeoutExpired):
                process.kill()
                process.wait(timeout=3)
        if process.stdin is not None:
            process.stdin.close()
        esp.unlink(missing_ok=True)
        variables.unlink(missing_ok=True)
    print("driver memory QEMU fault injection and retry smoke OK (4 vCPU)")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, TimeoutError) as error:
        print(error)
        raise SystemExit(1)
