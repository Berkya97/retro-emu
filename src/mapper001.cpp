#include "mapper001.h"

void Mapper001::reset() {
    shift_ = 0x10;
    control_ = 0x0C;
    chrBank0_ = chrBank1_ = prgBank_ = 0;
    mirror_ = Mirror::HARDWARE;
}

bool Mapper001::cpuMapRead(uint16_t addr, uint32_t& mapped) {
    if (addr < 0x8000) return false;
    uint8_t mode = (control_ >> 2) & 0x03;
    if (mode <= 1) {  // 32 KB switching
        uint32_t bank = (prgBank_ & 0x0E) >> 1;
        mapped = bank * 0x8000 + (addr & 0x7FFF);
    } else if (mode == 2) {  // fix first bank at $8000
        if (addr < 0xC000) mapped = addr & 0x3FFF;
        else mapped = (prgBank_ & 0x0F) * 0x4000 + (addr & 0x3FFF);
    } else {  // mode 3: fix last bank at $C000
        if (addr < 0xC000) mapped = (prgBank_ & 0x0F) * 0x4000 + (addr & 0x3FFF);
        else mapped = static_cast<uint32_t>(prgBanks_ - 1) * 0x4000 + (addr & 0x3FFF);
    }
    return true;
}

bool Mapper001::cpuMapWrite(uint16_t addr, uint32_t& mapped, uint8_t data) {
    (void)mapped;
    if (addr < 0x8000) return false;

    if (data & 0x80) {
        shift_ = 0x10;
        control_ |= 0x0C;
        return false;
    }

    bool full = shift_ & 0x01;
    shift_ = (shift_ >> 1) | ((data & 0x01) << 4);
    if (!full) return false;

    uint8_t value = shift_ & 0x1F;
    shift_ = 0x10;
    switch ((addr >> 13) & 0x03) {
        case 0:  // $8000-$9FFF control
            control_ = value;
            switch (control_ & 0x03) {
                case 0: mirror_ = Mirror::ONESCREEN_LO; break;
                case 1: mirror_ = Mirror::ONESCREEN_HI; break;
                case 2: mirror_ = Mirror::VERTICAL; break;
                case 3: mirror_ = Mirror::HORIZONTAL; break;
            }
            break;
        case 1: chrBank0_ = value; break;   // $A000-$BFFF
        case 2: chrBank1_ = value; break;   // $C000-$DFFF
        case 3: prgBank_ = value & 0x0F; break;  // $E000-$FFFF
    }
    return false;  // register write, nothing lands in PRG ROM
}

uint32_t Mapper001::mapChr(uint16_t addr) const {
    if (control_ & 0x10) {  // two 4 KB banks
        uint8_t bank = (addr < 0x1000) ? chrBank0_ : chrBank1_;
        return static_cast<uint32_t>(bank) * 0x1000 + (addr & 0x0FFF);
    }
    return static_cast<uint32_t>(chrBank0_ & 0x1E) * 0x1000 + (addr & 0x1FFF);
}

bool Mapper001::ppuMapRead(uint16_t addr, uint32_t& mapped) {
    if (addr > 0x1FFF) return false;
    mapped = mapChr(addr);
    return true;
}

bool Mapper001::ppuMapWrite(uint16_t addr, uint32_t& mapped) {
    if (addr > 0x1FFF || chrBanks_ != 0) return false;  // CHR RAM only
    mapped = mapChr(addr);
    return true;
}
