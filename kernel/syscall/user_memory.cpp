#include "kernel/syscall/user_memory.h"

#include <stddef.h>

#include "kernel/mm/address_space.h"
#include "kernel/process.h"
#include "kernel/process64.h"
#include "kernel/spinlock.h"

#define USER_CANONICAL_LIMIT UINT64_C(0x0000800000000000)

static void zero_lease(UserMemoryLease* lease) {
    if (lease == 0) {
        return;
    }
    lease->process = 0;
    lease->address_space = 0;
    lease->address = 0;
    lease->size = 0;
    lease->end = 0;
    lease->address_space_identity = 0;
    lease->tlb_generation = 0;
    lease->access = 0;
    lease->active = 0;
    lease->reserved[0] = 0;
    lease->reserved[1] = 0;
    lease->reserved[2] = 0;
}

static int valid_alignment(uint32_t alignment) {
    return alignment != 0 && (alignment & (alignment - 1u)) == 0;
}

OsResult user_checked_add_u64(uint64_t left,
                              uint64_t right,
                              uint64_t* output) {
    if (output == 0) {
        return SYS_ERR_INVALID_ARGUMENT;
    }
    *output = 0;
    if (right > UINT64_MAX - left) {
        return SYS_ERR_OVERFLOW;
    }
    *output = left + right;
    return OS_SUCCESS;
}

OsResult user_checked_multiply_u64(uint64_t left,
                                   uint64_t right,
                                   uint64_t* output) {
    if (output == 0) {
        return SYS_ERR_INVALID_ARGUMENT;
    }
    *output = 0;
    if (left != 0 && right > UINT64_MAX / left) {
        return SYS_ERR_OVERFLOW;
    }
    *output = left * right;
    return OS_SUCCESS;
}

int user_address_is_canonical(uint64_t address) {
    return address < USER_CANONICAL_LIMIT;
}

OsResult user_memory_lease_begin(Process* process,
                                 uint64_t address,
                                 uint64_t size,
                                 uint32_t alignment,
                                 uint32_t access,
                                 uint32_t flags,
                                 UserMemoryLease* lease) {
    zero_lease(lease);
    if (lease == 0 || process == 0 || !process->active ||
        current_process() != process || !valid_alignment(alignment) ||
        (access != USER_MEMORY_READ && access != USER_MEMORY_WRITE &&
         access != USER_MEMORY_READ_WRITE) ||
        (flags & ~USER_MEMORY_VALID_FLAGS) != 0) {
        return SYS_ERR_INVALID_ARGUMENT;
    }
    if (kernel_spinlock_depth() != 0 || kernel_in_tlb_wait()) {
        return SYS_ERR_NOT_READY;
    }
    if (size == 0) {
        return (flags & USER_MEMORY_ALLOW_EMPTY) != 0
            ? OS_SUCCESS
            : SYS_ERR_INVALID_ARGUMENT;
    }
    if (address == 0 || (address & (alignment - 1u)) != 0) {
        return SYS_ERR_BAD_BUFFER;
    }

    uint64_t end = 0;
    OsResult arithmetic = user_checked_add_u64(address, size - 1u, &end);
    if (arithmetic != OS_SUCCESS) {
        return arithmetic;
    }
    if (!user_address_is_canonical(address) || !user_address_is_canonical(end)) {
        return SYS_ERR_BAD_BUFFER;
    }

    AddressSpace* space = &process->address_space;
    const uint64_t identity = address_space_identity(space);
    if (!address_space_user_access_begin(space, identity)) {
        return SYS_ERR_NOT_READY;
    }
    const int writable = access != USER_MEMORY_READ;
    if (!address_space_buffer_accessible(space, address, size, writable)) {
        address_space_user_access_end(space);
        return SYS_ERR_BAD_BUFFER;
    }

    lease->process = process;
    lease->address_space = space;
    lease->address = address;
    lease->size = size;
    lease->end = end;
    lease->address_space_identity = identity;
    lease->tlb_generation = address_space_tlb_generation(space);
    lease->access = access;
    lease->active = 1;
    return OS_SUCCESS;
}

