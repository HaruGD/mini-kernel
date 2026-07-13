# ACPI Power-Off

OS64 implements firmware-described S5 shutdown rather than a QEMU-only magic
port. The shell command is:

```text
OS64> shutdown
```

## Discovery Path

1. The RSDT/XSDT scan retains the Fixed ACPI Description Table (`FACP`).
2. The FADT checksum and length are validated.
3. The 64-bit `X_DSDT` address is preferred, with the legacy `DSDT` address as
   a fallback.
4. The DSDT checksum and length are validated.
5. The AML stream is searched for a bounded static
   `Name(_S5_, Package(...))` declaration.
6. The first two package integers become the PM1a and PM1b sleep types.

The AML reader accepts zero, one, byte, word, dword, and qword integer
encodings. Every package and integer access is bounded by the validated DSDT
length.

## Register Access

Extended FADT generic-address structures are preferred. Legacy PM1 control
ports are used when the extended fields are absent or invalid.

Supported address spaces:

- System I/O, using 16-bit x86 port access
- System Memory, mapped writable, cache-disabled, and non-executable

If firmware has not enabled ACPI mode, OS64 writes the FADT `ACPI_ENABLE`
value to `SMI_CMD` and waits for the PM1 `SCI_EN` bit. Shutdown then preserves
the existing PM1 control value, replaces the sleep-type field, and sets
`SLP_EN`. PM1b is written before PM1a when both blocks exist.

## Compatibility Boundary

This supports the common PC firmware form where `_S5_` is a static package in
the DSDT, including the OVMF/q35 path used by the automated test. Firmware that
constructs `_S5_` through AML method execution or exposes it only through an
SSDT requires a fuller AML interpreter and is reported as unsupported instead
of using guessed hardware ports.

Run the automated shutdown check with:

```sh
make test-shutdown
```

The test boots the normal UEFI image, enters `shutdown`, requires the ACPI S5
diagnostics, and fails unless QEMU exits by itself.
