#include "os64_driver.h"

static const char entry_message[] OS64_EXPORT = "irq_timer_c.drv driver_entry()";
static const char exit_message[] OS64_EXPORT = "irq_timer_c.drv driver_exit()";
static volatile os64_u64 irq0_ticks;

static os64_u64 irq0_handler(os64_u64 irq) {
    (void)irq;
    irq0_ticks++;
    return irq0_ticks;
}

os64_u64 driver_entry(void) {
    os64_klog(entry_message);
    return os64_irq_register(0, irq0_handler) == 0 ? 0 : 1;
}

os64_u64 driver_exit(void) {
    os64_klog(exit_message);
    return os64_irq_unregister(0, irq0_handler) == 0 ? 0 : 1;
}
