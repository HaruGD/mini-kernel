#!/usr/bin/env python3
"""Regression gate for the Phase 5S-A/B/C syscall catalog and generated ABI."""

from __future__ import annotations

import copy
import json
import re
import subprocess
import tempfile
from pathlib import Path

from gen_syscall_catalog import generated_outputs
from syscall_catalog import (
    CATALOG_PATH,
    ROOT,
    SCHEMA_PATH,
    DuplicateKeyError,
    load_catalog,
    load_json,
    validate_catalog,
)


def require(condition: bool, message: str, failures: list[str]) -> None:
    if not condition:
        failures.append(message)


def compile_header_view(include_dir: Path, cxx: bool, source: str,
                        temp: Path) -> None:
    suffix = "cpp" if cxx else "c"
    path = temp / f"syscall_contract_{include_dir.parent.name}_{suffix}.{suffix}"
    path.write_text(source, encoding="utf-8")
    subprocess.run([
        "g++" if cxx else "gcc",
        "-std=c++17" if cxx else "-std=c11",
        "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
        "-I", str(include_dir), str(path),
    ], cwd=ROOT, check=True)


def generated_contract(catalog: dict, failures: list[str]) -> None:
    expected = generated_outputs(catalog)
    for relative, content in expected.items():
        path = ROOT / relative
        require(path.is_file(), f"missing generated artifact: {relative}", failures)
        if path.is_file():
            require(path.read_text(encoding="utf-8") == content,
                    f"stale generated artifact: {relative}", failures)
    require(expected == generated_outputs(catalog),
            "syscall generation is nondeterministic", failures)
    process = subprocess.run(
        ["python3", "tools/gen_syscall_catalog.py", "--check"],
        cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    require(process.returncode == 0,
            f"generator --check failed:\n{process.stdout}", failures)


def mutation_contract(catalog: dict, failures: list[str]) -> None:
    duplicate = copy.deepcopy(catalog)
    duplicate["syscalls"][1]["number"] = duplicate["syscalls"][0]["number"]
    require(any("duplicate" in error for error in validate_catalog(duplicate)),
            "duplicate syscall number was accepted", failures)

    incomplete = copy.deepcopy(catalog)
    del incomplete["syscalls"][0]["arguments"][0]["size"]
    require(any("missing field 'size'" in error
                for error in validate_catalog(incomplete)),
            "incomplete pointer contract was accepted", failures)

    incomplete_memory = copy.deepcopy(catalog)
    del incomplete_memory["syscalls"][0]["arguments"][0]["snapshot"]
    require(any("missing field 'snapshot'" in error
                for error in validate_catalog(incomplete_memory)),
            "incomplete user-memory contract was accepted", failures)

    invalid_memory = copy.deepcopy(catalog)
    invalid_memory["syscalls"][0]["arguments"][0]["access"] = "write"
    require(any("input pointer must be read" in error
                for error in validate_catalog(invalid_memory)),
            "input pointer with write-only access was accepted", failures)

    invalid_alignment = copy.deepcopy(catalog)
    invalid_alignment["syscalls"][0]["arguments"][0]["alignment"] = 3
    require(any("power of two" in error
                for error in validate_catalog(invalid_alignment)),
            "non-power-of-two pointer alignment was accepted", failures)

    unknown_errors = copy.deepcopy(catalog)
    unknown_errors["syscalls"][0]["result"]["errors"] = "not_declared"
    require(any("unknown error set" in error
                for error in validate_catalog(unknown_errors)),
            "unknown result error set was accepted", failures)

    incomplete_result = copy.deepcopy(catalog)
    del incomplete_result["syscalls"][0]["result"]["success_domain"]
    require(any("missing field 'success_domain'" in error
                for error in validate_catalog(incomplete_result)),
            "incomplete success domain was accepted", failures)

    invalid_output = copy.deepcopy(catalog)
    output_call = next(call for call in invalid_output["syscalls"]
                       if any(arg["direction"] == "out"
                              for arg in call["arguments"]))
    output_call["result"]["output"]["publication"] = "none"
    require(any("output pointer requires" in error
                for error in validate_catalog(invalid_output)),
            "output pointer without publication contract was accepted", failures)

    invalid_partial = copy.deepcopy(catalog)
    query = next(call for call in invalid_partial["syscalls"]
                 if call["name"] == "ipc_query")
    query["result"]["output"]["partial_errors"] = []
    require(any("partial output requires" in error
                for error in validate_catalog(invalid_partial)),
            "partial output without named errors was accepted", failures)

    sparse_errors = copy.deepcopy(catalog)
    sparse_errors["error_codes"][-1]["value"] = -23
    require(any("contiguous" in error
                for error in validate_catalog(sparse_errors)),
            "noncontiguous public result codes were accepted", failures)

    with tempfile.TemporaryDirectory(prefix="os64_syscall_json_") as directory:
        path = Path(directory) / "duplicate.json"
        path.write_text('{"schema_version":1,"schema_version":1}\n',
                        encoding="utf-8")
        try:
            load_json(path)
        except DuplicateKeyError:
            pass
        else:
            failures.append("duplicate JSON key was accepted")


def source_of_truth_contract(catalog: dict, failures: list[str]) -> None:
    kernel_header = (ROOT / "include/kernel/syscall64.h").read_text(encoding="utf-8")
    sdk_internal = (ROOT / "user/sdk/src/internal.h").read_text(encoding="utf-8")
    nasm_wrapper = (ROOT / "user/include/syscall.inc").read_text(encoding="utf-8")
    require('"os64/syscall_numbers.h"' in kernel_header and
            '"os64/result.h"' in kernel_header,
            "kernel syscall header does not consume generated ABI headers", failures)
    require('"os64/syscall_numbers.h"' in sdk_internal,
            "SDK internal boundary does not consume generated syscall numbers", failures)
    require('syscall_numbers.inc' in nasm_wrapper,
            "NASM wrappers do not consume generated syscall numbers", failures)
    require(re.search(r"^#define SYS_[A-Z0-9_]+\s+\d", kernel_header,
                      re.MULTILINE) is None,
            "manual numeric syscall define remains in kernel header", failures)
    require(re.search(r"OS_SYS_[A-Z0-9_]+\s*=\s*\d", sdk_internal) is None,
            "manual numeric syscall enum remains in SDK internal header", failures)
    require(re.search(r"^%define SYS_[A-Z0-9_]+\s+\d", nasm_wrapper,
                      re.MULTILINE) is None,
            "manual numeric syscall define remains in NASM wrapper", failures)

    dispatch_text = "\n".join(
        path.read_text(encoding="utf-8")
        for path in sorted((ROOT / "kernel/syscall").glob("*.cpp"))
    )
    symbols = {call["symbol"] for call in catalog["syscalls"]
               if call["state"] == "active"}
    for symbol in sorted(symbols):
        require(re.search(rf"\b{re.escape(symbol)}\b", dispatch_text) is not None,
                f"active syscall has no dispatcher reference: {symbol}", failures)
    dispatch_symbols = set(re.findall(r"\bSYS_[A-Z][A-Z0-9_]*\b", dispatch_text))
    unexpected = sorted(symbol for symbol in dispatch_symbols
                        if not symbol.startswith("SYS_ERR_") and symbol not in symbols)
    require(not unexpected,
            f"dispatcher uses uncataloged syscall symbols: {unexpected}", failures)

    sdk_text = "\n".join(
        path.read_text(encoding="utf-8")
        for path in sorted((ROOT / "user/sdk/src").glob("*.[ch]"))
    )
    sdk_symbols = {call["sdk_symbol"] for call in catalog["syscalls"]}
    used_sdk_symbols = set(re.findall(r"\bOS_SYS_[A-Z][A-Z0-9_]*\b", sdk_text))
    require(used_sdk_symbols <= sdk_symbols,
            f"SDK uses uncataloged syscall aliases: {sorted(used_sdk_symbols - sdk_symbols)}",
            failures)

    user_sources = "\n".join(
        path.read_text(encoding="utf-8")
        for path in sorted((ROOT / "user").rglob("*"))
        if path.suffix in {".c", ".h", ".asm", ".easm"}
    )
    require(re.search(r"\b(?:os|user)_syscall[0-3]\s*\(\s*\d+", user_sources) is None,
            "user source contains a raw numeric syscall invocation", failures)
    require(re.search(r"mov\s+(?:e|r)ax\s*,\s*\d+\s*\n\s*int\s+0x80",
                      user_sources) is None,
            "user assembly contains a raw numeric syscall invocation", failures)
    require('"mov $91, %rax' not in user_sources,
            "thread trampoline contains a raw numeric syscall invocation", failures)

    result_source = (ROOT / "user/sdk/src/result.c").read_text(encoding="utf-8")
    require("OS64_RESULT_CODE_TABLE(OS64_RESULT_STRING_CASE)" in result_source,
            "SDK result strings do not consume the generated code table", failures)
    require(re.search(r"case\s+OS_ERR_", result_source) is None,
            "SDK result strings still contain a manual error-code switch", failures)
    syscall_sources = dispatch_text
    require(re.search(r"\(uint64_t\)-\d+", syscall_sources) is None,
            "syscall dispatcher contains an ambiguous raw negative result", failures)
    require("SYS_ERR_UNSUPPORTED" in
            (ROOT / "kernel/syscall/syscall64.cpp").read_text(encoding="utf-8"),
            "unknown syscall path does not use the cataloged unsupported result", failures)
    kernel_syscall_header = (ROOT / "include/kernel/syscall64.h").read_text(
        encoding="utf-8")
    for token in ("SYSCALL_RETURN_TO_KERNEL", "SYSCALL_YIELD_TO_KERNEL",
                  "SYSCALL_SLEEP_TO_KERNEL", "SYSCALL_WAIT_TO_KERNEL"):
        match = re.search(rf"#define\s+{token}\s+0x([0-9A-Fa-f]+)ULL",
                          kernel_syscall_header)
        require(match is not None, f"missing internal control token {token}", failures)
        if match is not None:
            unsigned = int(match.group(1), 16)
            signed = unsigned - (1 << 64) if unsigned >= (1 << 63) else unsigned
            require(signed < catalog["result_domain"]["error_min"],
                    f"internal control token overlaps public result range: {token}",
                    failures)

    for code in catalog["error_codes"]:
        require(code["symbol"] in
                (ROOT / "user/sdk/include/os64/result.h").read_text(encoding="utf-8"),
                f"generated result table missing {code['symbol']}", failures)
    result_header = (ROOT / "user/sdk/include/os64/result.h").read_text(
        encoding="utf-8")
    require("int os_result_failed(long result);" in result_header and
            "const char* os_result_string(long result);" in result_header,
            "generated result header lost public SDK helpers", failures)


def compile_contract(catalog: dict, failures: list[str]) -> None:
    first = catalog["syscalls"][0]
    last = catalog["syscalls"][-1]
    c_source = f'''#include "os64/result.h"
#include "os64/syscall_numbers.h"
_Static_assert(OS64_SYSCALL_CATALOG_VERSION == {catalog['catalog_version']}u, "catalog version");
_Static_assert(OS64_SYSCALL_MAX_NUMBER == {catalog['abi']['max_number']}u, "maximum number");
_Static_assert({first['symbol']} == {first['number']}u, "first syscall");
_Static_assert({last['symbol']} == {last['number']}u, "last syscall");
_Static_assert(sizeof(OsResult) == 8u, "signed result width");
_Static_assert(OS_ERR_CANCELLED == -18, "existing result ABI");
_Static_assert(OS_ERR_OVERFLOW == -22, "extended result ABI");
_Static_assert(OS64_RESULT_ERROR_MIN == -4095, "reserved result range");
int main(void) {{ return 0; }}
'''
    cpp_source = c_source.replace("_Static_assert", "static_assert")
    descriptor_source = f'''#include "kernel/syscall_catalog_generated.h"
_Static_assert(OS64_SYSCALL_CATALOG_COUNT == {len(catalog['syscalls'])}u, "descriptor count");
_Static_assert(OS64_SYSCALL_OUTPUT_PARTIAL == 2u, "output policy ABI");
int main(void) {{ return os64_syscall_catalog[0].number == 1u ? 0 : 1; }}
'''
    with tempfile.TemporaryDirectory(prefix="os64_syscall_headers_") as directory:
        temp = Path(directory)
        compile_header_view(ROOT / "include", False, c_source, temp)
        compile_header_view(ROOT / "include", True, cpp_source, temp)
        compile_header_view(ROOT / "user/sdk/include", False, c_source, temp)
        compile_header_view(ROOT / "user/sdk/include", True, cpp_source, temp)
        compile_header_view(ROOT / "include", False, descriptor_source, temp)

        nasm_source = temp / "numbers.asm"
        nasm_source.write_text(
            f'''BITS 64
%include "user/include/syscall_numbers.inc"
%if {first['symbol']} != {first['number']}
%error first syscall drifted
%endif
%if {last['symbol']} != {last['number']}
%error last syscall drifted
%endif
ret
''', encoding="utf-8")
        subprocess.run(["nasm", "-f", "bin", "-I", str(ROOT) + "/",
                        "-o", str(temp / "numbers.bin"), str(nasm_source)],
                       cwd=ROOT, check=True)


def result_runtime_contract(catalog: dict, failures: list[str]) -> None:
    checks = "\n".join(
        f'    if (strcmp(os_result_string({code["symbol"]}), '
        f'"{code["message"]}") != 0) return {index + 10};'
        for index, code in enumerate(catalog["error_codes"])
    )
    source = f'''#include <string.h>
#include "os64/result.h"
int main(void) {{
    if (os_result_failed(OS_SUCCESS) || os_result_failed(17)) return 1;
    if (!os_result_failed(OS_ERR_NOT_READY)) return 2;
    if (strcmp(os_result_string(OS_SUCCESS), "success") != 0) return 3;
    if (strcmp(os_result_string(17), "success") != 0) return 4;
    if (strcmp(os_result_string(OS64_RESULT_ERROR_MIN), "unknown error") != 0) return 5;
{checks}
    return 0;
}}
'''
    with tempfile.TemporaryDirectory(prefix="os64_result_runtime_") as directory:
        temp = Path(directory)
        source_path = temp / "result_runtime.c"
        binary_path = temp / "result_runtime"
        source_path.write_text(source, encoding="utf-8")
        process = subprocess.run([
            "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
            "-I", str(ROOT / "user/sdk/include"),
            str(ROOT / "user/sdk/src/result.c"), str(source_path),
            "-o", str(binary_path),
        ], cwd=ROOT, text=True, stdout=subprocess.PIPE,
           stderr=subprocess.STDOUT)
        require(process.returncode == 0,
                f"SDK result runtime compile failed:\n{process.stdout}", failures)
        if process.returncode == 0:
            run = subprocess.run([str(binary_path)], text=True,
                                 stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT)
            require(run.returncode == 0,
                    f"SDK result runtime failed with {run.returncode}:\n{run.stdout}",
                    failures)


def documentation_contract(catalog: dict, failures: list[str]) -> None:
    schema = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))
    require(schema.get("$schema") == "https://json-schema.org/draft/2020-12/schema",
            "syscall schema is not Draft 2020-12", failures)
    require(schema.get("additionalProperties") is False,
            "syscall schema does not reject unknown root fields", failures)
    require(schema.get("properties", {}).get("schema_version", {}).get("const") == 3,
            "syscall schema version did not advance for user-memory contracts", failures)
    reference = (ROOT / "docs/reference/syscall_catalog.generated.md").read_text(
        encoding="utf-8")
    rows = re.findall(r"^\| \d+ \| `SYS_", reference, re.MULTILINE)
    require(len(rows) == len(catalog["syscalls"]),
            "generated syscall reference row count drifted", failures)
    plan = (ROOT / "docs/phases/phase-5/syscall_modernization_plan.md").read_text(
        encoding="utf-8")
    progress = (ROOT / "docs/phases/phase-5/progress.md").read_text(encoding="utf-8")
    require("config/abi/syscalls.json" in plan and "5S-A" in plan,
            "Phase 5S plan does not own the syscall catalog", failures)
    require("P5S-R01" in progress,
            "Phase 5 progress does not track the syscall contract gate", failures)
    result_contract = ROOT / "docs/reference/syscall_result_contract.md"
    require(result_contract.is_file() and
            "signed 64-bit" in result_contract.read_text(encoding="utf-8"),
            "result/output contract reference is missing", failures)
    memory_contract = ROOT / "docs/reference/syscall_user_memory.md"
    require(memory_contract.is_file() and
            "UserMemoryLease" in memory_contract.read_text(encoding="utf-8"),
            "user-memory boundary reference is missing", failures)


