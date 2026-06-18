#ifndef ATA_H
#define ATA_H

#include "driver.h"
#include <stdint.h>

void itoa(int num, char* str);

class ATADriver : public Driver {
    uint8_t drive;
    bool exists;

    static const uint16_t DATA         = 0x1F0;
    static const uint16_t ERROR        = 0x1F1;
    static const uint16_t SECTOR_COUNT = 0x1F2;
    static const uint16_t LBA_LOW      = 0x1F3;
    static const uint16_t LBA_MID      = 0x1F4;
    static const uint16_t LBA_HIGH     = 0x1F5;
    static const uint16_t DRIVE_HEAD   = 0x1F6;
    static const uint16_t STATUS       = 0x1F7;
    static const uint16_t COMMAND      = 0x1F7;

    bool wait_ready();
    bool wait_drq();

public:
    ATADriver(uint8_t drive = 0xE0);
    void init() override;
    bool read_sector(uint32_t lba, uint8_t* buffer);
    bool write_sector(uint32_t lba, const uint8_t* buffer);
};

#endif
