#include <os64/os64.h>

#define SEGMENT_COUNT_MAX 16u
#define SEGMENT_SIZE_MAX 32u

static int apply_path(const char* path, char segments[][SEGMENT_SIZE_MAX], uint32_t* count) {
    while (path != 0 && *path != '\0') {
        char segment[SEGMENT_SIZE_MAX];
        uint32_t length = 0;

        while (*path == '/') {
            path++;
        }
        if (*path == '\0') {
            break;
        }
        while (path[length] != '\0' && path[length] != '/') {
            if (length + 1 >= SEGMENT_SIZE_MAX) {
                return 0;
            }
            segment[length] = path[length];
            length++;
        }
        segment[length] = '\0';
        path += length;

        if (os_streq(segment, ".")) {
            continue;
        }
        if (os_streq(segment, "..")) {
            if (*count > 0) {
                (*count)--;
            }
            continue;
        }
        if (*count >= SEGMENT_COUNT_MAX) {
            return 0;
        }
        for (uint32_t i = 0; i <= length; i++) {
            segments[*count][i] = segment[i];
        }
        (*count)++;
    }
    return 1;
}

int os_normalize_path(const char* cwd, const char* input, char* output, uint32_t capacity) {
    char segments[SEGMENT_COUNT_MAX][SEGMENT_SIZE_MAX];
    uint32_t count = 0;
    uint32_t offset = 0;

    if (input == 0 || output == 0 || capacity < 2 || input[0] == '\0') {
        return 0;
    }
    if (input[0] != '/' && !apply_path(cwd != 0 ? cwd : "/", segments, &count)) {
        return 0;
    }
    if (!apply_path(input, segments, &count)) {
        return 0;
    }

    output[offset++] = '/';
    for (uint32_t i = 0; i < count; i++) {
        size_t length = os_strlen(segments[i]);
        uint32_t separator_size = i + 1 < count ? 1u : 0u;

        if (offset + length + separator_size + 1u > capacity) {
            return 0;
        }
        for (size_t j = 0; j < length; j++) {
            output[offset++] = segments[i][j];
        }
        if (separator_size != 0) {
            output[offset++] = '/';
        }
    }
    output[offset] = '\0';
    return 1;
}

int os_resolve_path(const char* input, char* output, uint32_t capacity) {
    char cwd[OS_PATH_MAX];

    if (os_getcwd(cwd, sizeof(cwd)) < 0) {
        cwd[0] = '/';
        cwd[1] = '\0';
    }
    return os_normalize_path(cwd, input, output, capacity);
}
