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

RESULT_KIND_IDS = {
    "status": 0,
    "count": 1,
    "value": 2,
    "handle": 3,
    "address": 4,
    "noreturn": 5,
}
SUCCESS_DOMAIN_IDS = {"zero": 0, "nonnegative": 1, "positive": 2, "noreturn": 3}
OUTPUT_PUBLICATION_IDS = {"none": 0, "atomic": 1, "partial": 2}
OUTPUT_FAILURE_IDS = {"not_applicable": 0, "unchanged": 1, "partial": 2}


def c_string(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"')

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
    domain = catalog["result_domain"]
    lines = [
        banner("//").rstrip(),
        "#ifndef OS64_RESULT_H",
        "#define OS64_RESULT_H",
        "",
        "#include <stdint.h>",
        "",
        "typedef int64_t OsResult;",
        "",
        "#define OS_SUCCESS INT64_C(0)",
        f"#define OS64_RESULT_SUCCESS_MIN INT64_C({domain['success_min']})",
        f"#define OS64_RESULT_SUCCESS_MAX INT64_C({domain['success_max']})",
        f"#define OS64_RESULT_ERROR_MIN (-INT64_C({-domain['error_min']}))",
        f"#define OS64_RESULT_ERROR_MAX (-INT64_C({-domain['error_max']}))",
        "",
    ]
    for code in catalog["error_codes"]:
        lines.append(f"#define {code['symbol']} (-INT64_C({-code['value']}))")
    lines.append("")
    for code in catalog["error_codes"]:
        lines.append(f"#define {code['kernel_symbol']} {code['symbol']}")
    lines.extend(["", "#define OS64_RESULT_CODE_TABLE(X) \\"])
    for index, code in enumerate(catalog["error_codes"]):
        suffix = " \\" if index + 1 < len(catalog["error_codes"]) else ""
        lines.append(
            f"    X({code['symbol']}, \"{c_string(code['message'])}\"){suffix}"
        )
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
        "#define OS64_SYSCALL_RESULT_STATUS 0u",
        "#define OS64_SYSCALL_RESULT_COUNT 1u",
        "#define OS64_SYSCALL_RESULT_VALUE 2u",
        "#define OS64_SYSCALL_RESULT_HANDLE 3u",
        "#define OS64_SYSCALL_RESULT_ADDRESS 4u",
        "#define OS64_SYSCALL_RESULT_NORETURN 5u",
        "#define OS64_SYSCALL_SUCCESS_ZERO 0u",
        "#define OS64_SYSCALL_SUCCESS_NONNEGATIVE 1u",
        "#define OS64_SYSCALL_SUCCESS_POSITIVE 2u",
        "#define OS64_SYSCALL_SUCCESS_NORETURN 3u",
        "#define OS64_SYSCALL_OUTPUT_NONE 0u",
        "#define OS64_SYSCALL_OUTPUT_ATOMIC 1u",
        "#define OS64_SYSCALL_OUTPUT_PARTIAL 2u",
        "#define OS64_SYSCALL_FAILURE_OUTPUT_NOT_APPLICABLE 0u",
        "#define OS64_SYSCALL_FAILURE_OUTPUT_UNCHANGED 1u",
        "#define OS64_SYSCALL_FAILURE_OUTPUT_PARTIAL 2u",
        "",
        "typedef struct OsSyscallCatalogDescriptor {",
        "    uint32_t number;",
        "    uint8_t argument_count;",
        "    uint8_t pointer_mask;",
        "    uint8_t readable_mask;",
        "    uint8_t writable_mask;",
        "    uint8_t snapshot_mask;",
        "    uint8_t nested_mask;",
        "    uint8_t flags;",
        "    uint8_t result_kind;",
        "    uint8_t success_domain;",
        "    uint8_t output_publication;",
        "    uint8_t failure_output;",
        "    uint16_t argument_alignment[3];",
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
        readable_mask = 0
        writable_mask = 0
        snapshot_mask = 0
        nested_mask = 0
        alignments = [1, 1, 1]
        for index, argument in enumerate(call["arguments"]):
            alignments[index] = argument["alignment"]
            if argument["kind"] in {"cstring", "buffer", "structure"}:
                pointer_mask |= 1 << index
            if argument["access"] in {"read", "read_write"}:
                readable_mask |= 1 << index
            if argument["direction"] in {"out", "inout"}:
                writable_mask |= 1 << index
            if argument["snapshot"] in {
                    "kernel_before_handler",
                    "kernel_before_handler_then_output"}:
                snapshot_mask |= 1 << index
            if argument["nested"] == "snapshot_then_validate":
                nested_mask |= 1 << index
        flags = 0
        if resolved["execution"]["blocking"]:
            flags |= 1
        if resolved["execution"]["cancellation"] != "none":
            flags |= 2
        if call["audit_status"] == "audited":
            flags |= 4
        result = call["result"]
        output = result["output"]
        lines.append(
            f"    {{{call['number']}u, {len(call['arguments'])}u, {pointer_mask}u, "
            f"{readable_mask}u, {writable_mask}u, {snapshot_mask}u, "
            f"{nested_mask}u, {flags}u, {RESULT_KIND_IDS[result['kind']]}u, "
            f"{SUCCESS_DOMAIN_IDS[result['success_domain']]}u, "
            f"{OUTPUT_PUBLICATION_IDS[output['publication']]}u, "
            f"{OUTPUT_FAILURE_IDS[output['failure_state']]}u, "
            f"{{{alignments[0]}u, {alignments[1]}u, {alignments[2]}u}}, "
            f"\"{call['name']}\", \"{call['symbol']}\", "
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
        "| No. | Symbol | Name | Arguments | Result | Output | Errors | Permission | Blocking | Audit | Reference |",
        "| ---: | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |",
    ]
    for call in catalog["syscalls"]:
        resolved = resolve_contract(call, defaults)
        args = ", ".join(
            f"`{arg['register']}:{arg['name']} {arg['direction']} {arg['type']} "
            f"[{arg['size']}; access={arg['access']}; snapshot={arg['snapshot']}; "
            f"align={arg['alignment']}]`"
            for arg in call["arguments"]
        ) or "-"
        lines.append(
            f"| {call['number']} | `{call['symbol']}` | `{call['name']}` | {args} | "
            f"{call['result']['kind']}/{call['result']['success_domain']}: {call['result']['success']} | "
            f"{call['result']['output']['publication']}; failure={call['result']['output']['failure_state']} | "
            f"`{call['result']['errors']}` | `{resolved['permission']}` | "
            f"{'yes' if resolved['execution']['blocking'] else 'no'} | "
            f"{call['audit_status']} | `{call['reference']}` |"
        )
    domain = catalog["result_domain"]
    lines.extend([
        "", "## Result Domain", "",
        f"Public results are signed `{domain['width_bits']}`-bit values. "
        f"Nonnegative values are success; cataloged errors occupy "
        f"`{domain['error_min']}..{domain['error_max']}`. Internal scheduler "
        "control tokens are outside that range and must never reach user mode.",
        "", "## Result Codes", "",
        "| Value | SDK symbol | Kernel alias | Category | Retryable | Meaning |",
        "| ---: | --- | --- | --- | --- | --- |",
    ])
    for code in catalog["error_codes"]:
        lines.append(
            f"| {code['value']} | `{code['symbol']}` | `{code['kernel_symbol']}` | "
            f"`{code['category']}` | {'yes' if code['retryable'] else 'no'} | "
            f"{code['description']} |"
        )
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