void user_memory_lease_end(UserMemoryLease* lease) {
    if (lease == 0) {
        return;
    }
    if (lease->active && lease->address_space != 0) {
        address_space_user_access_end(lease->address_space);
    }
    zero_lease(lease);
}

static void copy_bytes(uint8_t* destination,
                       const uint8_t* source,
                       uint64_t size) {
    for (uint64_t index = 0; index < size; index++) {
        destination[index] = source[index];
    }
}

OsResult user_memory_copy_in(Process* process,
                             void* kernel_destination,
                             uint64_t user_address,
                             uint64_t size,
                             uint32_t alignment,
                             uint32_t flags) {
    if (size != 0 && kernel_destination == 0) {
        return SYS_ERR_INVALID_ARGUMENT;
    }
    UserMemoryLease lease;
    OsResult result = user_memory_lease_begin(process,
                                              user_address,
                                              size,
                                              alignment,
                                              USER_MEMORY_READ,
                                              flags,
                                              &lease);
    if (result != OS_SUCCESS || size == 0) {
        return result;
    }
    copy_bytes((uint8_t*)kernel_destination,
               (const uint8_t*)(uintptr_t)user_address,
               size);
    user_memory_lease_end(&lease);
    return OS_SUCCESS;
}

OsResult user_memory_copy_out(Process* process,
                              uint64_t user_address,
                              const void* kernel_source,
                              uint64_t size,
                              uint32_t alignment,
                              uint32_t flags) {
    if (size != 0 && kernel_source == 0) {
        return SYS_ERR_INVALID_ARGUMENT;
    }
    UserMemoryLease lease;
    OsResult result = user_memory_lease_begin(process,
                                              user_address,
                                              size,
                                              alignment,
                                              USER_MEMORY_WRITE,
                                              flags,
                                              &lease);
    if (result != OS_SUCCESS || size == 0) {
        return result;
    }
    copy_bytes((uint8_t*)(uintptr_t)user_address,
               (const uint8_t*)kernel_source,
               size);
    user_memory_lease_end(&lease);
    return OS_SUCCESS;
}

OsResult user_memory_copy_array_in(Process* process,
                                   void* kernel_destination,
                                   uint64_t user_address,
                                   uint64_t element_count,
                                   uint64_t element_size,
                                   uint32_t alignment) {
    uint64_t byte_count = 0;
    OsResult result = user_checked_multiply_u64(element_count,
                                                element_size,
                                                &byte_count);
    if (result != OS_SUCCESS) {
        return result;
    }
    return user_memory_copy_in(process,
                               kernel_destination,
                               user_address,
                               byte_count,
                               alignment,
                               USER_MEMORY_ALLOW_EMPTY);
}

OsResult user_memory_copy_cstring(Process* process,
                                  uint64_t user_address,
                                  char* kernel_destination,
                                  uint64_t capacity,
                                  uint64_t* length_out) {
    if (length_out != 0) {
        *length_out = 0;
    }
    if (kernel_destination == 0 || capacity < 2) {
        return SYS_ERR_INVALID_ARGUMENT;
    }
    uint64_t copied = 0;
    while (copied < capacity) {
        uint64_t address = 0;
        OsResult result = user_checked_add_u64(user_address, copied, &address);
        if (result != OS_SUCCESS) {
            return result;
        }
        uint64_t chunk = 4096u - (address & 4095u);
        if (chunk > capacity - copied) {
            chunk = capacity - copied;
        }
        UserMemoryLease lease;
        result = user_memory_lease_begin(process,
                                         address,
                                         chunk,
                                         1,
                                         USER_MEMORY_READ,
                                         0,
                                         &lease);
        if (result != OS_SUCCESS) {
            return result;
        }
        const char* source = (const char*)(uintptr_t)address;
        for (uint64_t index = 0; index < chunk; index++) {
            const char value = source[index];
            kernel_destination[copied + index] = value;
            if (value == '\0') {
                user_memory_lease_end(&lease);
                if (length_out != 0) {
                    *length_out = copied + index;
                }
                return OS_SUCCESS;
            }
        }
        copied += chunk;
        user_memory_lease_end(&lease);
    }
    return SYS_ERR_BUFFER_TOO_SMALL;
}

