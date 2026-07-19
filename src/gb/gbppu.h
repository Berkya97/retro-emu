#pragma once
#include <array>
#include <cstdint>

// DMG PPU. Satır başına 456 dot: mode 2 (OAM, 80) -> mode 3 (~172) ->
// mode 0 (HBlank); LY 144-153 mode 1 (VBlank). Satır, mode 0'a girerken
// tek seferde render edilir. step() biriktirdiği kesme bitlerini döndürür
// (bit0 = VBlank, bit1 = STAT).
class GbPpu {
public:
    void reset();
    uint8_t step(int tcycles);   // döner: IF'e or'lanacak kesme bitleri

    uint8_t readReg(uint16_t addr);        // FF40-FF4B, FF4F, FF68-FF6B
    void writeReg(uint16_t addr, uint8_t v);

    bool cgb = false;                      // CGB (renkli) mod
    std::array<uint8_t, 16384> vram{};     // 2 bank (CGB); DMG yalnız bank 0
    std::array<uint8_t, 160> oam{};

    // MMU erişimi aktif VBK bankı üzerinden
    uint8_t vramRead(uint16_t off) const { return vram[vbk_ * 8192u + off]; }
    void vramWrite(uint16_t off, uint8_t v) { vram[vbk_ * 8192u + off] = v; }

    bool frameDone = false;
    bool hblankEvent = false;              // mode 0'a giriş (HDMA için)
    const uint32_t* framebuffer() const { return fb_.data(); }

private:
    uint8_t lcdc_ = 0x91, stat_ = 0x85, scy_ = 0, scx_ = 0;
    uint8_t ly_ = 0, lyc_ = 0, bgp_ = 0xFC, obp0_ = 0xFF, obp1_ = 0xFF;
    uint8_t wy_ = 0, wx_ = 0;
    uint8_t vbk_ = 0;                          // FF4F VRAM bank
    uint8_t bcps_ = 0, ocps_ = 0;              // palet indeks kayıtları
    std::array<uint8_t, 64> bgPalRam_{};       // 8 palet x 4 renk x RGB555
    std::array<uint8_t, 64> objPalRam_{};
    int dot_ = 0;
    int windowLine_ = 0;         // pencere iç satır sayacı
    bool statLine_ = false;      // STAT kesmesi kenar tetiklemeli

    std::array<uint32_t, 160 * 144> fb_{};

    void renderScanline();
    uint32_t cgbColor(const std::array<uint8_t, 64>& ram, int pal, int idx) const;
    uint8_t checkStat();         // STAT satırını değerlendir, kenar varsa bit1
    int mode() const { return stat_ & 3; }
    void setMode(int m) { stat_ = static_cast<uint8_t>((stat_ & ~3) | m); }
};
