#!/usr/bin/env python3
import re
import shutil
import subprocess
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
SERIAL = ROOT / "logs/serial_surface_mapping.log"
QEMU_LOG = ROOT / "logs/qemu_surface_mapping.log"
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


def send_command(process: subprocess.Popen, command: str, timeout: float = 30) -> str:
    key_map = {" ": "spc", ".": "dot", "_": "shift-minus", "-": "minus"}
    start = len(serial_bytes())
    for char in command:
        monitor_line(process, f"sendkey {key_map.get(char, char)}")
        time.sleep(0.02)
    monitor_line(process, "sendkey ret")
    end = wait_for("OS64>", timeout, start)
    return serial_bytes()[start:end].decode(errors="replace")


def resources(text: str) -> tuple[int, ...]:
    matches = list(RESOURCE_RE.finditer(text))
    if not matches:
        raise RuntimeError("resource snapshot missing")
    return tuple(int(value, 16) for value in matches[-1].groups())


def main() -> int:
    SERIAL.parent.mkdir(parents=True, exist_ok=True)
    SERIAL.unlink(missing_ok=True)
    QEMU_LOG.unlink(missing_ok=True)
    image = Path("/tmp/os64_surface_mapping_esp.img")
    vars_image = Path("/tmp/os64_surface_mapping_vars.fd")
    shutil.copyfile(ROOT / "bin/uefi_esp.img", image)
    shutil.copyfile("/usr/share/OVMF/OVMF_VARS_4M.fd", vars_image)
    command = [
        "qemu-system-x86_64", "-machine", "q35", "-m", "512M", "-cpu", "max",
        "-drive", "if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd",
        "-drive", f"if=pflash,format=raw,file={vars_image}",
        "-drive", f"if=none,id=esp,format=raw,file={image}",
        "-device", "virtio-blk-pci,drive=esp,bootindex=1", "-boot", "menu=off",
        "-display", "none", "-serial", f"file:{SERIAL}", "-monitor", "stdio",
        "-no-reboot", "-d", "guest_errors,cpu_reset,int", "-D", str(QEMU_LOG),
    ]
    process = subprocess.Popen(command, cwd=ROOT, stdin=subprocess.PIPE,
                               stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    try:
        wait_for("OS64>", 25)
        for _ in range(16):
            output = send_command(process, "run usdk_c.elf surface-leak")
            if "[usurface-leak] mapped exit" not in output:
                raise RuntimeError("mapped-exit marker missing")
        baseline = resources(send_command(process, "resources"))
        for _ in range(12):
            output = send_command(process, "run usdk_c.elf surface-leak")
            if "[usurface-leak] mapped exit" not in output:
                raise RuntimeError("mapped-exit marker missing")
        final = resources(send_command(process, "resources"))
        if baseline != final:
            raise RuntimeError(f"resource drift: baseline={baseline} final={final}")
        if final[1] != 0 or final[2] != 0 or final[6] != 0:
            raise RuntimeError(f"surface cleanup incomplete: {final}")
        output = serial_bytes().decode(errors="replace")
        if "KERNEL PANIC" in output or "Double fault" in output:
            raise RuntimeError("kernel fault observed")
    except (RuntimeError, TimeoutError) as error:
        try:
            print(send_command(process, "locks", 10))
            print(send_command(process, "resources", 10))
            print(send_command(process, "cpus", 10))
        except (RuntimeError, TimeoutError):
            pass
        print(f"surface mapping smoke failed: {error}")
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
    print(f"surface mapping smoke OK: baseline={baseline} final={final}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
