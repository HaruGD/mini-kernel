#include <os64/os64.h>

int os_result_failed(long result) {
    return result < 0;
}

const char* os_result_string(long result) {
    if (result >= OS64_RESULT_SUCCESS_MIN) {
        return "success";
    }
    switch (result) {
#define OS64_RESULT_STRING_CASE(symbol, message) case symbol: return message;
        OS64_RESULT_CODE_TABLE(OS64_RESULT_STRING_CASE)
#undef OS64_RESULT_STRING_CASE
        default: return "unknown error";
    }
}
