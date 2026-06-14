#!/usr/bin/env python3
import argparse
import json
import struct
from dataclasses import dataclass
from pathlib import Path


DRV_MAGIC = 0x5652443436534F
DRV_FORMAT_VERSION = 1
DRV_ABI_VERSION = 1
DRV_ARCH_X86_64 = 0x8664

DRV_SECTION_CODE = 1
DRV_SECTION_RODATA = 2
DRV_SECTION_DATA = 3
DRV_SECTION_BSS = 4
DRV_SYMBOL_FUNC = 1
DRV_SYMBOL_OBJECT = 2
DRV_RELOC_ABS64 = 1
DRV_RELOC_REL32 = 2
DRV_BOOT_NORMAL = 0x00000001
DRV_BOOT_SAFE = 0x00000002
DRV_BOOT_RECOVERY = 0x00000004
DRV_PERMISSION_PCI = 0x00000001
DRV_PERMISSION_MMIO = 0x00000002
DRV_PERMISSION_INTERRUPT = 0x00000004
DRV_PERMISSION_BLOCK = 0x00000008
DRV_PERMISSION_VFS = 0x00000010
DRV_PERMISSION_INPUT = 0x00000020
DRV_PERMISSION_TIMER = 0x00000040

SHT_NULL = 0
SHT_PROGBITS = 1
SHT_SYMTAB = 2
SHT_STRTAB = 3
SHT_RELA = 4
SHT_NOBITS = 8

SHF_WRITE = 0x1
SHF_ALLOC = 0x2
SHF_EXECINSTR = 0x4

STB_LOCAL = 0
STT_OBJECT = 1
STT_FUNC = 2
STT_SECTION = 3
SHN_UNDEF = 0

R_X86_64_64 = 1
R_X86_64_PC32 = 2
R_X86_64_PLT32 = 4

HEADER_FORMAT = "<QIIIIQQQQIIQIIQIIQIIQIIQQQQ"
MANIFEST_FORMAT = "<32s16s32sIIII"
SECTION_FORMAT = "<16sIIQQQQ"
SYMBOL_FORMAT = "<48sIIQQ"
IMPORT_FORMAT = "<32s48sIIQ"
EXPORT_FORMAT = "<48sIIQ"
RELOCATION_FORMAT = "<IIQQq"

PERMISSIONS = {
    "PCI": DRV_PERMISSION_PCI,
    "MMIO": DRV_PERMISSION_MMIO,
    "INTERRUPT": DRV_PERMISSION_INTERRUPT,
    "BLOCK": DRV_PERMISSION_BLOCK,
    "VFS": DRV_PERMISSION_VFS,
    "INPUT": DRV_PERMISSION_INPUT,
    "TIMER": DRV_PERMISSION_TIMER,
}
BOOT_MODES = {
    "NORMAL": DRV_BOOT_NORMAL,
    "SAFE": DRV_BOOT_SAFE,
    "RECOVERY": DRV_BOOT_RECOVERY,
}


@dataclass
class ElfSection:
    name: str
    sh_type: int
    flags: int
    offset: int
    size: int
    link: int
    info: int
    align: int
    entsize: int


@dataclass
class ElfSymbol:
    name: str
    bind: int
    typ: int
    shndx: int
    value: int
    size: int


@dataclass
class DrvSectionOut:
    name: str
    kind: int
    data: bytes
    memory_size: int
    alignment: int
    elf_index: int


@dataclass
class RelocOut:
    typ: int
    section_index: int
    offset: int
    symbol_index: int
    addend: int


@dataclass
class ImportOut:
    module: str
    name: str
    required_permission: int
    section_index: int
    patch_offset: int


def zstr(text: str, size: int) -> bytes:
    data = text.encode("ascii")
    if len(data) >= size:
        raise SystemExit(f"{text}: string is too long for {size} bytes")
    return data + bytes(size - len(data))


def read_cstr(data: bytes, offset: int) -> str:
    end = offset
    while end < len(data) and data[end] != 0:
        end += 1
    return data[offset:end].decode("utf-8")


