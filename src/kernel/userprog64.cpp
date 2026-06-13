#include <stdint.h>

#include "arch/x86/paging64.h"
#include "arch/x86/pmm64.h"
#include "kernel/kutil64.h"
#include "kernel/userprog64.h"

uint32_t parse_launch_command(char* command_line, UserLaunchInfo* launch) {
    if (command_line == 0 || launch == 0) {
        return 0;
    }

    launch->argc = 0;
    for (uint32_t i = 0; i < PROCESS_ARG_MAX; i++) {
        launch->argv[i] = 0;
    }

    char* cursor = command_line;
    while (*cursor != '\0') {
        while (is_space64(*cursor)) {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }
        if (launch->argc >= PROCESS_ARG_MAX) {
            break;
        }

        launch->argv[launch->argc++] = cursor;
        while (*cursor != '\0' && !is_space64(*cursor)) {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }
        *cursor++ = '\0';
    }

    return launch->argc;
}

uint64_t prepare_user_stack_with_argv(Process* process, uint64_t user_stack_top, const UserLaunchInfo* launch) {
    if (process == 0 || launch == 0) {
        return user_stack_top - 16;
    }

    uint64_t string_addrs[PROCESS_ARG_MAX];
    uint64_t rsp = user_stack_top;
    uint32_t argc = launch->argc;

    for (int32_t i = (int32_t)argc - 1; i >= 0; i--) {
        const char* arg = launch->argv[i] != 0 ? launch->argv[i] : "";
        uint32_t len = (uint32_t)strlen64(arg) + 1;
        rsp -= len;
        for (uint32_t j = 0; j < len; j++) {
            *((volatile uint8_t*)(uintptr_t)(rsp + j)) = (uint8_t)arg[j];
        }
        string_addrs[i] = rsp;
    }

    rsp &= ~0x7ULL;
    rsp -= 8;
    *((volatile uint64_t*)(uintptr_t)rsp) = 0;

    for (int32_t i = (int32_t)argc - 1; i >= 0; i--) {
        rsp -= 8;
        *((volatile uint64_t*)(uintptr_t)rsp) = string_addrs[i];
    }

    rsp -= 8;
    *((volatile uint64_t*)(uintptr_t)rsp) = argc;
    process->argc = argc;
    return rsp;
}

int elf64_has_magic(const uint8_t* image, uint32_t size) {
    if (image == 0 || size < ELF64_EI_NIDENT) {
        return 0;
    }

    return image[ELF64_EI_MAG0] == ELF64_ELFMAG0 &&
           image[ELF64_EI_MAG1] == ELF64_ELFMAG1 &&
           image[ELF64_EI_MAG2] == ELF64_ELFMAG2 &&
           image[ELF64_EI_MAG3] == ELF64_ELFMAG3;
}

int elf64_validate_supported_image(const uint8_t* image, uint32_t size, const Elf64_Ehdr** out_header) {
    if (!elf64_has_magic(image, size) || size < sizeof(Elf64_Ehdr)) {
        return 0;
    }

    const Elf64_Ehdr* header = (const Elf64_Ehdr*)(const void*)image;
    if (header->e_ident[ELF64_EI_CLASS] != ELF64_CLASS_64) {
        return 0;
    }
    if (header->e_ident[ELF64_EI_DATA] != ELF64_DATA_LSB) {
        return 0;
    }
    if (header->e_ident[ELF64_EI_VERSION] != ELF64_VERSION_CURRENT) {
        return 0;
    }
    if (header->e_ident[ELF64_EI_OSABI] != ELF64_OSABI_SYSTEM_V) {
        return 0;
    }
    if (header->e_type != ELF64_TYPE_EXEC) {
        return 0;
    }
    if (header->e_machine != ELF64_MACHINE_X86_64) {
        return 0;
    }
    if (header->e_version != ELF64_VERSION_CURRENT) {
        return 0;
    }
    if (header->e_ehsize != sizeof(Elf64_Ehdr)) {
        return 0;
    }
    if (header->e_phnum == 0) {
        return 0;
    }
    if (header->e_phentsize != sizeof(Elf64_Phdr)) {
        return 0;
    }
    if (header->e_phoff > size) {
        return 0;
    }
    if ((uint64_t)header->e_phoff + ((uint64_t)header->e_phnum * sizeof(Elf64_Phdr)) > size) {
        return 0;
    }

    if (out_header != 0) {
        *out_header = header;
    }
    return 1;
}

