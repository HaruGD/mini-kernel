#ifndef PIT_H
#define PIT_H

#include "driver.h"
#include <stdint.h>

#define PIT_BASE_FREQUENCY 1193180u
#define PIT_DEFAULT_HZ 100u

class PIT : public Driver {
    static const uint16_t CHANNEL0  = 0x40;
    static const uint16_t COMMAND   = 0x43;
    
    volatile uint64_t tick;

public:
    PIT();
    void init() override;
    void init(uint32_t frequency);
    void handle();
    uint32_t get_tick();
    uint64_t get_tick64() const;
    uint32_t get_frequency() const;
    uint32_t ticks_to_ms(uint32_t ticks) const;
};

#endif