def align_up(value: int, alignment: int) -> int:
    if alignment <= 1:
        return value
    return (value + alignment - 1) & ~(alignment - 1)


def section_kind(section: ElfSection) -> int:
    if section.flags & SHF_EXECINSTR:
        return DRV_SECTION_CODE
    if section.sh_type == SHT_NOBITS:
        return DRV_SECTION_BSS
    if section.flags & SHF_WRITE:
        return DRV_SECTION_DATA
    return DRV_SECTION_RODATA


def parse_elf(path: Path) -> tuple[bytes, list[ElfSection], list[ElfSymbol], int]:
    data = path.read_bytes()
    if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        raise SystemExit(f"{path}: expected little-endian ELF64 object")
    if struct.unpack_from("<H", data, 16)[0] != 1:
        raise SystemExit(f"{path}: expected relocatable object")
    if struct.unpack_from("<H", data, 18)[0] != 0x3E:
        raise SystemExit(f"{path}: expected x86-64 object")

    e_shoff = struct.unpack_from("<Q", data, 40)[0]
    e_shentsize = struct.unpack_from("<H", data, 58)[0]
    e_shnum = struct.unpack_from("<H", data, 60)[0]
    e_shstrndx = struct.unpack_from("<H", data, 62)[0]

    raw_sections = []
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        raw_sections.append(struct.unpack_from("<IIQQQQIIQQ", data, off))

    shstr = raw_sections[e_shstrndx]
    shstr_data = data[shstr[4]:shstr[4] + shstr[5]]
    sections: list[ElfSection] = []
    for raw in raw_sections:
        name = read_cstr(shstr_data, raw[0])
        sections.append(ElfSection(name, raw[1], raw[2], raw[4], raw[5], raw[6], raw[7], raw[8], raw[9]))

    symbols: list[ElfSymbol] = []
    symtab_index = -1
    for index, section in enumerate(sections):
        if section.sh_type != SHT_SYMTAB:
            continue
        symtab_index = index
        strtab = sections[section.link]
        strtab_data = data[strtab.offset:strtab.offset + strtab.size]
        count = section.size // section.entsize
        for i in range(count):
            off = section.offset + i * section.entsize
            st_name, st_info, _st_other, st_shndx, st_value, st_size = struct.unpack_from("<IBBHQQ", data, off)
            symbols.append(ElfSymbol(read_cstr(strtab_data, st_name), st_info >> 4, st_info & 0x0F, st_shndx, st_value, st_size))
        break

    if symtab_index < 0:
        raise SystemExit(f"{path}: no symbol table found")
    return data, sections, symbols, symtab_index


def parse_import_symbol(name: str) -> tuple[str, str]:
    if name == "klog":
        return "kernel", "klog"
    if name == "kmalloc":
        return "kernel", "kmalloc"
    if name == "kfree":
        return "kernel", "kfree"
    if "__" in name:
        module, imported = name.split("__", 1)
        return module, imported
    raise SystemExit(f"{name}: unresolved symbol has no import mapping")


def validate_ascii_name(value: str, field_name: str, limit: int) -> None:
    if not isinstance(value, str) or value == "":
        raise SystemExit(f"{field_name}: expected non-empty string")
    try:
        encoded = value.encode("ascii")
    except UnicodeEncodeError as exc:
        raise SystemExit(f"{field_name}: expected ASCII") from exc
    if len(encoded) >= limit:
        raise SystemExit(f"{field_name}: value is too long")


def parse_flag_list(value, table: dict[str, int], field_name: str) -> int:
    if value is None:
        return 0
    if isinstance(value, int):
        return value
    if not isinstance(value, list):
        raise SystemExit(f"{field_name}: expected integer or string list")

    flags = 0
    for item in value:
        if not isinstance(item, str):
            raise SystemExit(f"{field_name}: expected string values")
        key = item.upper()
        if key not in table:
            raise SystemExit(f"{field_name}: unknown flag {item}")
        flags |= table[key]
    return flags


