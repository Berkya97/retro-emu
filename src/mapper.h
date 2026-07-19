#pragma once
#include <cstdint>

// Nametable mirroring arrangement. HARDWARE means "use whatever the iNES
// header said" (solder pad on real carts); mappers with CIRAM control
// (MMC1, MMC3...) override it dynamically.
enum class Mirror {
    HARDWARE,
    HORIZONTAL,
    VERTICAL,
    ONESCREEN_LO,
    ONESCREEN_HI,
};

// A mapper translates CPU/PPU addresses into offsets inside the cartridge's
// PRG/CHR memory. Each map function returns true if the cartridge claims the
// address, leaving the translated offset in `mapped`.
class Mapper {
public:
    Mapper(uint8_t prgBanks, uint8_t chrBanks)
        : prgBanks_(prgBanks), chrBanks_(chrBanks) {}
    virtual ~Mapper() = default;

    virtual bool cpuMapRead(uint16_t addr, uint32_t& mapped) = 0;
    virtual bool cpuMapWrite(uint16_t addr, uint32_t& mapped, uint8_t data) = 0;
    virtual bool ppuMapRead(uint16_t addr, uint32_t& mapped) = 0;
    virtual bool ppuMapWrite(uint16_t addr, uint32_t& mapped) = 0;

    virtual void reset() {}
    virtual Mirror mirror() { return Mirror::HARDWARE; }

    // IRQ hooks (used by MMC3)
    virtual bool irqPending() { return false; }
    virtual void irqClear() {}
    virtual void onScanline() {}

protected:
    uint8_t prgBanks_;  // 16 KB units
    uint8_t chrBanks_;  // 8 KB units
};
