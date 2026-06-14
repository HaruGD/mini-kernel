#!/usr/bin/env python3
import argparse
import struct
from pathlib import Path


HEADER_FORMAT = "<QIIIIQQQQIIQIIQIIQIIQIIQQQQ"
MANIFEST_FORMAT = "<32s16s32sIIII"
SECTION_FORMAT = "<16sIIQQQQ"
IMPORT_FORMAT = "<32s48sIIQ"
EXPORT_FORMAT = "<48sIIQ"
RELOCATION_FORMAT = "<IIQQq"

DRV_MAGIC = 0x5652443436534F
DRV_FORMAT_VERSION = 1
DRV_ABI_VERSION = 1


def cstr(data: bytes) -> str:
    return data.split(b"\0", 1)[0].decode("ascii", errors="replace")


def checksum64(data: bytes) -> int:
    value = 0xCBF29CE484222325
    for byte in data:
        value ^= byte
        value = (value * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


def permission_names(value: int) -> str:
    names = [
        (0x00000001, "PCI"),
        (0x00000002, "MMIO"),
        (0x00000004, "INTERRUPT"),
        (0x00000008, "BLOCK"),
        (0x00000010, "VFS"),
        (0x00000020, "INPUT"),
        (0x00000040, "TIMER"),
    ]
    text = [name for bit, name in names if value & bit]
    return "|".join(text) if text else "-"


def check_signature(image: bytes, header: tuple) -> str:
    signature_offset = header[23]
    signature_size = header[24]
    certificate_offset = header[25]
    certificate_size = header[26]
    file_size = header[5]
    if signature_size != 16:
        return "bad-size"
    signature = image[signature_offset:signature_offset + signature_size]
    certificate = image[certificate_offset:certificate_offset + certificate_size]
    if not signature.startswith(b"OS64SIG0"):
        return "bad-magic"
    if certificate != b"OS64LOCALTESTCERT":
        return "bad-cert"
    expected = struct.unpack_from("<Q", signature, 8)[0]
    actual = checksum64(image[:signature_offset]) ^ checksum64(certificate) ^ file_size
    return "ok" if actual == expected else f"bad-checksum actual=0x{actual:016x}"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("driver")
    args = parser.parse_args()

    path = Path(args.driver)
    image = path.read_bytes()
    if len(image) < struct.calcsize(HEADER_FORMAT):
        raise SystemExit(f"{path}: too small")

    header = struct.unpack_from(HEADER_FORMAT, image, 0)
    if header[0] != DRV_MAGIC:
        raise SystemExit(f"{path}: bad magic")
    if header[1] != DRV_FORMAT_VERSION or header[2] != DRV_ABI_VERSION:
        raise SystemExit(f"{path}: unsupported format/ABI")
    if header[5] != len(image):
        raise SystemExit(f"{path}: file size mismatch")

    manifest = struct.unpack_from(MANIFEST_FORMAT, image, header[6])
    print(f"file={path}")
    print(f"name={cstr(manifest[0])} version={cstr(manifest[1])} entry={cstr(manifest[2])}")
    print(f"permissions={permission_names(manifest[3])} boot_modes=0x{manifest[4]:08x}")
    print(f"sections={header[9]} symbols={header[12]} imports={header[15]} exports={header[18]} relocs={header[21]}")
    print(f"signature={check_signature(image, header)}")

    for index in range(header[9]):
        off = header[8] + index * header[10]
        section = struct.unpack_from(SECTION_FORMAT, image, off)
        print(f"section[{index}] {cstr(section[0])} kind={section[1]} file=0x{section[3]:x}+0x{section[4]:x} mem=0x{section[5]:x}")
    for index in range(header[15]):
        off = header[14] + index * header[16]
        item = struct.unpack_from(IMPORT_FORMAT, image, off)
        print(f"import[{index}] {cstr(item[0])}.{cstr(item[1])} requires={permission_names(item[2])} patch_section={item[3]} patch=0x{item[4]:x}")
    for index in range(header[18]):
        off = header[17] + index * header[19]
        item = struct.unpack_from(EXPORT_FORMAT, image, off)
        print(f"export[{index}] {cstr(item[0])} requires={permission_names(item[2])} address=0x{item[3]:x}")
    for index in range(header[21]):
        off = header[20] + index * header[22]
        item = struct.unpack_from(RELOCATION_FORMAT, image, off)
        print(f"reloc[{index}] type={item[0]} section={item[1]} offset=0x{item[2]:x} symbol={item[3]} addend={item[4]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
