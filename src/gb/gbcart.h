#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Game Boy cartridge: iNES'in GB karşılığı. Header'dan MBC tipini okur;
// MBC yok / MBC1 / MBC3 / MBC5 banking desteklenir. Pilli kartuşlarda
// harici RAM <rom>.sav dosyasına yazılır/okunur.
class GbCart {
public:
    explicit GbCart(const std::string& path);

    bool valid() const { return valid_; }
    const std::string& error() const { return error_; }
    const std::string& title() const { return title_; }
    int mbc() const { return mbc_; }
    bool cgb() const { return cgb_; }   // header 0x143 bit7: CGB destekli/zorunlu

    uint8_t read(uint16_t addr);          // $0000-$7FFF ROM, $A000-$BFFF RAM
    void write(uint16_t addr, uint8_t v); // banking kontrol + RAM

    void saveBattery();                    // .sav yaz (pilliyse)

private:
    bool valid_ = false;
    std::string error_;
    std::string title_;
    std::string savPath_;

    std::vector<uint8_t> rom_;
    std::vector<uint8_t> ram_;
    int mbc_ = 0;               // 0, 1, 3, 5
    bool hasBattery_ = false;
    bool cgb_ = false;

    bool ramEnable_ = false;
    uint16_t romBank_ = 1;
    uint8_t ramBank_ = 0;
    uint8_t mode_ = 0;          // MBC1 banking mode

    uint32_t romMask_ = 0;      // bank sayısı - 1
};
