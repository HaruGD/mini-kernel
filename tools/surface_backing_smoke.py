#!/usr/bin/env python3
import shutil
import subprocess
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
SERIAL = ROOT / "logs/serial_surface_backing.log"
QEMU_LOG = ROOT / "logs/qemu_surface_backing.log"
OVMF_VARS_TEMPLATE = Path("/usr/share/OVMF/OVMF_VARS_4M.fd")


def serial_bytes() -> bytes:
    try:
        return SERIAL.read_bytes()
    except FileNotFoundError:
        return b""


def wait_for(marker: str, timeout: float, offset: int = 0) -> int:
    needle = marker.encode("ascii")
    deadline = time.time() + timeout
    while time.time() < deadline:
        index = serial_bytes().find(needle, offset)
        if index >= 0:
            return index + len(needle)
        time.sleep(0.1)
    raise TimeoutError(f"timed out waiting for {marker!r}")


def monitor_line(process: subprocess.Popen, line: str) -> None:
    assert process.stdin is not None
    process.stdin.write((line + "\n").encode("ascii"))
    process.stdin.flush()


def send_command(process: subprocess.Popen, command: str) -> int:
    key_map = {" ": "spc", ".": "dot", "-": "minus", "_": "shift-minus"}
    start = len(serial_bytes())
    for char in command:
        monitor_line(process, f"sendkey {key_map.get(char, char)}")
        time.sleep(0.03)
    monitor_line(process, "sendkey ret")
    return start


def main() -> int:
    SERIAL.parent.mkdir(parents=True, exist_ok=True)
    SERIAL.unlink(missing_ok=True)
    QEMU_LOG.unlink(missing_ok=True)
    image = Path("/tmp/os64_surface_backing_esp.img")
    vars_image = Path("/tmp/os64_surface_backing_vars.fd")
    image.unlink(missing_ok=True)
    vars_image.unlink(missing_ok=True)
    shutil.copyfile(ROOT / "bin/uefi_esp.img", image)
    shutil.copyfile(OVMF_VARS_TEMPLATE, vars_image)

    qemu = [
        "qemu-system-x86_64", "-machine", "q35", "-m", "512M", "-cpu", "max",
        "-drive", "if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd",
        "-drive", f"if=pflash,format=raw,file={vars_image}",
        "-drive", f"if=none,id=esp,format=raw,file={image}",
        "-device", "virtio-blk-pci,drive=esp,bootindex=1", "-boot", "menu=off",
        "-display", "none", "-serial", f"file:{SERIAL}", "-monitor", "stdio",
        "-no-reboot", "-d", "guest_errors,cpu_reset,int", "-D", str(QEMU_LOG),
    ]
    process = subprocess.Popen(
        qemu, cwd=ROOT, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
        stderr=subprocess.STDOUT
    )
    try:
        wait_for("OS64>", 25)
        offset = send_command(process, "surfacetest")
        wait_for("SURFACETEST passed=0x00000009 expected=0x00000009 result=ok", 15, offset)
        output = serial_bytes()[offset:].decode(errors="replace")
        if "KERNEL PANIC" in output or "Double fault" in output:
            raise RuntimeError("kernel fault observed")
    except (RuntimeError, TimeoutError) as error:
        print(f"surface backing smoke failed: {error}")
        return 1
    finally:
        if process.poll() is None:
            monitor_line(process, "quit")
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        if process.stdin is not None:
            process.stdin.close()
        image.unlink(missing_ok=True)
        vars_image.unlink(missing_ok=True)

    print("surface backing smoke OK: 2 pages, cross-page read/write, zero drift")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
