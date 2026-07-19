#pragma once
#include <array>
#include <cstdint>
#include <string>

class Cartridge;

// 2C02 PPU. Dot-based state machine: 262 scanlines x 341 cycles (NTSC),
// loopy v/t/x/w scroll registers, 16-bit background shifters.
// PPU address space:
//   $0000-$1FFF  pattern tables (cartridge CHR)
//   $2000-$3EFF  nametables (2 KB CIRAM, mirrored per cartridge)
//   $3F00-$3FFF  palette RAM (32 bytes, mirrored)
class PPU {
public:
    void connectCartridge(Cartridge* cart) { cart_ = cart; }
    void reset();
    void clock();

    // CPU-facing registers ($2000-$2007, addr pre-masked to 0-7)
    uint8_t cpuRead(uint16_t addr);
    void cpuWrite(uint16_t addr, uint8_t data);

    // PPU-side memory access
    uint8_t ppuRead(uint16_t addr);
    void ppuWrite(uint16_t addr, uint8_t data);

    bool nmi = false;             // latched; Bus delivers it to the CPU
    bool frameComplete = false;

    std::array<uint8_t, 256> oam{};  // sprite attribute memory
    uint8_t oamAddr = 0;

    const uint32_t* framebuffer() const { return framebuffer_.data(); }

    // Debug: nametable 0 as ASCII (test ROMs use tile id == ASCII code)
    std::string nametableText() const;

private:
    Cartridge* cart_ = nullptr;

    // -- CPU-visible register state
    union {
        struct {
            uint8_t nametableX : 1;
            uint8_t nametableY : 1;
            uint8_t incrementMode : 1;   // 0: +1, 1: +32
            uint8_t patternSprite : 1;
            uint8_t patternBackground : 1;
            uint8_t spriteSize : 1;      // 0: 8x8, 1: 8x16
            uint8_t slaveMode : 1;
            uint8_t enableNmi : 1;
        };
        uint8_t reg = 0;
    } ctrl_;

    union {
        struct {
            uint8_t grayscale : 1;
            uint8_t renderBackgroundLeft : 1;
            uint8_t renderSpritesLeft : 1;
            uint8_t renderBackground : 1;
            uint8_t renderSprites : 1;
            uint8_t emphasizeRed : 1;
            uint8_t emphasizeGreen : 1;
            uint8_t emphasizeBlue : 1;
        };
        uint8_t reg = 0;
    } mask_;

    union {
        struct {
            uint8_t openBus : 5;
            uint8_t spriteOverflow : 1;
            uint8_t spriteZeroHit : 1;
            uint8_t verticalBlank : 1;
        };
        uint8_t reg = 0;
    } status_;

    // -- loopy scroll registers
    union LoopyReg {
        struct {
            uint16_t coarseX : 5;
            uint16_t coarseY : 5;
            uint16_t nametableX : 1;
            uint16_t nametableY : 1;
            uint16_t fineY : 3;
            uint16_t unused : 1;
        };
        uint16_t reg = 0;
    };
    LoopyReg vram_;   // current VRAM address (v)
    LoopyReg tram_;   // temporary VRAM address (t)
    uint8_t fineX_ = 0;
    uint8_t writeLatch_ = 0;   // w
    uint8_t dataBuffer_ = 0;   // $2007 read delay buffer

    // -- internal memories
    std::array<uint8_t, 2048> nametable_{};
    std::array<uint8_t, 32> palette_{};

    // -- rendering state
    int16_t scanline_ = -1;    // -1 (pre-render) .. 260
    int16_t cycle_ = 0;        // 0 .. 340
    bool oddFrame_ = false;

    uint8_t bgNextTileId_ = 0;
    uint8_t bgNextTileAttrib_ = 0;
    uint8_t bgNextTileLsb_ = 0;
    uint8_t bgNextTileMsb_ = 0;
    uint16_t bgShifterPatternLo_ = 0;
    uint16_t bgShifterPatternHi_ = 0;
    uint16_t bgShifterAttribLo_ = 0;
    uint16_t bgShifterAttribHi_ = 0;

    std::array<uint32_t, 256 * 240> framebuffer_{};

    // -- sprite state (per-scanline)
    // Evaluation runs once at cycle 257 of each visible scanline for the NEXT
    // line (matching the OAM "y+1" display convention); pattern rows are
    // fetched at cycle 340 into per-sprite shifters.
    std::array<uint8_t, 32> secondaryOam_{};   // up to 8 sprites x 4 bytes
    uint8_t spriteCount_ = 0;
    uint8_t spriteShifterLo_[8] = {};
    uint8_t spriteShifterHi_[8] = {};
    uint8_t spriteAttrib_[8] = {};
    uint8_t spriteX_[8] = {};
    bool spriteZeroInLine_ = false;
    bool spriteZeroRendering_ = false;

    void evaluateSprites();
    void fetchSpritePatterns();

    bool renderingEnabled() const { return mask_.renderBackground || mask_.renderSprites; }
    void incrementScrollX();
    void incrementScrollY();
    void transferAddressX();
    void transferAddressY();
    void loadBackgroundShifters();
    void updateShifters();
    uint16_t nametableIndex(uint16_t addr) const;
    uint32_t colorFromPalette(uint8_t palette, uint8_t pixel);

    static const uint32_t kPalette[64];
};
