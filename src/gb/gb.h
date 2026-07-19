#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "gbapu.h"
#include "gbcart.h"
#include "gbcpu.h"
#include "gbppu.h"

// Game Boy (DMG) makinesi: MMU + timer + joypad + seri port + bileşen
// koordinasyonu. runFrame() bir LCD karesi (70224 T-cycle) yürütür.
class GB {
public:
    explicit GB(std::shared_ptr<GbCart> cart);

    void reset();
    void runFrame();

    uint8_t read(uint16_t addr);
    void write(uint16_t addr, uint8_t v);

    // kesme yardımcıları (CPU kullanır)
    uint8_t interruptEnable() const { return ie_; }
    uint8_t interruptFlags() const { return if_; }
    void clearInterrupt(int bit) { if_ &= ~(1 << bit); }
    void requestInterrupt(int bit) { if_ |= 1 << bit; }

    // NES bit düzeniyle uyumlu giriş:
    // bit0=A bit1=B bit2=Select bit3=Start bit4=Up bit5=Down bit6=Left bit7=Right
    void setButtons(uint8_t state) { buttons_ = state; }

    const uint32_t* framebuffer() const { return ppu_.framebuffer(); }
    std::vector<int16_t> audioBuffer;          // 44.1 kHz int16 mono
    const std::string& serialLog() const { return serialLog_; }
    GbCart* cart() { return cart_.get(); }

private:
    std::shared_ptr<GbCart> cart_;
    GbCpu cpu_;
    GbPpu ppu_;
    GbApu apu_;

    std::array<uint8_t, 32768> wram_{};   // CGB: 8 bank; DMG yalnız 0-1
    std::array<uint8_t, 127> hram_{};
    uint8_t ie_ = 0, if_ = 0xE1;

    // CGB
    bool cgb_ = false;
    uint8_t svbk_ = 1;                    // FF70 WRAM bank (1-7)
    bool doubleSpeed_ = false;
    bool speedPrep_ = false;              // FF4D bit0
    uint16_t hdmaSrc_ = 0, hdmaDst_ = 0;
    bool hdmaActive_ = false;             // HBlank DMA sürüyor
    int hdmaRemain_ = 0;                  // kalan 16-baytlık blok
    uint8_t hdma5_ = 0xFF;

    uint32_t wramIndex(uint16_t addr) const;
    void hdmaCopyBlock();

public:
    void onStop();                        // CPU STOP: hız değişimi

private:

    // timer
    uint16_t divCounter_ = 0;
    uint8_t tima_ = 0, tma_ = 0, tac_ = 0;
    int timaCounter_ = 0;

    // joypad
    uint8_t joypSelect_ = 0x30;
    uint8_t buttons_ = 0;

    // seri port (blargg testleri buraya yazar)
    uint8_t serialData_ = 0;
    std::string serialLog_;

    void stepTimers(int tcycles);
    uint8_t readJoyp() const;

    // 44.1 kHz örnekleyici (makine/PPU hızında; çift hızda CPU 2x koşar)
    uint32_t sampleAccum_ = 0;
    static constexpr uint32_t kClockHz = 4194304;
    static constexpr uint32_t kSampleHz = 44100;
};
