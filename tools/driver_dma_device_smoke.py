#!/usr/bin/env python3
import shutil
import subprocess
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MARKERS = (
    "[drv] pci_probe_c.drv QEMU EDU DMA round trip OK",
    "[drv] pci_probe_c.drv QEMU EDU IRQ completion OK",
    "[drv] pci_probe_c.drv QEMU EDU streaming DMA round trip OK",
    "[drv] pci_probe_c.drv QEMU EDU SG DMA round trip OK",
)


def run(cpus: int) -> None:
    serial = ROOT / "logs" / f"serial_driver_dma_device_{cpus}cpu.log"
    serial.parent.mkdir(exist_ok=True)
    serial.unlink(missing_ok=True)
    stamp = f"{int(time.time() * 1000)}_{cpus}"
    esp = Path(f"/tmp/os64_dma_device_{stamp}.img")
    variables = Path(f"/tmp/os64_dma_device_{stamp}.fd")
    shutil.copyfile(ROOT / "bin" / "uefi_esp.img", esp)
    shutil.copyfile("/usr/share/OVMF/OVMF_VARS_4M.fd", variables)
    command = [
        "qemu-system-x86_64", "-machine", "q35", "-m", "512M",
        "-cpu", "max", "-smp", str(cpus),
        "-drive", "if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd",
        "-drive", f"if=pflash,format=raw,file={variables}",
        "-drive", f"if=none,id=esp,format=raw,file={esp}",
        "-device", "virtio-blk-pci,drive=esp,bootindex=1",
        "-device", "edu,dma_mask=0xffffffff", "-boot", "menu=off",
        "-display", "none", "-serial", f"file:{serial}",
        "-monitor", "stdio", "-no-reboot",
    ]
    process = subprocess.Popen(command, cwd=ROOT, stdin=subprocess.PIPE,
                               stdout=subprocess.DEVNULL,
                               stderr=subprocess.STDOUT)
    deadline = time.monotonic() + 35
    try:
        while time.monotonic() < deadline:
            text = serial.read_text(errors="replace") if serial.exists() else ""
            if all(marker in text for marker in MARKERS) and "OS64>" in text:
                break
            if process.poll() is not None:
                raise RuntimeError(f"QEMU exited early for {cpus} vCPU")
            time.sleep(0.1)
        else:
            raise TimeoutError(f"EDU completion timeout for {cpus} vCPU")
        if "KERNEL PANIC" in text or "CPU EMERGENCY" in text:
            raise RuntimeError(f"fatal kernel path for {cpus} vCPU")
    finally:
        if process.poll() is None:
            assert process.stdin is not None
            process.stdin.write(b"quit\n")
            process.stdin.flush()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=3)
        if process.stdin is not None:
            process.stdin.close()
        esp.unlink(missing_ok=True)
        variables.unlink(missing_ok=True)


def main() -> int:
    for cpus in (1, 4):
        run(cpus)
    print("QEMU EDU MMIO, IRQ, coherent, streaming, and SG DMA smoke OK (1/4 vCPU)")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, TimeoutError) as error:
        print(error)
        raise SystemExit(1)
