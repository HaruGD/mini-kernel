[BITS 32]

global _start
global gdt_start

extern kernel_main
extern __ctors_start
extern __ctors_end
extern __init_array_start
extern __init_array_end

_start:
    ; .ctors 호출
    mov ebx, __ctors_start
.ctors_loop:
    cmp ebx, __ctors_end
    jge .ctors_done
    call [ebx]
    add ebx, 4
    jmp .ctors_loop
.ctors_done:
    ; .init_array 호출
    mov ebx, __init_array_start
.ctor_loop:
    cmp ebx, __init_array_end
    jge .ctor_done
    call [ebx]
    add ebx, 4
    jmp .ctor_loop
.ctor_done:
    call kernel_main
    jmp $

global idt_load
extern idtp

idt_load:
    lidt [idtp]
    ret

global keyboard_handler_asm
extern keyboard_handler_c

keyboard_handler_asm:
    pushad              ; 1. 현재 CPU의 모든 레지스터 상태를 안전하게 저장
    cld                 ; 2. C언어 함수가 정상 동작하도록 방향 플래그 초기화
    call keyboard_handler_c ; 3. 우리가 만든 C언어 키보드 처리 함수 실행!
    popad               ; 4. 저장해둔 레지스터 상태를 원상 복구
    iretd               ; 5. 인터럽트 종료 및 원래 하던 일로 복귀 (Interrupt Return)

global page_fault_asm
extern page_fault_handler

page_fault_asm:
    pushad
    cld
    call page_fault_handler
    popad
    iretd

global timer_handler_asm
extern timer_handler_c

timer_handler_asm:
    pushad
    cld
    call timer_handler_c
    popad
    iretd