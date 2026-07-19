#include "cartridge.h"
#include <cstdio>
#include <cstring>
#include "mapper000.h"
#include "mapper001.h"
#include "mapper004.h"

Cartridge::Cartridge(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        error_ = "cannot open file";
        return;
    }

    uint8_t header[16];
    if (std::fread(header, 1, 16, f) != 16 || std::memcmp(header, "NES\x1A", 4) != 0) {
        error_ = "not an iNES file (bad magic)";
        std::fclose(f);
        return;
    }

    prgBanks_ = header[4];
    chrBanks_ = header[5];
    uint8_t flags6 = header[6];
    uint8_t flags7 = header[7];

    mapperId_ = static_cast<uint8_t>((flags7 & 0xF0) | (flags6 >> 4));
    hwMirror_ = (flags6 & 0x01) ? Mirror::VERTICAL : Mirror::HORIZONTAL;
    // flags6 bit 3: four-screen (rare; not supported — falls back to header mirroring)

    if (flags6 & 0x04) std::fseek(f, 512, SEEK_CUR);  // skip trainer

    prg_.resize(static_cast<size_t>(prgBanks_) * 16384);
    if (std::fread(prg_.data(), 1, prg_.size(), f) != prg_.size()) {
        error_ = "truncated PRG data";
        std::fclose(f);
        return;
    }

    if (chrBanks_ == 0) {
        chr_.resize(8192, 0);  // CHR RAM
    } else {
        chr_.resize(static_cast<size_t>(chrBanks_) * 8192);
        if (std::fread(chr_.data(), 1, chr_.size(), f) != chr_.size()) {
            error_ = "truncated CHR data";
            std::fclose(f);
            return;
        }
    }
    std::fclose(f);

    prgRam_.resize(8192, 0);

    switch (mapperId_) {
        case 0: mapper_ = std::make_unique<Mapper000>(prgBanks_, chrBanks_); break;
        case 1: mapper_ = std::make_unique<Mapper001>(prgBanks_, chrBanks_); break;
        case 4: mapper_ = std::make_unique<Mapper004>(prgBanks_, chrBanks_); break;
        default:
            error_ = "unsupported mapper " + std::to_string(mapperId_);
            return;
    }
    valid_ = true;
}

Mirror Cartridge::mirror() const {
    Mirror m = mapper_ ? mapper_->mirror() : Mirror::HARDWARE;
    return m == Mirror::HARDWARE ? hwMirror_ : m;
}

bool Cartridge::cpuRead(uint16_t addr, uint8_t& data) {
    if (addr >= 0x6000 && addr <= 0x7FFF) {
        data = prgRam_[addr & 0x1FFF];
        return true;
    }
    uint32_t mapped;
    if (mapper_->cpuMapRead(addr, mapped)) {
        data = prg_[mapped % prg_.size()];
        return true;
    }
    return false;
}

bool Cartridge::cpuWrite(uint16_t addr, uint8_t data) {
    if (addr >= 0x6000 && addr <= 0x7FFF) {
        prgRam_[addr & 0x1FFF] = data;
        return true;
    }
    uint32_t mapped;
    if (mapper_->cpuMapWrite(addr, mapped, data)) {
        prg_[mapped % prg_.size()] = data;
        return true;
    }
    // Mapper register writes land in $8000+ but map nowhere; still claimed.
    return addr >= 0x8000;
}

bool Cartridge::ppuRead(uint16_t addr, uint8_t& data) {
    uint32_t mapped;
    if (mapper_->ppuMapRead(addr, mapped)) {
        data = chr_[mapped % chr_.size()];
        return true;
    }
    return false;
}

bool Cartridge::ppuWrite(uint16_t addr, uint8_t data) {
    uint32_t mapped;
    if (mapper_->ppuMapWrite(addr, mapped)) {
        chr_[mapped % chr_.size()] = data;
        return true;
    }
    return false;
}

void Cartridge::reset() {
    if (mapper_) mapper_->reset();
}
