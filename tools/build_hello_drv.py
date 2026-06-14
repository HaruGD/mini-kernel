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
DRV_SYMBOL_OBJECT = 2
DRV_RELOC_ABS64 = 1
DRV_RELOC_REL32 = 2

HEADER_FORMAT = "<QIIIIQQQQIIQIIQIIQIIQIIQQQQ"
MANIFEST_FORMAT = "<32s16s32sIIII"
SECTION_FORMAT = "<16sIIQQQQ"
SYMBOL_FORMAT = "<48sIIQQ"
IMPORT_FORMAT = "<32s48sIIQ"
RELOCATION_FORMAT = "<IIQQq"


def zstr(text: str, size: int) -> bytes:
    data = text.encode("ascii")
    if len(data) >= size:
        raise SystemExit(f"{text}: string is too long for {size} bytes")
    return data + bytes(size - len(data))


def checksum64(data: bytes) -> int:
    value = 0xCBF29CE484222325
    for byte in data:
        value ^= byte
        value = (value * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


def local_signature(unsigned_prefix: bytes, certificate: bytes, file_size: int) -> bytes:
    value = checksum64(unsigned_prefix) ^ checksum64(certificate) ^ file_size
    return b"OS64SIG0" + struct.pack("<Q", value)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    header_size = struct.calcsize(HEADER_FORMAT)
    manifest_size = struct.calcsize(MANIFEST_FORMAT)
    section_size = struct.calcsize(SECTION_FORMAT)
    symbol_size = struct.calcsize(SYMBOL_FORMAT)
    import_size = struct.calcsize(IMPORT_FORMAT)
    relocation_size = struct.calcsize(RELOCATION_FORMAT)

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
    helper_offset = 8
    helper_size = 25
    message_offset = helper_offset + helper_size
    rel32_patch_offset = 1
    abs64_patch_offset = helper_offset + 2
    import_patch_offset = helper_offset + 12
    code = (
        b"\xE8" + struct.pack("<i", 0) +
        b"\x31\xC0" +
        b"\xC3" +
        b"\x48\xBF" + struct.pack("<Q", 0) +
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
    symbols = b"".join([
        struct.pack(
            SYMBOL_FORMAT,
            zstr("driver_entry", 48),
            DRV_SYMBOL_FUNC,
            0,
            0,
            helper_offset,
        ),
        struct.pack(
            SYMBOL_FORMAT,
            zstr("hello_log", 48),
            DRV_SYMBOL_FUNC,
            0,
            helper_offset,
            helper_size,
        ),
        struct.pack(
            SYMBOL_FORMAT,
            zstr("hello_message", 48),
            DRV_SYMBOL_OBJECT,
            0,
            message_offset,
            len(message),
        ),
    ])
    import_entry = struct.pack(
        IMPORT_FORMAT,
        zstr("kernel", 32),
        zstr("klog", 48),
        0,
        0,
        import_patch_offset,
    )
    relocations = b"".join([
        struct.pack(RELOCATION_FORMAT, DRV_RELOC_REL32, 0, rel32_patch_offset, 1, 0),
        struct.pack(RELOCATION_FORMAT, DRV_RELOC_ABS64, 0, abs64_patch_offset, 2, 0),
    ])

    certificate = b"OS64LOCALTESTCERT"
    signature_size = 16

    manifest_offset = header_size
    section_table_offset = manifest_offset + len(manifest)
    symbol_table_offset = section_table_offset + len(section)
    import_table_offset = symbol_table_offset + len(symbols)
    code_offset = import_table_offset + len(import_entry)
    relocation_table_offset = code_offset + len(code)
    signature_offset = relocation_table_offset + len(relocations)
    certificate_offset = signature_offset + signature_size
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
        3,
        symbol_size,
        import_table_offset,
        1,
        import_size,
        0,
        0,
        0,
        relocation_table_offset,
        2,
        relocation_size,
        signature_offset,
        signature_size,
        certificate_offset,
        len(certificate),
    )
    unsigned_prefix = b"".join([
        header,
        manifest,
        section,
        symbols,
        import_entry,
        code,
        relocations,
    ])
    signature = local_signature(unsigned_prefix, certificate, file_size)

    with open(args.output, "wb") as f:
        f.write(unsigned_prefix)
        f.write(signature)
        f.write(certificate)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
