#include <os64/os64.h>

int main(void) {

    int* test = os_malloc(256);
    if (test == 0) {
        return 1;
    }
    test[0] = 0x64;

    os_sleep(10000000);

    os_free(test);
    return 0;
}
