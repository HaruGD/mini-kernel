#include "os64_driver.h"

class HelloCppDriver {
public:
    os64_u64 start() const {
        os64_klog("hello_cpp.drv driver_entry()");
        os64_klog(message());
        return 0;
    }

private:
    const char* message() const {
        return "hello_cpp.drv C++ method call OK";
    }
};

extern "C" os64_u64 driver_entry(void) {
    HelloCppDriver driver;
    return driver.start();
}
