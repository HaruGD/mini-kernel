#!/usr/bin/env python3
import argparse
import struct
from pathlib import Path


DRV_MAGIC = 0x5652443436534F
DRV_FORMAT_VERSION = 1
DRV_ABI_VERSION = 1
DRV_ARCH_X86_64 = 0x8664

DRV_BOOT_NORMAL = 0x00000001
DRV_SECTION_CODE = 1
DRV_SYMBOL_FUNC = 1
DRV_SYMBOL_OBJECT = 2
DRV_RELOC_ABS64 = 1

HEADER_FORMAT = "<QIIIIQQQQIIQIIQIIQIIQIIQQQQ"
MANIFEST_FORMAT = "<32s16s32sIIII"
SECTION_FORMAT = "<16sIIQQQQ"
SYMBOL_FORMAT = "<48sIIQQ"
IMPORT_FORMAT = "<32s48sIIQ"
EXPORT_FORMAT = "<48sIIQ"
RELOCATION_FORMAT = "<IIQQq"


def zstr(text: str, size: int) -> bytes:
    data = text.encode("ascii")
    if len(data) >= size:
        raise SystemExit(f"{text}: string is too long for {size} bytes")
    return data + bytes(size - len(data))


def pack_driver(name: str,
                entry: str,
                code: bytes,
                symbols: list[tuple[str, int, int, int, int]],
                imports: list[tuple[str, str, int]],
                exports: list[tuple[str, int]],
                relocations: list[tuple[int, int]]) -> bytes:
    header_size = struct.calcsize(HEADER_FORMAT)
    section_size = struct.calcsize(SECTION_FORMAT)
    symbol_size = struct.calcsize(SYMBOL_FORMAT)
    import_size = struct.calcsize(IMPORT_FORMAT)
    export_size = struct.calcsize(EXPORT_FORMAT)
    relocation_size = struct.calcsize(RELOCATION_FORMAT)

    manifest = struct.pack(
        MANIFEST_FORMAT,
        zstr(name, 32),
        zstr("0.1.0", 16),
        zstr(entry, 32),
        0,
        DRV_BOOT_NORMAL,
        0,
        0,
    )
    section = struct.pack(SECTION_FORMAT, zstr(".text", 16), DRV_SECTION_CODE, 0, 0, len(code), len(code), 16)
    symbol_table = b"".join(
        struct.pack(SYMBOL_FORMAT, zstr(sym_name, 48), kind, section_index, value, size)
        for sym_name, kind, section_index, value, size in symbols
    )
    import_table = b"".join(
        struct.pack(IMPORT_FORMAT, zstr(module, 32), zstr(import_name, 48), 0, 0, patch_offset)
        for module, import_name, patch_offset in imports
    )
    export_table = b"".join(
        struct.pack(EXPORT_FORMAT, zstr(export_name, 48), DRV_SYMBOL_FUNC, 0, address)
        for export_name, address in exports
    )
    relocation_table = b"".join(
        struct.pack(RELOCATION_FORMAT, DRV_RELOC_ABS64, 0, offset, symbol_index, 0)
        for offset, symbol_index in relocations
    )

    manifest_offset = header_size
    section_table_offset = manifest_offset + len(manifest)
    symbol_table_offset = section_table_offset + len(section)
    import_table_offset = symbol_table_offset + len(symbol_table)
    export_table_offset = import_table_offset + len(import_table)
    code_offset = export_table_offset + len(export_table)
    relocation_table_offset = code_offset + len(code)
    signature = b"OS64TESTSIGNATURE"
    certificate = b"OS64TESTCERT"
    signature_offset = relocation_table_offset + len(relocation_table)
    certificate_offset = signature_offset + len(signature)
    file_size = certificate_offset + len(certificate)

    section = struct.pack(SECTION_FORMAT, zstr(".text", 16), DRV_SECTION_CODE, 0, code_offset, len(code), len(code), 16)
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
        len(symbols),
        symbol_size,
        import_table_offset,
        len(imports),
        import_size,
        export_table_offset,
        len(exports),
        export_size,
        relocation_table_offset,
        len(relocations),
        relocation_size,
        signature_offset,
        len(signature),
        certificate_offset,
        len(certificate),
    )
    return b"".join([
        header,
        manifest,
        section,
        symbol_table,
        import_table,
        export_table,
        code,
        relocation_table,
        signature,
        certificate,
    ])


def build_provider() -> bytes:
    entry_message = b"provider.drv driver_entry()\x00"
    ping_message = b"provider.drv provider_ping()\x00"
    log_stub_size = 25
    entry_offset = 0
    ping_offset = log_stub_size
    entry_message_offset = log_stub_size * 2
    ping_message_offset = entry_message_offset + len(entry_message)
    code = (
        b"\x48\xBF" + struct.pack("<Q", 0) +
        b"\x48\xB8" + struct.pack("<Q", 0) +
        b"\xFF\xD0" +
        b"\x31\xC0" +
        b"\xC3" +
        b"\x48\xBF" + struct.pack("<Q", 0) +
        b"\x48\xB8" + struct.pack("<Q", 0) +
        b"\xFF\xD0" +
        b"\x31\xC0" +
        b"\xC3" +
        entry_message +
        ping_message
    )
    symbols = [
        ("driver_entry", DRV_SYMBOL_FUNC, 0, entry_offset, log_stub_size),
        ("provider_ping", DRV_SYMBOL_FUNC, 0, ping_offset, log_stub_size),
        ("entry_message", DRV_SYMBOL_OBJECT, 0, entry_message_offset, len(entry_message)),
        ("ping_message", DRV_SYMBOL_OBJECT, 0, ping_message_offset, len(ping_message)),
    ]
    imports = [
        ("kernel", "klog", 12),
        ("kernel", "klog", ping_offset + 12),
    ]
    exports = [("provider_ping", ping_offset)]
    relocations = [(2, 2), (ping_offset + 2, 3)]
    return pack_driver("provider", "driver_entry", code, symbols, imports, exports, relocations)


def build_consumer() -> bytes:
    message = b"consumer.drv driver_entry()\x00"
    message_offset = 37
    code = (
        b"\x48\xBF" + struct.pack("<Q", 0) +
        b"\x48\xB8" + struct.pack("<Q", 0) +
        b"\xFF\xD0" +
        b"\x48\xB8" + struct.pack("<Q", 0) +
        b"\xFF\xD0" +
        b"\x31\xC0" +
        b"\xC3" +
        message
    )
    symbols = [
        ("driver_entry", DRV_SYMBOL_FUNC, 0, 0, 37),
        ("entry_message", DRV_SYMBOL_OBJECT, 0, message_offset, len(message)),
    ]
    imports = [
        ("kernel", "klog", 12),
        ("provider", "provider_ping", 24),
    ]
    relocations = [(2, 1)]
    return pack_driver("consumer", "driver_entry", code, symbols, imports, [], relocations)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--provider", required=True)
    parser.add_argument("--consumer", required=True)
    args = parser.parse_args()

    Path(args.provider).parent.mkdir(parents=True, exist_ok=True)
    Path(args.provider).write_bytes(build_provider())
    Path(args.consumer).write_bytes(build_consumer())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
