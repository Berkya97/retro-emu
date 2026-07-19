#pragma once
#include "mapper.h"

// Mapper 4 (MMC3): 8 KB PRG banks, 1/2 KB CHR banks, scanline IRQ.
// The IRQ counter here is clocked per-scanline via PPU::clock's hook
// (simplified vs. real A12-edge counting; fine for most titles).
class Mapper004 : public Mapper {
public:
    Mapper004(uint8_t prgBanks, uint8_t chrBanks) : Mapper(prgBanks, chrBanks) { reset(); }

    bool cpuMapRead(uint16_t addr, uint32_t& mapped) override;
    bool cpuMapWrite(uint16_t addr, uint32_t& mapped, uint8_t data) override;
    bool ppuMapRead(uint16_t addr, uint32_t& mapped) override;
    bool ppuMapWrite(uint16_t addr, uint32_t& mapped) override;
    void reset() override;
    Mirror mirror() override { return mirror_; }

    bool irqPending() override { return irqActive_; }
    void irqClear() override { irqActive_ = false; }
    void onScanline() override;

private:
    uint8_t bankSelect_ = 0;
    uint8_t regs_[8] = {};
    Mirror mirror_ = Mirror::HARDWARE;

    uint8_t irqLatch_ = 0;
    uint8_t irqCounter_ = 0;
    bool irqReload_ = false;
    bool irqEnable_ = false;
    bool irqActive_ = false;

    uint32_t mapChr(uint16_t addr) const;
};
