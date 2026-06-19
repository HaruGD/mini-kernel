ENTRY(_start64)

PHDRS
{
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x100000;

    . = ALIGN(0x1000);
    __kernel_text_start = .;
    .text :
    {
        *(.text*)
    } :text
    __kernel_text_end = .;

    . = ALIGN(0x1000);
    __kernel_rodata_start = .;
    .rodata :
    {
        *(.rodata*)
    } :rodata
    __kernel_rodata_end = .;

    . = ALIGN(0x1000);
    __kernel_data_start = .;
    .data :
    {
        *(.data*)
    } :data

    .init_array :
    {
        __init_array_start = .;
        KEEP(*(.init_array*))
        __init_array_end = .;
    } :data
    __kernel_data_end = .;

    . = ALIGN(0x1000);
    __kernel_bss_start = .;
    .bss :
    {
        __bss_start = .;
        *(COMMON)
        *(.bss*)
        __bss_end = .;
    } :data
    __kernel_bss_end = .;
}