def parse_export_specs(value) -> dict[str, int]:
    if value is None:
        return {}
    if not isinstance(value, list):
        raise SystemExit("exports: expected list")

    exports: dict[str, int] = {}
    for item in value:
        if isinstance(item, str):
            validate_ascii_name(item, "exports.name", 48)
            exports[item] = 0
            continue
        if isinstance(item, dict):
            name = item.get("name")
            validate_ascii_name(name, "exports.name", 48)
            exports[name] = parse_flag_list(item.get("required_permissions", []), PERMISSIONS, "exports.required_permissions")
            continue
        raise SystemExit("exports: expected string or object")
    return exports


def parse_import_permissions(value) -> dict[str, int]:
    if value is None:
        return {}
    if not isinstance(value, dict):
        raise SystemExit("imports: expected object")

    imports: dict[str, int] = {}
    for key, permissions in value.items():
        validate_ascii_name(key, "imports.key", 80)
        if "." not in key:
            raise SystemExit(f"imports.{key}: expected module.name")
        imports[key] = parse_flag_list(permissions, PERMISSIONS, f"imports.{key}")
    return imports


def load_manifest(args: argparse.Namespace) -> None:
    if args.manifest is None:
        return

    path = Path(args.manifest)
    manifest = json.loads(path.read_text())
    if not isinstance(manifest, dict):
        raise SystemExit(f"{path}: manifest root must be an object")

    args.name = manifest.get("name", args.name)
    args.version = manifest.get("version", args.version)
    args.entry = manifest.get("entry", args.entry)
    args.permissions = parse_flag_list(manifest.get("permissions", args.permissions), PERMISSIONS, "permissions")
    args.boot_modes = parse_flag_list(manifest.get("boot_modes", args.boot_modes), BOOT_MODES, "boot_modes")
    args.export_specs = parse_export_specs(manifest.get("exports", args.export or []))
    args.import_permissions = parse_import_permissions(manifest.get("imports", {}))


def normalized_reloc_addend(reloc_type: int, addend: int) -> int:
    if reloc_type == DRV_RELOC_REL32:
        return addend + 4
    return addend