static uint32_t read_u32(const uint8_t* bytes) {
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

OsResult user_memory_copy_versioned_struct_in(Process* process,
                                              uint64_t user_address,
                                              void* kernel_destination,
                                              uint32_t destination_size,
                                              uint32_t minimum_size,
                                              uint32_t expected_version,
                                              uint32_t alignment) {
    if (kernel_destination == 0 || minimum_size < 8u ||
        destination_size < minimum_size ||
        destination_size > USER_MEMORY_STRUCT_MAX) {
        return SYS_ERR_INVALID_ARGUMENT;
    }
    uint8_t header[8] = {};
    OsResult result = user_memory_copy_in(process,
                                          header,
                                          user_address,
                                          sizeof(header),
                                          alignment,
                                          0);
    if (result != OS_SUCCESS) {
        return result;
    }
    const uint32_t declared_size = read_u32(header);
    const uint32_t version = read_u32(header + sizeof(uint32_t));
    if (declared_size < minimum_size || declared_size > destination_size ||
        version != expected_version) {
        return SYS_ERR_INVALID_ARGUMENT;
    }
    uint8_t snapshot[USER_MEMORY_STRUCT_MAX] = {};
    result = user_memory_copy_in(process,
                                 snapshot,
                                 user_address,
                                 declared_size,
                                 alignment,
                                 0);
    if (result != OS_SUCCESS) {
        return result;
    }
    if (read_u32(snapshot) != declared_size ||
        read_u32(snapshot + sizeof(uint32_t)) != expected_version) {
        return SYS_ERR_INVALID_ARGUMENT;
    }
    copy_bytes((uint8_t*)kernel_destination, snapshot, declared_size);
    for (uint32_t index = declared_size; index < destination_size; index++) {
        ((uint8_t*)kernel_destination)[index] = 0;
    }
    return OS_SUCCESS;
}

OsResult user_memory_validate_utf8(const uint8_t* bytes, uint64_t size) {
    if (size != 0 && bytes == 0) {
        return SYS_ERR_INVALID_ARGUMENT;
    }
    for (uint64_t index = 0; index < size;) {
        const uint8_t first = bytes[index++];
        if (first < 0x80u) {
            continue;
        }
        uint32_t continuation_count = 0;
        uint32_t codepoint = 0;
        if (first >= 0xC2u && first <= 0xDFu) {
            continuation_count = 1;
            codepoint = first & 0x1Fu;
        } else if (first >= 0xE0u && first <= 0xEFu) {
            continuation_count = 2;
            codepoint = first & 0x0Fu;
        } else if (first >= 0xF0u && first <= 0xF4u) {
            continuation_count = 3;
            codepoint = first & 0x07u;
        } else {
            return SYS_ERR_INVALID_ARGUMENT;
        }
        if (continuation_count > size - index) {
            return SYS_ERR_INVALID_ARGUMENT;
        }
        for (uint32_t part = 0; part < continuation_count; part++) {
            const uint8_t next = bytes[index++];
            if ((next & 0xC0u) != 0x80u) {
                return SYS_ERR_INVALID_ARGUMENT;
            }
            codepoint = (codepoint << 6) | (next & 0x3Fu);
        }
        if ((continuation_count == 2 && codepoint < 0x800u) ||
            (continuation_count == 3 && codepoint < 0x10000u) ||
            codepoint > 0x10FFFFu ||
            (codepoint >= 0xD800u && codepoint <= 0xDFFFu)) {
            return SYS_ERR_INVALID_ARGUMENT;
        }
    }
    return OS_SUCCESS;
}
