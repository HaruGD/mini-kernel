#ifndef KERNEL_USERPROG64_H
#define KERNEL_USERPROG64_H

#include <stdint.h>

#include "kernel/elf64.h"
#include "kernel/process.h"

struct UserLaunchInfo {
    uint32_t argc;
    char* argv[PROCESS_ARG_MAX];
};

uint32_t parse_launch_command(char* command_line, UserLaunchInfo* launch);
uint64_t prepare_user_stack_with_argv(Process* process, uint64_t user_stack_top, const UserLaunchInfo* launch);

int elf64_has_magic(const uint8_t* image, uint32_t size);
int elf64_validate_supported_image(const uint8_t* image, uint32_t size, const Elf64_Ehdr** out_header);
int elf64_collect_load_info(const uint8_t* image,
                            uint32_t size,
                            const Elf64_Ehdr* header,
                            uint32_t* out_load_count,
                            uint64_t* out_first_vaddr,
                            uint64_t* out_last_vaddr,
                            uint32_t* out_error_code);
uint64_t elf64_segment_page_flags(uint32_t segment_flags);

void copy_process_name(char* dest, const char* src);
uint32_t infer_shell_prompt_kind(const char* filename);

void cleanup_user_process_mapping(Process* process);
int copy_user_cstring(const char* user_ptr, char* kernel_buf, uint32_t max_len);
int copy_user_buffer(const uint8_t* user_ptr, uint8_t* kernel_buf, uint32_t size);
int copy_kernel_to_user_buffer(uint8_t* user_ptr, const uint8_t* kernel_buf, uint32_t size);

#endif
