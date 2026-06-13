#!/usr/bin/env python3
import argparse
import struct


DRV_MAGIC = 0x5652443436534F
DRV_FORMAT_VERSION = 1
DRV_ABI_VERSION = 1
DRV_ARCH_X86_64 = 0x8664

DRV_BOOT_NORMAL = 0x00000001
DRV_SECTION_CODE = 1
DRV_SYMBOL_FUNC = 1

HEADER_FORMAT = "<QIIIIQQQQIIQIIQIIQIIQIIQQQQ"
MANIFEST_FORMAT = "<32s16s32sIIII"
SECTION_FORMAT = "<16sIIQQQQ"
SYMBOL_FORMAT = "<48sIIQQ"
IMPORT_FORMAT = "<32s48sIIQ"


def zstr(text: str, size: int) -> bytes:
    data = text.encode("ascii")
    if len(data) >= size:
        raise SystemExit(f"{text}: string is too long for {size} bytes")
    return data + bytes(size - len(data))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    header_size = struct.calcsize(HEADER_FORMAT)
    manifest_size = struct.calcsize(MANIFEST_FORMAT)
    section_size = struct.calcsize(SECTION_FORMAT)
    symbol_size = struct.calcsize(SYMBOL_FORMAT)
    import_size = struct.calcsize(IMPORT_FORMAT)

    manifest = struct.pack(
        MANIFEST_FORMAT,
        zstr("hello", 32),
        zstr("0.1.0", 16),
        zstr("driver_entry", 32),
        0,
        DRV_BOOT_NORMAL,
        0,
        0,
    )

    message = b"hello.drv driver_entry()\x00"
    message_offset = 22
    rip_after_lea = 7
    message_disp = message_offset - rip_after_lea
    import_patch_offset = 9
    code = (
        b"\x48\x8D\x3D" + struct.pack("<i", message_disp) +
        b"\x48\xB8" + struct.pack("<Q", 0) +
        b"\xFF\xD0" +
        b"\x31\xC0" +
        b"\xC3" +
        message
    )
    section = struct.pack(
        SECTION_FORMAT,
        zstr(".text", 16),
        DRV_SECTION_CODE,
        0,
        0,  # patched after offsets are known
        len(code),
        len(code),
        16,
    )
    symbol = struct.pack(
        SYMBOL_FORMAT,
        zstr("driver_entry", 48),
        DRV_SYMBOL_FUNC,
        0,
        0,
        len(code),
    )
    import_entry = struct.pack(
        IMPORT_FORMAT,
        zstr("kernel", 32),
        zstr("klog", 48),
        0,
        0,
        import_patch_offset,
    )

    signature = b"OS64TESTSIGNATURE"
    certificate = b"OS64TESTCERT"

    manifest_offset = header_size
    section_table_offset = manifest_offset + len(manifest)
    symbol_table_offset = section_table_offset + len(section)
    import_table_offset = symbol_table_offset + len(symbol)
    code_offset = import_table_offset + len(import_entry)
    signature_offset = code_offset + len(code)
    certificate_offset = signature_offset + len(signature)
    file_size = certificate_offset + len(certificate)

    section = struct.pack(
        SECTION_FORMAT,
        zstr(".text", 16),
        DRV_SECTION_CODE,
        0,
        code_offset,
        len(code),
        len(code),
        16,
    )

    header = struct.pack(
        HEADER_FORMAT,
        DRV_MAGIC,
        DRV_FORMAT_VERSION,
        DRV_ABI_VERSION,
        DRV_ARCH_X86_64,
        0,
        file_size,
        manifest_offset,
        len(manifest),
        section_table_offset,
        1,
        section_size,
        symbol_table_offset,
        1,
        symbol_size,
        import_table_offset,
        1,
        import_size,
        0,
        0,
        0,
        0,
        0,
        0,
        signature_offset,
        len(signature),
        certificate_offset,
        len(certificate),
    )

    with open(args.output, "wb") as f:
        f.write(header)
        f.write(manifest)
        f.write(section)
        f.write(symbol)
        f.write(import_entry)
        f.write(code)
        f.write(signature)
        f.write(certificate)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