def build_drv(args: argparse.Namespace) -> bytes:
    elf_data, elf_sections, elf_symbols, symtab_index = parse_elf(Path(args.object))
    validate_ascii_name(args.name, "name", 32)
    validate_ascii_name(args.version, "version", 16)
    validate_ascii_name(args.entry, "entry", 32)
    selected: list[DrvSectionOut] = []
    elf_to_drv: dict[int, int] = {}

    for index, section in enumerate(elf_sections):
        if (section.flags & SHF_ALLOC) == 0:
            continue
        if section.sh_type not in (SHT_PROGBITS, SHT_NOBITS):
            continue
        kind = section_kind(section)
        data = b"" if section.sh_type == SHT_NOBITS else elf_data[section.offset:section.offset + section.size]
        elf_to_drv[index] = len(selected)
        selected.append(DrvSectionOut(section.name, kind, data, section.size, max(section.align, 1), index))

    if not selected:
        raise SystemExit(f"{args.object}: no allocatable sections found")

    drv_symbols = []
    elf_symbol_to_drv_symbol: dict[int, int] = {}
    for index, symbol in enumerate(elf_symbols):
        if symbol.shndx == SHN_UNDEF:
            continue
        if symbol.shndx not in elf_to_drv:
            continue
        if symbol.typ == STT_SECTION:
            symbol_name = f"section_{elf_to_drv[symbol.shndx]}"
            kind = DRV_SYMBOL_OBJECT
        elif symbol.name != "" and symbol.typ in (STT_FUNC, STT_OBJECT):
            symbol_name = symbol.name
            kind = DRV_SYMBOL_FUNC if symbol.typ == STT_FUNC else DRV_SYMBOL_OBJECT
        else:
            continue
        drv_symbol_index = len(drv_symbols)
        elf_symbol_to_drv_symbol[index] = drv_symbol_index
        drv_symbols.append((symbol_name, kind, elf_to_drv[symbol.shndx], symbol.value, symbol.size))

    imports: list[ImportOut] = []
    import_slot_section_index = -1
    import_slot_data = bytearray()
    import_slot_symbols: dict[int, int] = {}
    relocations: list[RelocOut] = []

    def ensure_import_slot(sym_index: int) -> int:
        nonlocal import_slot_section_index

        if sym_index in import_slot_symbols:
            return import_slot_symbols[sym_index]

        symbol = elf_symbols[sym_index]
        module, import_name = parse_import_symbol(symbol.name)
        required_permission = args.import_permissions.get(f"{module}.{import_name}", 0)
        if import_slot_section_index < 0:
            import_slot_section_index = len(selected)
            selected.append(DrvSectionOut(".import", DRV_SECTION_DATA, b"", 0, 8, -1))

        slot_offset = align_up(len(import_slot_data), 8)
        while len(import_slot_data) < slot_offset:
            import_slot_data.append(0)
        import_slot_data.extend(bytes(8))

        slot_symbol_index = len(drv_symbols)
        import_slot_symbols[sym_index] = slot_symbol_index
        drv_symbols.append((f"import_{module}__{import_name}", DRV_SYMBOL_OBJECT, import_slot_section_index, slot_offset, 8))
        imports.append(ImportOut(module, import_name, required_permission, import_slot_section_index, slot_offset))
        return slot_symbol_index

    for section in elf_sections:
        if section.sh_type != SHT_RELA or section.link != symtab_index:
            continue
        if section.info not in elf_to_drv:
            continue
        target_section_index = elf_to_drv[section.info]
        count = section.size // section.entsize
        for i in range(count):
            off = section.offset + i * section.entsize
            r_offset, r_info, r_addend = struct.unpack_from("<QQq", elf_data, off)
            sym_index = r_info >> 32
            r_type = r_info & 0xFFFFFFFF
            if sym_index >= len(elf_symbols):
                raise SystemExit("relocation references invalid symbol")
            symbol = elf_symbols[sym_index]

            if r_type in (R_X86_64_PC32, R_X86_64_PLT32):
                drv_type = DRV_RELOC_REL32
            elif r_type == R_X86_64_64:
                drv_type = DRV_RELOC_ABS64
            else:
                raise SystemExit(f"unsupported relocation type {r_type} for {symbol.name}")

            if symbol.shndx == SHN_UNDEF:
                if r_type == R_X86_64_PLT32:
                    raise SystemExit(f"{symbol.name}: direct imported calls are not supported; use an import pointer")
                slot_symbol_index = ensure_import_slot(sym_index)
                relocations.append(RelocOut(drv_type,
                                            target_section_index,
                                            r_offset,
                                            slot_symbol_index,
                                            normalized_reloc_addend(drv_type, r_addend)))
                continue

            if sym_index not in elf_symbol_to_drv_symbol:
                raise SystemExit(f"{symbol.name}: relocation target is not a packaged symbol")
            relocations.append(RelocOut(drv_type,
                                        target_section_index,
                                        r_offset,
                                        elf_symbol_to_drv_symbol[sym_index],
                                        normalized_reloc_addend(drv_type, r_addend)))

    if import_slot_section_index >= 0:
        selected[import_slot_section_index].data = bytes(import_slot_data)
        selected[import_slot_section_index].memory_size = len(import_slot_data)

    symbols_by_name = {symbol_name for symbol_name, _kind, _section_index, _value, _size in drv_symbols}
    if args.entry not in symbols_by_name:
        raise SystemExit(f"{args.entry}: entry symbol was not found")

    exports = []
    export_specs = args.export_specs
    missing_exports = [name for name in export_specs.keys() if name not in symbols_by_name]
    if missing_exports:
        raise SystemExit(f"missing export symbols: {', '.join(missing_exports)}")
    for symbol_name, kind, section_index, value, _size in drv_symbols:
        if symbol_name in export_specs:
            exports.append((symbol_name, kind, export_specs[symbol_name], value))

    return pack_drv(args, selected, drv_symbols, imports, exports, relocations)


