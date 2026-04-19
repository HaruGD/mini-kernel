#ifndef PIT_H
#define PIT_H

#include "driver.h"
#include <stdint.h>

class PIT : public Driver {
    static const uint16_t CHANNEL0  = 0x40;
    static const uint16_t COMMAND   = 0x43;
    
    uint32_t tick;

public:
    PIT();
    void init() override;
    void init(uint32_t frequency);
    void handle();
    uint32_t get_tick();
};

#endif