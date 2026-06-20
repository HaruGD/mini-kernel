#include <os64/os64.h>

static int is_space(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

size_t os_strlen(const char* text) {
    size_t length = 0;
    if (text == 0) {
        return 0;
    }
    while (text[length] != '\0') {
        length++;
    }
    return length;
}

int os_streq(const char* left, const char* right) {
    if (left == 0 || right == 0) {
        return left == right;
    }
    while (*left != '\0' && *right != '\0') {
        if (*left++ != *right++) {
            return 0;
        }
    }
    return *left == *right;
}

char* os_trim(char* text) {
    char* start = text;
    size_t length;

    if (text == 0) {
        return 0;
    }
    while (is_space(*start)) {
        start++;
    }
    length = os_strlen(start);
    while (length > 0 && is_space(start[length - 1])) {
        start[--length] = '\0';
    }
    return start;
}

int os_parse_u32(const char* text, uint32_t* value_out) {
    uint32_t value = 0;
    uint32_t base = 10;
    int saw_digit = 0;

    if (text == 0) {
        return 0;
    }
    while (is_space(*text)) {
        text++;
    }
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text += 2;
    }
    while (*text != '\0' && !is_space(*text)) {
        uint32_t digit;

        if (*text >= '0' && *text <= '9') {
            digit = (uint32_t)(*text - '0');
        } else if (base == 16 && *text >= 'a' && *text <= 'f') {
            digit = (uint32_t)(*text - 'a' + 10);
        } else if (base == 16 && *text >= 'A' && *text <= 'F') {
            digit = (uint32_t)(*text - 'A' + 10);
        } else {
            return 0;
        }
        if (digit >= base) {
            return 0;
        }
        value = value * base + digit;
        saw_digit = 1;
        text++;
    }
    while (is_space(*text)) {
        text++;
    }
    if (!saw_digit || *text != '\0') {
        return 0;
    }
    if (value_out != 0) {
        *value_out = value;
    }
    return 1;
}
