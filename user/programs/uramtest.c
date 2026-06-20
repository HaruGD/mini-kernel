#include <os64/os64.h>

int main(void) {

    int* test = os_malloc(256);

    os_sleep(10000000);


    return 0;
}