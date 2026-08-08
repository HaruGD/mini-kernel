#!/usr/bin/env python3
"""Generate deterministic syscall ABI artifacts from config/abi/syscalls.json."""

from __future__ import annotations

import argparse
from pathlib import Path

from syscall_catalog import CATALOG_PATH, ROOT, load_catalog, resolve_contract


OUTPUTS = (
    "include/os64/syscall_numbers.h",
    "user/sdk/include/os64/syscall_numbers.h",
    "include/os64/result.h",
    "user/sdk/include/os64/result.h",
    "include/kernel/syscall_catalog_generated.h",
    "user/include/syscall_numbers.inc",
    "docs/reference/syscall_catalog.generated.md",
)

def banner(comment: str) -> str:
    return f"{comment} Generated from config/abi/syscalls.json. Do not edit.\n"


def numbers_header(catalog: dict) -> str:
    lines = [
        banner("//").rstrip(),
        "#ifndef OS64_SYSCALL_NUMBERS_H",
        "#define OS64_SYSCALL_NUMBERS_H",
        "",
        f"#define OS64_SYSCALL_CATALOG_VERSION {catalog['catalog_version']}u",
        f"#define OS64_SYSCALL_MAX_NUMBER {catalog['abi']['max_number']}u",
        "",
    ]
    for call in catalog["syscalls"]:
        lines.append(f"#define {call['symbol']} {call['number']}")
        if call["sdk_symbol"] != call["symbol"]:
            lines.append(f"#define {call['sdk_symbol']} {call['number']}")
    lines.extend(["", "#endif", ""])
    return "\n".join(lines)


def result_header(catalog: dict) -> str:
    lines = [
        banner("//").rstrip(),
        "#ifndef OS64_RESULT_H",
        "#define OS64_RESULT_H",
        "",
        "typedef enum OsResult {",
        "    OS_SUCCESS = 0,",
    ]
    for index, code in enumerate(catalog["error_codes"]):
        suffix = "," if index + 1 < len(catalog["error_codes"]) else ""
        lines.append(f"    {code['symbol']} = {code['value']}{suffix}")
    lines.extend(["} OsResult;", ""])
    for code in catalog["error_codes"]:
        lines.append(f"#define {code['kernel_symbol']} {code['symbol']}")
    lines.extend([
        "", "int os_result_failed(long result);",
        "const char* os_result_string(long result);", "", "#endif", "",
    ])
    return "\n".join(lines)


def nasm_numbers(catalog: dict) -> str:
    lines = [banner(";").rstrip(), "%ifndef OS64_SYSCALL_NUMBERS_INC", "%define OS64_SYSCALL_NUMBERS_INC", ""]
    for call in catalog["syscalls"]:
        lines.append(f"%define {call['symbol']} {call['number']}")
    lines.extend(["", "%endif", ""])
    return "\n".join(lines)


def descriptor_header(catalog: dict) -> str:
    defaults = catalog["defaults"]
    lines = [
        banner("//").rstrip(),
        "#ifndef KERNEL_SYSCALL_CATALOG_GENERATED_H",
        "#define KERNEL_SYSCALL_CATALOG_GENERATED_H",
        "",
        "#include <stdint.h>",
        "#include \"os64/syscall_numbers.h\"",
        "",
        "#define OS64_SYSCALL_FLAG_BLOCKING (1u << 0)",
        "#define OS64_SYSCALL_FLAG_CANCELLABLE (1u << 1)",
        "#define OS64_SYSCALL_FLAG_AUDITED (1u << 2)",
        "",
        "typedef struct OsSyscallCatalogDescriptor {",
        "    uint32_t number;",
        "    uint8_t argument_count;",
        "    uint8_t pointer_mask;",
        "    uint8_t writable_mask;",
        "    uint8_t flags;",
        "    const char* name;",
        "    const char* symbol;",
        "    const char* permission;",
        "    const char* error_set;",
        "} OsSyscallCatalogDescriptor;",
        "",
        "static const OsSyscallCatalogDescriptor os64_syscall_catalog[] = {",
    ]
    for call in catalog["syscalls"]:
        resolved = resolve_contract(call, defaults)
        pointer_mask = 0
        writable_mask = 0
        for index, argument in enumerate(call["arguments"]):
            if argument["kind"] in {"cstring", "buffer", "structure"}:
                pointer_mask |= 1 << index
            if argument["direction"] in {"out", "inout"}:
                writable_mask |= 1 << index
        flags = 0
        if resolved["execution"]["blocking"]:
            flags |= 1
        if resolved["execution"]["cancellation"] != "none":
            flags |= 2
        if call["audit_status"] == "audited":
            flags |= 4
        lines.append(
            f"    {{{call['number']}u, {len(call['arguments'])}u, {pointer_mask}u, "
            f"{writable_mask}u, {flags}u, \"{call['name']}\", \"{call['symbol']}\", "
            f"\"{resolved['permission']}\", \"{call['result']['errors']}\"}},"
        )
    lines.extend([
        "};",
        "",
        "#define OS64_SYSCALL_CATALOG_COUNT \\",
        "    ((uint32_t)(sizeof(os64_syscall_catalog) / sizeof(os64_syscall_catalog[0])))",
        "",
        "#endif",
        "",
    ])
    return "\n".join(lines)


