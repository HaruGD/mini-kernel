#include <os64/os64.h>

int os_result_failed(long result) {
    return result < 0;
}

const char* os_result_string(long result) {
    switch (result) {
        case OS_SUCCESS: return "success";
        case OS_ERR_NOT_READY: return "not ready";
        case OS_ERR_INVALID_ARGUMENT: return "invalid argument";
        case OS_ERR_NOT_FOUND: return "not found";
        case OS_ERR_BUFFER_TOO_SMALL: return "buffer too small";
        case OS_ERR_IO: return "I/O error";
        case OS_ERR_ALREADY_EXISTS: return "already exists";
        case OS_ERR_NO_RESOURCES: return "no resources";
        case OS_ERR_UNSUPPORTED: return "unsupported";
        case OS_ERR_WOULD_BLOCK: return "would block";
        case OS_ERR_OUT_OF_MEMORY: return "out of memory";
        case OS_ERR_OUT_OF_RANGE: return "out of range";
        default: return "unknown error";
    }
}
