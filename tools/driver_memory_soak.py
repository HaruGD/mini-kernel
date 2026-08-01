#!/usr/bin/env python3
import argparse
import re
import shutil
import subprocess
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SERIAL = ROOT / "logs" / "serial_driver_memory_soak.log"

RESOURCE_RE = re.compile(
    r"processes=0x([0-9A-Fa-f]+) mappings=0x([0-9A-Fa-f]+) "
    r"handles=0x([0-9A-Fa-f]+) mailboxes=0x([0-9A-Fa-f]+) "
    r"services=0x([0-9A-Fa-f]+).*?shared=0x([0-9A-Fa-f]+) "
    r"surfaces=0x([0-9A-Fa-f]+).*?pmm_free=0x([0-9A-Fa-f]+).*?"
    r"heap_used=0x([0-9A-Fa-f]+) heap_mapped=0x([0-9A-Fa-f]+)",
    re.DOTALL,
)
DRIVER_RE = re.compile(
    r"exports=0x([0-9A-Fa-f]+) bindings=0x([0-9A-Fa-f]+) "
    r"irq_hooks=0x([0-9A-Fa-f]+) resources=0x([0-9A-Fa-f]+).*?"
    r"image_va_active=0x([0-9A-Fa-f]+) image_va_free_pages=0x([0-9A-Fa-f]+).*?"
    r"alloc_active=0x([0-9A-Fa-f]+) alloc_bytes=0x([0-9A-Fa-f]+) "
    r"alloc_quarantine=0x([0-9A-Fa-f]+) mmio_active=0x([0-9A-Fa-f]+) "
    r"mmio_free_pages=0x([0-9A-Fa-f]+) mmio_quarantine_pages=0x([0-9A-Fa-f]+) "
    r"dma_domains=0x([0-9A-Fa-f]+) dma_coherent=0x([0-9A-Fa-f]+) "
    r"dma_bytes=0x([0-9A-Fa-f]+) dma_quarantine=0x([0-9A-Fa-f]+) "
    r"dma_streaming=0x([0-9A-Fa-f]+) dma_pinned=0x([0-9A-Fa-f]+) "
    r"drv_inflight=0x([0-9A-Fa-f]+) drv_quiescing=0x([0-9A-Fa-f]+) "
    r"drv_quiesce_timeouts=0x([0-9A-Fa-f]+)",
    re.DOTALL,
)


def serial_bytes() -> bytes:
    try:
        return SERIAL.read_bytes()
    except FileNotFoundError:
        return b""


def wait_for(marker: str, timeout: float, offset: int = 0) -> int:
    deadline = time.monotonic() + timeout
    needle = marker.encode("ascii")
    while time.monotonic() < deadline:
        index = serial_bytes().find(needle, offset)
        if index >= 0:
            return index + len(needle)
        time.sleep(0.05)
    raise TimeoutError(f"timed out waiting for {marker!r}")


def monitor(process: subprocess.Popen, line: str) -> None:
    assert process.stdin is not None
    process.stdin.write((line + "\n").encode("ascii"))
    process.stdin.flush()
    time.sleep(0.025)


def command(process: subprocess.Popen, text: str, timeout: float = 60) -> str:
    keys = {" ": "spc", ".": "dot", "_": "shift-minus", "-": "minus"}
    start = len(serial_bytes())
    for character in text:
        monitor(process, f"sendkey {keys.get(character, character)}")
    monitor(process, "sendkey ret")
    end = wait_for("OS64>", timeout, start)
    return serial_bytes()[start:end].decode(errors="replace")


def require(output: str, marker: str) -> None:
    if marker not in output:
        raise RuntimeError(f"missing {marker!r} in:\n{output}")


