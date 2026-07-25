[BITS 64]

section .rodata

global _binary_bin_ap_trampoline_bin_start
global _binary_bin_ap_trampoline_bin_end

_binary_bin_ap_trampoline_bin_start:
    incbin "bin/ap_trampoline.bin"
_binary_bin_ap_trampoline_bin_end:

section .note.GNU-stack noalloc noexec nowrite progbits
