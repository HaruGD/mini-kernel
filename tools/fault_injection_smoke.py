#!/usr/bin/env python3
import sys

from phase1_smoke import run_session


def main() -> int:
    diagnostic = run_session(
        "fault_injection",
        ("resources", "faulttest", "faultinject status", "resources", "locks"),
        diagnostic=True,
    )
    normal = run_session("fault_injection_reject", ("faulttest", "faultinject pmm 0"), diagnostic=False)
    required = [
        "=== RESOURCES ===",
        "FAULTTEST passed=0x00000007 expected=0x00000007 result=ok",
        "=== FAULT INJECTION ===",
        "order_violations=0x0000000000000000 recursion_violations=0x0000000000000000 release_violations=0x0000000000000000",
    ]
    missing = [item for item in required if item not in diagnostic]
    if "faulttest is only available in diagnostic boot mode" not in normal:
        missing.append("normal boot faulttest rejection")
    if "fault injection is only available in diagnostic boot mode" not in normal:
        missing.append("normal boot faultinject rejection")
    if "KERNEL PANIC" in diagnostic or "KERNEL PANIC" in normal:
        missing.append("panic observed")
    if missing:
        print("fault injection smoke missing:", file=sys.stderr)
        for item in missing:
            print(item, file=sys.stderr)
        return 1
    print("fault injection smoke OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