def reference_markdown(catalog: dict) -> str:
    defaults = catalog["defaults"]
    lines = [
        "<!-- Generated from config/abi/syscalls.json. Do not edit. -->",
        "# Generated System Call Catalog",
        "",
        f"Catalog version: `{catalog['catalog_version']}`. Active transport: "
        f"`{catalog['abi']['active_transport']}`. Compatibility transport: "
        f"`{catalog['abi']['compatibility_transport']}`.",
        "",
        "The catalog is authoritative for identifiers and declared contract metadata. "
        "Rows marked `provisional` still require the Phase 5S-G implementation audit.",
        "",
        "| No. | Symbol | Name | Arguments | Result | Errors | Permission | Blocking | Audit | Reference |",
        "| ---: | --- | --- | --- | --- | --- | --- | --- | --- | --- |",
    ]
    for call in catalog["syscalls"]:
        resolved = resolve_contract(call, defaults)
        args = ", ".join(
            f"`{arg['register']}:{arg['name']} {arg['direction']} {arg['type']} [{arg['size']}]`"
            for arg in call["arguments"]
        ) or "-"
        lines.append(
            f"| {call['number']} | `{call['symbol']}` | `{call['name']}` | {args} | "
            f"{call['result']['kind']}: {call['result']['success']} | "
            f"`{call['result']['errors']}` | `{resolved['permission']}` | "
            f"{'yes' if resolved['execution']['blocking'] else 'no'} | "
            f"{call['audit_status']} | `{call['reference']}` |"
        )
    lines.extend(["", "## Result Codes", "", "| Value | SDK symbol | Kernel alias | Meaning |", "| ---: | --- | --- | --- |"])
    for code in catalog["error_codes"]:
        lines.append(f"| {code['value']} | `{code['symbol']}` | `{code['kernel_symbol']}` | {code['description']} |")
    lines.append("")
    return "\n".join(lines)


def generated_outputs(catalog: dict) -> dict[str, str]:
    numbers = numbers_header(catalog)
    results = result_header(catalog)
    return {
        "include/os64/syscall_numbers.h": numbers,
        "user/sdk/include/os64/syscall_numbers.h": numbers,
        "include/os64/result.h": results,
        "user/sdk/include/os64/result.h": results,
        "include/kernel/syscall_catalog_generated.h": descriptor_header(catalog),
        "user/include/syscall_numbers.inc": nasm_numbers(catalog),
        "docs/reference/syscall_catalog.generated.md": reference_markdown(catalog),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--catalog", type=Path, default=CATALOG_PATH)
    parser.add_argument("--root", type=Path, default=ROOT)
    args = parser.parse_args()
    root = args.root.resolve()
    catalog = load_catalog(args.catalog.resolve(), root)
    outputs = generated_outputs(catalog)
    stale: list[str] = []
    for relative, content in outputs.items():
        path = root / relative
        if args.check:
            if not path.is_file() or path.read_text(encoding="utf-8") != content:
                stale.append(relative)
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="utf-8")
    if stale:
        print("stale syscall generated artifacts:")
        for path in stale:
            print(f"- {path}")
        return 1
    action = "checked" if args.check else "generated"
    print(f"syscall catalog {action}: {len(catalog['syscalls'])} calls, {len(outputs)} artifacts")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