int elf64_collect_load_info(const uint8_t* image,
                            uint32_t size,
                            const Elf64_Ehdr* header,
                            uint32_t* out_load_count,
                            uint64_t* out_first_vaddr,
                            uint64_t* out_last_vaddr,
                            uint32_t* out_error_code) {
    if (image == 0 || header == 0) {
        if (out_error_code != 0) {
            *out_error_code = 1;
        }
        return 0;
    }

    const Elf64_Phdr* phdrs = (const Elf64_Phdr*)(const void*)(image + header->e_phoff);
    uint32_t load_count = 0;
    uint64_t first_vaddr = 0xFFFFFFFFFFFFFFFFULL;
    uint64_t last_vaddr = 0;
    int entry_covered = 0;

    for (uint16_t i = 0; i < header->e_phnum; i++) {
        const Elf64_Phdr* phdr = &phdrs[i];
        if (phdr->p_type != ELF64_PT_LOAD) {
            continue;
        }

        if (phdr->p_memsz == 0) {
            if (phdr->p_filesz == 0) {
                continue;
            }
            if (out_error_code != 0) {
                *out_error_code = 2;
            }
            return 0;
        }
        if (phdr->p_filesz > phdr->p_memsz) {
            if (out_error_code != 0) {
                *out_error_code = 3;
            }
            return 0;
        }
        if (phdr->p_offset > size) {
            if (out_error_code != 0) {
                *out_error_code = 4;
            }
            return 0;
        }
        if (phdr->p_offset + phdr->p_filesz > size) {
            if (out_error_code != 0) {
                *out_error_code = 5;
            }
            return 0;
        }
        uint64_t seg_start = phdr->p_vaddr;
        uint64_t seg_end = phdr->p_vaddr + phdr->p_memsz;
        if (seg_end < seg_start) {
            if (out_error_code != 0) {
                *out_error_code = 6;
            }
            return 0;
        }

        if (seg_start < first_vaddr) {
            first_vaddr = seg_start;
        }
        if (seg_end > last_vaddr) {
            last_vaddr = seg_end;
        }
        if (header->e_entry >= seg_start && header->e_entry < seg_end) {
            entry_covered = 1;
        }

        load_count++;
    }

    if (load_count == 0 || !entry_covered) {
        if (out_error_code != 0) {
            *out_error_code = load_count == 0 ? 7 : 8;
        }
        return 0;
    }

    if (out_load_count != 0) {
        *out_load_count = load_count;
    }
    if (out_first_vaddr != 0) {
        *out_first_vaddr = first_vaddr;
    }
    if (out_last_vaddr != 0) {
        *out_last_vaddr = last_vaddr;
    }
    if (out_error_code != 0) {
        *out_error_code = 0;
    }
    return 1;
}

uint64_t elf64_segment_page_flags(uint32_t segment_flags) {
    uint64_t flags = PAGING64_FLAG_USER;
    if (segment_flags & ELF64_PF_W) {
        flags |= PAGING64_FLAG_WRITABLE;
    }
    return flags;
}

void copy_process_name(char* dest, const char* src) {
    if (dest == 0) {
        return;
    }

    if (src == 0) {
        dest[0] = '\0';
        return;
    }

    uint32_t i = 0;
    for (; i < PROCESS_NAME_MAX - 1 && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
}

uint32_t infer_shell_prompt_kind(const char* filename) {
    if (filename == 0) {
        return SHELL_PROMPT_NONE;
    }

    if (strcmp64(filename, "ushell_c.elf") == 0) {
        return SHELL_PROMPT_CSH;
    }

    return SHELL_PROMPT_NONE;
}

void cleanup_user_process_mapping(Process* process) {
    if (process == 0) {
        return;
    }

    for (uint32_t page = 0; page < process->code_page_count; page++) {
        uint64_t virt = process->code_base + ((uint64_t)page * PAGING64_PAGE_SIZE);
        uint64_t code_phys = paging64_get_phys(virt);
        if (code_phys != 0) {
            paging64_unmap_page(virt);
            pmm64_free_block((void*)(uintptr_t)code_phys);
        }
    }

    for (uint32_t page = 0; page < process->stack_page_count; page++) {
        uint64_t virt = process->stack_base + ((uint64_t)page * PAGING64_PAGE_SIZE);
        uint64_t stack_phys = paging64_get_phys(virt);
        if (stack_phys != 0) {
            paging64_unmap_page(virt);
            pmm64_free_block((void*)(uintptr_t)stack_phys);
        }
    }
}

int copy_user_cstring(const char* user_ptr, char* kernel_buf, uint32_t max_len) {
    if (user_ptr == 0 || kernel_buf == 0 || max_len == 0) {
        return 0;
    }

    for (uint32_t i = 0; i < max_len - 1; i++) {
        if (paging64_get_phys((uint64_t)(uintptr_t)(user_ptr + i)) == 0) {
            return 0;
        }

        char c = user_ptr[i];
        kernel_buf[i] = c;
        if (c == '\0') {
            return 1;
        }
    }

    kernel_buf[max_len - 1] = '\0';
    return 1;
}

int copy_user_buffer(const uint8_t* user_ptr, uint8_t* kernel_buf, uint32_t size) {
    if ((size > 0 && user_ptr == 0) || (size > 0 && kernel_buf == 0)) {
        return 0;
    }

    for (uint32_t i = 0; i < size; i++) {
        if (paging64_get_phys((uint64_t)(uintptr_t)(user_ptr + i)) == 0) {
            return 0;
        }
        kernel_buf[i] = user_ptr[i];
    }
    return 1;
}

int copy_kernel_to_user_buffer(uint8_t* user_ptr, const uint8_t* kernel_buf, uint32_t size) {
    if ((size > 0 && user_ptr == 0) || (size > 0 && kernel_buf == 0)) {
        return 0;
    }

    for (uint32_t i = 0; i < size; i++) {
        if (paging64_get_phys((uint64_t)(uintptr_t)(user_ptr + i)) == 0) {
            return 0;
        }
        user_ptr[i] = kernel_buf[i];
    }
    return 1;
}
