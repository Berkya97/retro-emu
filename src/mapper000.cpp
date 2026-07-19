#include "mapper000.h"

bool Mapper000::cpuMapRead(uint16_t addr, uint32_t& mapped) {
    if (addr >= 0x8000) {
        mapped = addr & (prgBanks_ > 1 ? 0x7FFF : 0x3FFF);
        return true;
    }
    return false;
}

bool Mapper000::cpuMapWrite(uint16_t addr, uint32_t& mapped, uint8_t) {
    // PRG is ROM on NROM; writes are ignored (no claim below $8000 either —
    // NROM has no PRG RAM in the common configuration).
    (void)addr; (void)mapped;
    return false;
}

bool Mapper000::ppuMapRead(uint16_t addr, uint32_t& mapped) {
    if (addr <= 0x1FFF) {
        mapped = addr;
        return true;
    }
    return false;
}

bool Mapper000::ppuMapWrite(uint16_t addr, uint32_t& mapped) {
    if (addr <= 0x1FFF && chrBanks_ == 0) {  // CHR RAM carts only
        mapped = addr;
        return true;
    }
    return false;
}
