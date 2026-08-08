[BITS 64]

%include "include/arch/x86_64/cpu_local_offsets.inc"

%define SYSCALL_ENTRY_TRANSPORT_FAST 1
%define SYSCALL_CPU_STATE_STRIDE 4096
%define SYSCALL_CPU_STACK_TOP    0
%define SYSCALL_CPU_USER_RSP     8
%define SYSCALL_CPU_READY        52
%define SYSCALL_CPU_USER_ACTIVE  53

global syscall_entry64
extern syscall_frame_dispatch_asm
extern syscall_cpu_states

section .text

syscall_entry64:
    ; User GS remains kernel-owned and immutable in Phase 5S. The marker is
    ; kernel-written immediately before user return, so SWAPGS is executed
    ; only for a proven user execution interval. Both GS MSRs intentionally
    ; name the same CpuLocal until a future paranoid user-GS protocol exists.
    mov [gs:CPU_LOCAL_SYSCALL_R10_SCRATCH], r10
    mov r10d, [gs:CPU_LOCAL_LOGICAL_ID]
    shl r10, 12
    add r10, syscall_cpu_states
    cmp byte [r10 + SYSCALL_CPU_USER_ACTIVE], 1
    jne .invalid_origin
    swapgs
    mov byte [r10 + SYSCALL_CPU_USER_ACTIVE], 0
    mov [r10 + SYSCALL_CPU_USER_RSP], rsp
    mov rsp, [r10 + SYSCALL_CPU_STACK_TOP]

    ; Materialize the same logical frame as a CPL3 interrupt gate:
    ; SS, RSP, RFLAGS, CS, RIP followed by PUSH_GPRS in idt64.asm.
    push qword 0x23
    push qword [r10 + SYSCALL_CPU_USER_RSP]
    push r11
    push qword 0x2B
    push rcx
    cld
    jmp syscall_frame_dispatch_asm

.invalid_origin:
    cli
.invalid_halt:
    hlt
    jmp .invalid_halt

section .note.GNU-stack noalloc noexec nowrite progbits
