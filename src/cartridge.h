#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "mapper.h"

// Loads an iNES (.nes) file and routes CPU/PPU accesses through the mapper.
class Cartridge {
public:
    explicit Cartridge(const std::string& path);

    bool valid() const { return valid_; }
    const std::string& error() const { return error_; }

    uint8_t mapperId() const { return mapperId_; }
    uint8_t prgBanks() const { return prgBanks_; }
    uint8_t chrBanks() const { return chrBanks_; }

    Mirror mirror() const;
    Mapper* mapper() { return mapper_.get(); }

    // Return true if the cartridge claims the access (data in/out via ref).
    bool cpuRead(uint16_t addr, uint8_t& data);
    bool cpuWrite(uint16_t addr, uint8_t data);
    bool ppuRead(uint16_t addr, uint8_t& data);
    bool ppuWrite(uint16_t addr, uint8_t data);

    void reset();

private:
    bool valid_ = false;
    std::string error_;

    uint8_t mapperId_ = 0;
    uint8_t prgBanks_ = 0;   // 16 KB units
    uint8_t chrBanks_ = 0;   // 8 KB units
    Mirror hwMirror_ = Mirror::HORIZONTAL;

    std::vector<uint8_t> prg_;
    std::vector<uint8_t> chr_;      // ROM, or 8 KB RAM when chrBanks_ == 0
    std::vector<uint8_t> prgRam_;   // $6000-$7FFF (8 KB, battery or not)

    std::unique_ptr<Mapper> mapper_;
};
