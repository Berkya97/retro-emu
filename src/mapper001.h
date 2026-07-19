#pragma once
#include "mapper.h"

// Mapper 1 (MMC1): serial 5-bit shift register controls PRG/CHR banking and
// mirroring. Covers Zelda, Metroid, Mega Man 2, etc.
class Mapper001 : public Mapper {
public:
    Mapper001(uint8_t prgBanks, uint8_t chrBanks) : Mapper(prgBanks, chrBanks) { reset(); }

    bool cpuMapRead(uint16_t addr, uint32_t& mapped) override;
    bool cpuMapWrite(uint16_t addr, uint32_t& mapped, uint8_t data) override;
    bool ppuMapRead(uint16_t addr, uint32_t& mapped) override;
    bool ppuMapWrite(uint16_t addr, uint32_t& mapped) override;
    void reset() override;
    Mirror mirror() override { return mirror_; }

private:
    uint8_t shift_ = 0x10;
    uint8_t control_ = 0x0C;   // PRG mode 3 (fix last bank) at power-on
    uint8_t chrBank0_ = 0, chrBank1_ = 0, prgBank_ = 0;
    Mirror mirror_ = Mirror::HARDWARE;

    uint32_t mapChr(uint16_t addr) const;
};
