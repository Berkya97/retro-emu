#pragma once
#include "mapper.h"

// Mapper 0 (NROM): no banking. 16 KB PRG mirrored at $8000/$C000, or 32 KB
// straight. CHR is an 8 KB ROM (or RAM if the cart has none).
class Mapper000 : public Mapper {
public:
    using Mapper::Mapper;

    bool cpuMapRead(uint16_t addr, uint32_t& mapped) override;
    bool cpuMapWrite(uint16_t addr, uint32_t& mapped, uint8_t data) override;
    bool ppuMapRead(uint16_t addr, uint32_t& mapped) override;
    bool ppuMapWrite(uint16_t addr, uint32_t& mapped) override;
};