def main() -> int:
    failures: list[str] = []
    try:
        catalog = load_catalog(CATALOG_PATH, ROOT)
    except Exception as error:
        print(error)
        return 1
    require(len(catalog["syscalls"]) == 111,
            "expected complete current syscall number coverage 1..111", failures)
    generated_contract(catalog, failures)
    mutation_contract(catalog, failures)
    source_of_truth_contract(catalog, failures)
    compile_contract(catalog, failures)
    result_runtime_contract(catalog, failures)
    documentation_contract(catalog, failures)
    if failures:
        print("syscall contract test failed:")
        for failure in failures:
            print(f"- {failure}")
        return 1
    pointer_calls = sum(
        any(arg["kind"] in {"cstring", "buffer", "structure"}
            for arg in call["arguments"])
        for call in catalog["syscalls"]
    )
    print("syscall contract test OK")
    print(f"catalog_version={catalog['catalog_version']} calls={len(catalog['syscalls'])} "
          f"error_codes={len(catalog['error_codes'])} pointer_calls={pointer_calls} "
          f"atomic_outputs={sum(call['result']['output']['publication'] == 'atomic' for call in catalog['syscalls'])} "
          f"partial_outputs={sum(call['result']['output']['publication'] == 'partial' for call in catalog['syscalls'])}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
