#ifndef KERNEL_ELF64_H
#define KERNEL_ELF64_H

#include <stdint.h>

#define ELF64_EI_MAG0 0
#define ELF64_EI_MAG1 1
#define ELF64_EI_MAG2 2
#define ELF64_EI_MAG3 3
#define ELF64_EI_CLASS 4
#define ELF64_EI_DATA 5
#define ELF64_EI_VERSION 6
#define ELF64_EI_OSABI 7
#define ELF64_EI_ABIVERSION 8
#define ELF64_EI_NIDENT 16

#define ELF64_ELFMAG0 0x7F
#define ELF64_ELFMAG1 'E'
#define ELF64_ELFMAG2 'L'
#define ELF64_ELFMAG3 'F'

#define ELF64_CLASS_NONE 0
#define ELF64_CLASS_64 2

#define ELF64_DATA_NONE 0
#define ELF64_DATA_LSB 1

#define ELF64_VERSION_NONE 0
#define ELF64_VERSION_CURRENT 1

#define ELF64_OSABI_SYSTEM_V 0

#define ELF64_TYPE_NONE 0
#define ELF64_TYPE_REL 1
#define ELF64_TYPE_EXEC 2
#define ELF64_TYPE_DYN 3

#define ELF64_MACHINE_NONE 0
#define ELF64_MACHINE_X86_64 62

#define ELF64_PT_NULL 0
#define ELF64_PT_LOAD 1

#define ELF64_PF_X 0x1
#define ELF64_PF_W 0x2
#define ELF64_PF_R 0x4

typedef struct Elf64_Ehdr {
    unsigned char e_ident[ELF64_EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf64_Ehdr;

typedef struct Elf64_Phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64_Phdr;

#endif
