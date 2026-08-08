#!/usr/bin/env python3
"""Regression gate for the Phase 5S-A system-call catalog."""

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

    unknown_errors = copy.deepcopy(catalog)
    unknown_errors["syscalls"][0]["result"]["errors"] = "not_declared"
    require(any("unknown error set" in error
                for error in validate_catalog(unknown_errors)),
            "unknown result error set was accepted", failures)

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
    for code in catalog["error_codes"]:
        require(code["symbol"] in result_source,
                f"SDK result string missing {code['symbol']}", failures)
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
_Static_assert(OS_ERR_CANCELLED == -18, "result ABI");
int main(void) {{ return 0; }}
'''
    cpp_source = c_source.replace("_Static_assert", "static_assert")
    descriptor_source = f'''#include "kernel/syscall_catalog_generated.h"
_Static_assert(OS64_SYSCALL_CATALOG_COUNT == {len(catalog['syscalls'])}u, "descriptor count");
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


def documentation_contract(catalog: dict, failures: list[str]) -> None:
    schema = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))
    require(schema.get("$schema") == "https://json-schema.org/draft/2020-12/schema",
            "syscall schema is not Draft 2020-12", failures)
    require(schema.get("additionalProperties") is False,
            "syscall schema does not reject unknown root fields", failures)
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
          f"error_codes={len(catalog['error_codes'])} pointer_calls={pointer_calls}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