def snapshot(process: subprocess.Popen) -> tuple[tuple[int, ...], tuple[int, ...]]:
    resource_output = command(process, "resources")
    driver_output = command(process, "drivers")
    resources = list(RESOURCE_RE.finditer(resource_output))
    drivers = list(DRIVER_RE.finditer(driver_output))
    if not resources or not drivers:
        raise RuntimeError("driver-memory resource snapshot missing")
    return (
        tuple(int(value, 16) for value in resources[-1].groups()),
        tuple(int(value, 16) for value in drivers[-1].groups()),
    )


def reload_driver(process: subprocess.Popen) -> None:
    require(command(process, "drvunload pci_probe_c"), "DRV unload OK")
    after_unload = command(process, "bindings")
    if "pci_probe_c pci" in after_unload:
        raise RuntimeError(f"binding survived unload:\n{after_unload}")
    loaded = command(process, "drvload pci_probe_c.drv")
    require(loaded, "DRV load OK")
    require(loaded, "QEMU EDU DMA round trip OK")
    require(loaded, "QEMU EDU streaming DMA round trip OK")
    require(loaded, "QEMU EDU SG DMA round trip OK")


def gui_cycle(process: subprocess.Popen) -> None:
    require(command(process, "run ugui_cycle_c.elf"),
            "[ugui-cycle] lifecycle OK cycles=4")


def run(duration: float) -> int:
    SERIAL.parent.mkdir(exist_ok=True)
    SERIAL.unlink(missing_ok=True)
    stamp = int(time.time() * 1000)
    esp = Path(f"/tmp/os64_driver_memory_soak_{stamp}.img")
    variables = Path(f"/tmp/os64_driver_memory_soak_{stamp}.fd")
    shutil.copyfile(ROOT / "bin" / "uefi_esp.img", esp)
    shutil.copyfile("/usr/share/OVMF/OVMF_VARS_4M.fd", variables)
    qemu = [
        "qemu-system-x86_64", "-machine", "q35", "-m", "512M",
        "-cpu", "max", "-smp", "4",
        "-drive", "if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd",
        "-drive", f"if=pflash,format=raw,file={variables}",
        "-drive", f"if=none,id=esp,format=raw,file={esp}",
        "-device", "virtio-blk-pci,drive=esp,bootindex=1",
        "-device", "edu,dma_mask=0xffffffff", "-boot", "menu=off",
        "-vga", "none", "-device", "VGA,xres=800,yres=600",
        "-display", "none", "-serial", f"file:{SERIAL}",
        "-monitor", "stdio", "-no-reboot",
    ]
    process = subprocess.Popen(qemu, cwd=ROOT, stdin=subprocess.PIPE,
                               stdout=subprocess.DEVNULL,
                               stderr=subprocess.STDOUT)
    cycles = 0
    gui_cycles = 0
    try:
        wait_for("OS64>", 30)
        require(command(process, "service start input"),
                "[usvcctl] start input OK")
        require(command(process, "service start window"),
                "[usvcctl] start window OK")
        for _ in range(2):
            reload_driver(process)
        for _ in range(10):
            gui_cycle(process)
            gui_cycles += 1
        baseline = snapshot(process)

        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            reload_driver(process)
            cycles += 1
            gui_cycle(process)
            gui_cycles += 1
            if cycles % 3 == 0:
                require(command(process, "service health window"),
                        "[usvcctl] health window OK")

        final = snapshot(process)
        if baseline != final:
            raise RuntimeError(f"driver resource drift: {baseline} -> {final}")
        locks = command(process, "locks")
        require(locks,
                "order_violations=0x0000000000000000 recursion_violations=0x0000000000000000 release_violations=0x0000000000000000")
        log = serial_bytes().decode(errors="replace")
        if "KERNEL PANIC" in log or "CPU EMERGENCY" in log or \
                "result=quiesce_timeout" in log:
            raise RuntimeError("fatal or quiesce-timeout path observed")
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
    print(f"driver memory soak OK duration={duration:.0f}s cycles={cycles} "
          f"gui_cycles={gui_cycles} cpus=4")
    print(f"warmed={baseline}")
    print(f"final={final}")
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
        print(error)
        raise SystemExit(1)