def pack_drv(args: argparse.Namespace,
             sections: list[DrvSectionOut],
             symbols: list[tuple[str, int, int, int, int]],
             imports: list[ImportOut],
             exports: list[tuple[str, int, int, int]],
             relocations: list[RelocOut]) -> bytes:
    header_size = struct.calcsize(HEADER_FORMAT)
    section_entry_size = struct.calcsize(SECTION_FORMAT)
    symbol_entry_size = struct.calcsize(SYMBOL_FORMAT)
    import_entry_size = struct.calcsize(IMPORT_FORMAT)
    export_entry_size = struct.calcsize(EXPORT_FORMAT)
    relocation_entry_size = struct.calcsize(RELOCATION_FORMAT)

    manifest = struct.pack(
        MANIFEST_FORMAT,
        zstr(args.name, 32),
        zstr(args.version, 16),
        zstr(args.entry, 32),
        args.permissions,
        args.boot_modes,
        0,
        0,
    )
    section_table_placeholder = b"".join(
        struct.pack(SECTION_FORMAT, zstr(section.name, 16), section.kind, 0, 0, len(section.data), section.memory_size, section.alignment)
        for section in sections
    )
    symbol_table = b"".join(
        struct.pack(SYMBOL_FORMAT, zstr(name, 48), kind, section_index, value, size)
        for name, kind, section_index, value, size in symbols
    )
    import_table = b"".join(
        struct.pack(IMPORT_FORMAT,
                    zstr(item.module, 32),
                    zstr(item.name, 48),
                    item.required_permission,
                    item.section_index,
                    item.patch_offset)
        for item in imports
    )
    export_table = b"".join(
        struct.pack(EXPORT_FORMAT, zstr(name, 48), kind, flags, address)
        for name, kind, flags, address in exports
    )
    relocation_table = b"".join(
        struct.pack(RELOCATION_FORMAT, item.typ, item.section_index, item.offset, item.symbol_index, item.addend)
        for item in relocations
    )

    manifest_offset = header_size
    section_table_offset = manifest_offset + len(manifest)
    symbol_table_offset = section_table_offset + len(section_table_placeholder)
    import_table_offset = symbol_table_offset + len(symbol_table)
    export_table_offset = import_table_offset + len(import_table)
    relocation_table_offset = export_table_offset + len(export_table)
    data_offset = relocation_table_offset + len(relocation_table)

    section_entries = []
    section_data = bytearray()
    cursor = data_offset
    for section in sections:
        cursor = align_up(cursor, section.alignment)
        while data_offset + len(section_data) < cursor:
            section_data.append(0)
        file_offset = cursor if section.kind != DRV_SECTION_BSS else 0
        if section.kind != DRV_SECTION_BSS:
            section_data.extend(section.data)
            cursor += len(section.data)
        section_entries.append(
            struct.pack(SECTION_FORMAT,
                        zstr(section.name, 16),
                        section.kind,
                        0,
                        file_offset,
                        0 if section.kind == DRV_SECTION_BSS else len(section.data),
                        section.memory_size,
                        section.alignment)
        )

    file_size = data_offset + len(section_data)

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
        len(sections),
        section_entry_size,
        symbol_table_offset,
        len(symbols),
        symbol_entry_size,
        import_table_offset,
        len(imports),
        import_entry_size,
        export_table_offset,
        len(exports),
        export_entry_size,
        relocation_table_offset,
        len(relocations),
        relocation_entry_size,
        0,
        0,
        0,
        0,
    )
    return b"".join([
        header,
        manifest,
        b"".join(section_entries),
        symbol_table,
        import_table,
        export_table,
        relocation_table,
        bytes(section_data),
    ])


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--object", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--manifest")
    parser.add_argument("--name")
    parser.add_argument("--version", default="0.1.0")
    parser.add_argument("--entry", default="driver_entry")
    parser.add_argument("--permissions", type=lambda value: int(value, 0), default=0)
    parser.add_argument("--boot-modes", dest="boot_modes", type=lambda value: int(value, 0), default=DRV_BOOT_NORMAL)
    parser.add_argument("--export", action="append", default=[])
    args = parser.parse_args()
    args.export_specs = parse_export_specs(args.export)
    args.import_permissions = {}
    load_manifest(args)
    if not args.name:
        raise SystemExit("--name or manifest name is required")

    image = build_drv(args)
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(image)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
