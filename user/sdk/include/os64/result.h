#ifndef OS64_RESULT_H
#define OS64_RESULT_H

typedef enum OsResult {
    OS_SUCCESS = 0,
    OS_ERR_NOT_READY = -1,
    OS_ERR_INVALID_ARGUMENT = -2,
    OS_ERR_NOT_FOUND = -3,
    OS_ERR_BUFFER_TOO_SMALL = -4,
    OS_ERR_IO = -5,
    OS_ERR_ALREADY_EXISTS = -6,
    OS_ERR_NO_RESOURCES = -7,
    OS_ERR_UNSUPPORTED = -8,
    OS_ERR_WOULD_BLOCK = -9,
    OS_ERR_OUT_OF_MEMORY = -10,
    OS_ERR_OUT_OF_RANGE = -11,
} OsResult;

int os_result_failed(long result);
const char* os_result_string(long result);

#endif
