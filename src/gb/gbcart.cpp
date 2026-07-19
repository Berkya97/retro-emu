#include "gbcart.h"
#include <cstdio>

GbCart::GbCart(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { error_ = "cannot open file"; return; }
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size < 0x8000) { error_ = "file too small for a GB ROM"; std::fclose(f); return; }
    rom_.resize(static_cast<size_t>(size));
    if (std::fread(rom_.data(), 1, rom_.size(), f) != rom_.size()) {
        error_ = "read error"; std::fclose(f); return;
    }
    std::fclose(f);

    for (int i = 0x134; i <= 0x143 && rom_[i]; i++)
        if (rom_[i] >= 0x20 && rom_[i] < 0x7F) title_ += static_cast<char>(rom_[i]);

    cgb_ = (rom_[0x143] & 0x80) != 0;

    uint8_t type = rom_[0x147];
    switch (type) {
        case 0x00: mbc_ = 0; break;
        case 0x01: case 0x02: mbc_ = 1; break;
        case 0x03: mbc_ = 1; hasBattery_ = true; break;
        case 0x08: mbc_ = 0; break;
        case 0x09: mbc_ = 0; hasBattery_ = true; break;
        case 0x0F: case 0x10: case 0x13: mbc_ = 3; hasBattery_ = true; break;
        case 0x11: case 0x12: mbc_ = 3; break;
        case 0x19: case 0x1A: case 0x1C: case 0x1D: mbc_ = 5; break;
        case 0x1B: case 0x1E: mbc_ = 5; hasBattery_ = true; break;
        default:
            error_ = "unsupported cartridge type 0x" + std::to_string(type);
            return;
    }

    static const uint32_t kRamSizes[] = {0, 2048, 8192, 32768, 131072, 65536};
    uint8_t ramCode = rom_[0x149];
    ram_.resize(ramCode < 6 ? kRamSizes[ramCode] : 0, 0);
    if (mbc_ == 0 && ram_.empty() && (type == 0x08 || type == 0x09)) ram_.resize(8192, 0);

    uint32_t banks = static_cast<uint32_t>(rom_.size() / 0x4000);
    romMask_ = banks ? banks - 1 : 0;

    savPath_ = path + ".sav";
    if (hasBattery_ && !ram_.empty()) {
        if (std::FILE* s = std::fopen(savPath_.c_str(), "rb")) {
            size_t got = std::fread(ram_.data(), 1, ram_.size(), s);
            (void)got;
            std::fclose(s);
        }
    }
    valid_ = true;
}

void GbCart::saveBattery() {
    if (!hasBattery_ || ram_.empty()) return;
    if (std::FILE* s = std::fopen(savPath_.c_str(), "wb")) {
        std::fwrite(ram_.data(), 1, ram_.size(), s);
        std::fclose(s);
    }
}

uint8_t GbCart::read(uint16_t addr) {
    if (addr < 0x4000) {
        // MBC1 mode 1: alt bölge de banklanabilir (büyük ROM'lar)
        if (mbc_ == 1 && mode_ == 1) {
            uint32_t bank = (ramBank_ << 5) & romMask_;
            return rom_[bank * 0x4000 + addr];
        }
        return rom_[addr];
    }
    if (addr < 0x8000) {
        uint32_t bank = romBank_;
        if (mbc_ == 1) bank = ((ramBank_ << 5) | (romBank_ & 0x1F)) & romMask_;
        else bank &= romMask_;
        return rom_[bank * 0x4000 + (addr & 0x3FFF)];
    }
    // $A000-$BFFF external RAM
    if (!ramEnable_ || ram_.empty()) return 0xFF;
    uint32_t off = (mbc_ == 1 && mode_ == 0) ? (addr & 0x1FFF)
                                             : ramBank_ * 0x2000u + (addr & 0x1FFF);
    return off < ram_.size() ? ram_[off] : 0xFF;
}

void GbCart::write(uint16_t addr, uint8_t v) {
    if (addr < 0x2000) {
        ramEnable_ = (v & 0x0F) == 0x0A;
        return;
    }
    if (addr < 0x4000) {
        switch (mbc_) {
            case 1: {
                uint8_t b = v & 0x1F;
                romBank_ = (romBank_ & 0x60) | (b == 0 ? 1 : b);
                break;
            }
            case 3: {
                uint8_t b = v & 0x7F;
                romBank_ = (b == 0 ? 1 : b);
                break;
            }
            case 5:
                if (addr < 0x3000) romBank_ = (romBank_ & 0x100) | v;
                else romBank_ = static_cast<uint16_t>((romBank_ & 0xFF) | ((v & 1) << 8));
                break;
            default: break;
        }
        return;
    }
    if (addr < 0x6000) {
        if (mbc_ == 1) ramBank_ = v & 0x03;
        else if (mbc_ == 3) ramBank_ = v & 0x0F;   // 8+ RTC kayıtları: yok sayılır
        else if (mbc_ == 5) ramBank_ = v & 0x0F;
        return;
    }
    if (addr < 0x8000) {
        if (mbc_ == 1) mode_ = v & 1;
        return;   // MBC3 RTC latch: yok sayılır
    }
    // external RAM
    if (!ramEnable_ || ram_.empty()) return;
    if (mbc_ == 3 && ramBank_ >= 8) return;  // RTC register seçiliyse yazma
    uint32_t off = (mbc_ == 1 && mode_ == 0) ? (addr & 0x1FFF)
                                             : ramBank_ * 0x2000u + (addr & 0x1FFF);
    if (off < ram_.size()) ram_[off] = v;
}
