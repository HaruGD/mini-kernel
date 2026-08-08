#ifndef KERNEL_SYSCALL_USER_MEMORY_H
#define KERNEL_SYSCALL_USER_MEMORY_H

#include <stdint.h>

#include "os64/result.h"

struct Process;
struct AddressSpace;

#define USER_MEMORY_ALLOW_EMPTY 0x00000001u
#define USER_MEMORY_VALID_FLAGS USER_MEMORY_ALLOW_EMPTY
#define USER_MEMORY_STRUCT_MAX 512u

enum UserMemoryAccess {
    USER_MEMORY_READ = 1,
    USER_MEMORY_WRITE = 2,
    USER_MEMORY_READ_WRITE = 3
};

struct UserMemoryLease {
    Process* process;
    AddressSpace* address_space;
    uint64_t address;
    uint64_t size;
    uint64_t end;
    uint64_t address_space_identity;
    uint64_t tlb_generation;
    uint32_t access;
    uint8_t active;
    uint8_t reserved[3];
};

OsResult user_checked_add_u64(uint64_t left, uint64_t right, uint64_t* output);
OsResult user_checked_multiply_u64(uint64_t left,
                                   uint64_t right,
                                   uint64_t* output);
int user_address_is_canonical(uint64_t address);

OsResult user_memory_lease_begin(Process* process,
                                 uint64_t address,
                                 uint64_t size,
                                 uint32_t alignment,
                                 uint32_t access,
                                 uint32_t flags,
                                 UserMemoryLease* lease);
void user_memory_lease_end(UserMemoryLease* lease);

OsResult user_memory_copy_in(Process* process,
                             void* kernel_destination,
                             uint64_t user_address,
                             uint64_t size,
                             uint32_t alignment,
                             uint32_t flags);
OsResult user_memory_copy_out(Process* process,
                              uint64_t user_address,
                              const void* kernel_source,
                              uint64_t size,
                              uint32_t alignment,
                              uint32_t flags);
OsResult user_memory_copy_array_in(Process* process,
                                   void* kernel_destination,
                                   uint64_t user_address,
                                   uint64_t element_count,
                                   uint64_t element_size,
                                   uint32_t alignment);
OsResult user_memory_copy_cstring(Process* process,
                                  uint64_t user_address,
                                  char* kernel_destination,
                                  uint64_t capacity,
                                  uint64_t* length_out);
OsResult user_memory_copy_versioned_struct_in(Process* process,
                                              uint64_t user_address,
                                              void* kernel_destination,
                                              uint32_t destination_size,
                                              uint32_t minimum_size,
                                              uint32_t expected_version,
                                              uint32_t alignment);
OsResult user_memory_validate_utf8(const uint8_t* bytes, uint64_t size);

#endif
