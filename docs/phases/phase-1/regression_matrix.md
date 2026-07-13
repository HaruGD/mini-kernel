# Phase 1 Regression Matrix

Phase 1 stabilizes kernel diagnostics, exception isolation, ACPI discovery,
and interrupt-controller fallback on the active UEFI path.

| ID | Scenario | Expected result | Status | Automated by |
| --- | --- | --- | --- | --- |
| C3 | PIC spurious IRQ7/IRQ15 | No panic; ISR-aware EOI; counters visible in `intctl` | Automated | `tools/phase1_smoke.py` checks counter exposure and ACPI fault PIC fallback |
| D2 | Ring 3 page fault | Only the process ends with `term=page_fault` | Automated | `run ufault_c.elf` interval check |
| D3 | Ring 0 page fault | Kernel panic with `kernel page fault` | Automated | Separate fatal `pagefault` session |
| D4 | Ring 3/Ring 0 general protection fault | User process ends with `term=gp_fault`; kernel fault panics | Automated | `run ugpfault_c.elf` and separate `debugfault gp` session |
| E1 | Invalid RSDP/MADT and missing IOAPIC | Parser rejects data without panic and controller falls back to PIC | Automated | Diagnostic `debugfault acpi_*` sequence |

The smoke also boots the normal image and verifies that `debugfault` is
rejected outside diagnostic mode. Runtime ACPI mutations are restored after
each parser test, while the rejected parser state is retained for inspection.

Run the matrix with:

```sh
make test-phase1
```

Additional regression coverage:

```sh
python3 tools/uefi_smoke.py
python3 tools/uefi_userland_smoke.py
python3 tools/uefi_screen_smoke.py
make test-user-sdk
```
