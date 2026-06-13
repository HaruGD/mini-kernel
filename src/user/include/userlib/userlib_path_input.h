static inline int user_normalize_path(const char* cwd, const char* input, char* out, uint32_t capacity) {
    char segments[16][32];
    uint32_t segment_count = 0;
    const char* cursor;

    if (out == 0 || capacity < 2 || input == 0 || input[0] == '\0') {
        return 0;
    }

    out[0] = '\0';

    if (input[0] == '/') {
        cursor = input;
    } else {
        cursor = cwd != 0 && cwd[0] != '\0' ? cwd : "/";
        while (*cursor != '\0') {
            while (*cursor == '/') {
                cursor++;
            }
            if (*cursor == '\0') {
                break;
            }

            uint32_t len = 0;
            while (cursor[len] != '\0' && cursor[len] != '/') {
                if (len + 1 >= sizeof(segments[0])) {
                    return 0;
                }
                segments[segment_count][len] = cursor[len];
                len++;
            }
            segments[segment_count][len] = '\0';
            if (segment_count + 1 >= 16) {
                return 0;
            }
            segment_count++;
            cursor += len;
        }
        cursor = input;
    }

    while (*cursor != '\0') {
        while (*cursor == '/') {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }

        char segment[32];
        uint32_t len = 0;
        while (cursor[len] != '\0' && cursor[len] != '/') {
            if (len + 1 >= sizeof(segment)) {
                return 0;
            }
            segment[len] = cursor[len];
            len++;
        }
        segment[len] = '\0';

        if (user_str_eq(segment, ".")) {
        } else if (user_str_eq(segment, "..")) {
            if (segment_count > 0) {
                segment_count--;
            }
        } else {
            if (segment_count >= 16) {
                return 0;
            }
            for (uint32_t i = 0; i <= len; i++) {
                segments[segment_count][i] = segment[i];
            }
            segment_count++;
        }

        cursor += len;
    }

    {
        uint32_t offset = 0;
        out[offset++] = '/';

        for (uint32_t i = 0; i < segment_count; i++) {
            uint32_t len = (uint32_t)user_strlen(segments[i]);
            if (offset + len + (i + 1 < segment_count ? 1u : 0u) + 1u > capacity) {
                return 0;
            }
            for (uint32_t j = 0; j < len; j++) {
                out[offset++] = segments[i][j];
            }
            if (i + 1 < segment_count) {
                out[offset++] = '/';
            }
        }
        out[offset] = '\0';
    }

    return 1;
}

static inline void user_read_line(char* buffer, uint32_t capacity) {
    uint32_t length = 0;

    if (capacity == 0) {
        return;
    }

    while (1) {
        long ch = user_getchar();
        if (ch < 0) {
            continue;
        }

        if (ch == '\r' || ch == '\n') {
            user_putchar('\n');
            break;
        }

        if (ch == '\b') {
            if (length > 0) {
                length--;
                buffer[length] = '\0';
                user_putchar('\b');
                user_putchar(' ');
                user_putchar('\b');
            }
            continue;
        }

        if (ch < 32 || ch > 126) {
            continue;
        }

        if (length + 1 >= capacity) {
            continue;
        }

        buffer[length++] = (char)ch;
        buffer[length] = '\0';
        user_putchar((char)ch);
    }

    buffer[length] = '\0';
}
