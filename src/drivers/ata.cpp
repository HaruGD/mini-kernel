#include "drivers/ata.h"
#include "arch/x86/io.h"
#include "drivers/terminal.h"

void itoa(int num, char* str) {
    int i = 0;
    int is_negative = 0;

    // 1. 숫자가 0일 때의 예외 처리
    if (num == 0) {
        str[i++] = '0';
        str[i] = '\0';
        return;
    }

    // 2. 음수 처리
    if (num < 0) {
        is_negative = 1;
        num = -num; // 일단 양수로 바꿔서 계산합니다.
    }

    // 3. 일의 자리부터 숫자를 하나씩 떼어내어 문자로 변환
    while (num != 0) {
        int rem = num % 10;           // 10으로 나눈 나머지 (일의 자리 숫자)
        str[i++] = rem + '0';         // 핵심! 숫자 3을 문자 '3'으로 변환
        num = num / 10;               // 다음 자리로 이동
    }

    // 4. 음수였다면 마지막에 마이너스 기호 추가
    if (is_negative) {
        str[i++] = '-';
    }

    str[i] = '\0'; // 문자열의 끝(NULL) 표시

    // 5. 문자열 뒤집기
    // (위의 로직은 일의 자리부터 배열에 넣었기 때문에 "321"처럼 거꾸로 들어있습니다)
    int start = 0;
    int end = i - 1;
    while (start < end) {
        char temp = str[start];
        str[start] = str[end];
        str[end] = temp;
        start++;
        end--;
    }
}

ATADriver::ATADriver(uint8_t drive)
    : drive(drive), exists(false) {}
Terminal terminal1;

int test = 0;
char ttt[32];

bool ATADriver::wait_ready() {
    for (int timeout = 100000; timeout > 0; timeout--) {
        uint8_t status = inb(STATUS);

        if (status & 0x01) return false; // ERR
        if (status & 0x20) return false; // DF

        if (!(status & 0x80)) {
            return true; // BSY clear
        }
    }

    return false;
}

bool ATADriver::wait_drq() {
    for (int timeout = 100000; timeout > 0; timeout--) {
        uint8_t status = inb(STATUS);

        if (status & 0x01) return false; // ERR
        if (status & 0x20) return false; // DF

        if (!(status & 0x80) && (status & 0x08)) {
            return true; // ready for data
        }
    }

    return false;
}

void ATADriver::init() {
    outb(DRIVE_HEAD, drive);

    // 드라이브 존재 확인
    uint8_t status = inb(STATUS);
    if (status == 0xFF) {
        // 드라이브 없음
        exists = false;
        return;
    }
    exists = true;
    if (!wait_ready()) {
        exists = false;
    }
}

bool ATADriver::read_sector(uint32_t lba, uint8_t* buffer) {
    if (!exists || buffer == 0) return false;
    if (!wait_ready()) return false;

    outb(DRIVE_HEAD, drive | ((lba >> 24) & 0x0F));  // 0xE0 대신 drive 사용
    outb(SECTOR_COUNT, 1);
    outb(LBA_LOW,      lba & 0xFF);
    outb(LBA_MID,      (lba >> 8) & 0xFF);
    outb(LBA_HIGH,     (lba >> 16) & 0xFF);
    outb(COMMAND,      0x20);  // READ SECTORS 명령

    if (!wait_drq()) return false;

    uint16_t* buf16 = (uint16_t*)buffer;
    for (int i = 0; i < 256; i++) {
        buf16[i] = inw(DATA);
    }

    return true;
}

bool ATADriver::write_sector(uint32_t lba, const uint8_t* buffer) {
    if (!exists || buffer == 0) return false;
    if (!wait_ready()) return false;

    outb(DRIVE_HEAD, drive | ((lba >> 24) & 0x0F));
    outb(SECTOR_COUNT, 1);
    outb(LBA_LOW,  lba & 0xFF);
    outb(LBA_MID,  (lba >> 8) & 0xFF);
    outb(LBA_HIGH, (lba >> 16) & 0xFF);
    outb(COMMAND, 0x30);

    if (!wait_drq()) return false;

    const uint16_t* buf16 = (const uint16_t*)buffer;
    for (int i = 0; i < 256; i++) {
        outw(DATA, buf16[i]);
    }

    outb(COMMAND, 0xE7); // CACHE FLUSH
    return wait_ready();
}
