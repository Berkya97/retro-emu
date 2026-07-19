#include "ppu.h"
#include <cstring>
#include "cartridge.h"

// Standard 2C02 palette (ARGB)
const uint32_t PPU::kPalette[64] = {
    0xFF545454, 0xFF001E74, 0xFF081090, 0xFF300088, 0xFF440064, 0xFF5C0030, 0xFF540400, 0xFF3C1800,
    0xFF202A00, 0xFF083A00, 0xFF004000, 0xFF003C00, 0xFF00323C, 0xFF000000, 0xFF000000, 0xFF000000,
    0xFF989698, 0xFF084CC4, 0xFF3032EC, 0xFF5C1EE4, 0xFF8814B0, 0xFFA01464, 0xFF982220, 0xFF783C00,
    0xFF545A00, 0xFF287200, 0xFF087C00, 0xFF007628, 0xFF006678, 0xFF000000, 0xFF000000, 0xFF000000,
    0xFFECEEEC, 0xFF4C9AEC, 0xFF787CEC, 0xFFB062EC, 0xFFE454EC, 0xFFEC58B4, 0xFFEC6A64, 0xFFD48820,
    0xFFA0AA00, 0xFF74C400, 0xFF4CD020, 0xFF38CC6C, 0xFF38B4CC, 0xFF3C3C3C, 0xFF000000, 0xFF000000,
    0xFFECEEEC, 0xFFA8CCEC, 0xFFBCBCEC, 0xFFD4B2EC, 0xFFECAEEC, 0xFFECAED4, 0xFFECB4B0, 0xFFE4C490,
    0xFFCCD278, 0xFFB4DE78, 0xFFA8E290, 0xFF98E2B4, 0xFFA0D6E4, 0xFFA0A2A0, 0xFF000000, 0xFF000000,
};

void PPU::reset() {
    ctrl_.reg = mask_.reg = status_.reg = 0;
    vram_.reg = tram_.reg = 0;
    fineX_ = writeLatch_ = dataBuffer_ = 0;
    scanline_ = -1;
    cycle_ = 0;
    oddFrame_ = false;
    nmi = false;
    frameComplete = false;
    bgNextTileId_ = bgNextTileAttrib_ = bgNextTileLsb_ = bgNextTileMsb_ = 0;
    bgShifterPatternLo_ = bgShifterPatternHi_ = 0;
    bgShifterAttribLo_ = bgShifterAttribHi_ = 0;
}

// ------------------------------------------------------------ PPU memory map

// Map $2000-$3EFF into the 2 KB CIRAM according to the cartridge's mirroring.
uint16_t PPU::nametableIndex(uint16_t addr) const {
    addr &= 0x0FFF;
    uint16_t table = addr / 0x0400;  // logical table 0-3
    uint16_t offset = addr & 0x03FF;
    Mirror m = cart_ ? cart_->mirror() : Mirror::HORIZONTAL;
    uint16_t physical = 0;
    switch (m) {
        case Mirror::VERTICAL:    physical = table & 1; break;
        case Mirror::HORIZONTAL:  physical = (table >> 1) & 1; break;
        case Mirror::ONESCREEN_LO: physical = 0; break;
        case Mirror::ONESCREEN_HI: physical = 1; break;
        default: physical = (table >> 1) & 1; break;
    }
    return physical * 0x0400 + offset;
}

uint8_t PPU::ppuRead(uint16_t addr) {
    addr &= 0x3FFF;
    uint8_t data = 0;
    if (cart_ && cart_->ppuRead(addr, data)) {
        // pattern table via CHR
    } else if (addr <= 0x3EFF) {
        data = nametable_[nametableIndex(addr)];
    } else {
        addr &= 0x001F;
        if (addr == 0x10 || addr == 0x14 || addr == 0x18 || addr == 0x1C) addr &= 0x0F;
        data = palette_[addr] & (mask_.grayscale ? 0x30 : 0x3F);
    }
    return data;
}

void PPU::ppuWrite(uint16_t addr, uint8_t data) {
    addr &= 0x3FFF;
    if (cart_ && cart_->ppuWrite(addr, data)) {
        // CHR RAM
    } else if (addr <= 0x3EFF) {
        nametable_[nametableIndex(addr)] = data;
    } else {
        addr &= 0x001F;
        if (addr == 0x10 || addr == 0x14 || addr == 0x18 || addr == 0x1C) addr &= 0x0F;
        palette_[addr] = data;
    }
}

// ------------------------------------------------------------ CPU registers

uint8_t PPU::cpuRead(uint16_t addr) {
    uint8_t data = 0;
    switch (addr) {
        case 0x0002:  // PPUSTATUS
            data = (status_.reg & 0xE0) | (dataBuffer_ & 0x1F);
            status_.verticalBlank = 0;
            writeLatch_ = 0;
            break;
        case 0x0004:  // OAMDATA
            data = oam[oamAddr];
            break;
        case 0x0007:  // PPUDATA (buffered except palette)
            data = dataBuffer_;
            dataBuffer_ = ppuRead(vram_.reg);
            if ((vram_.reg & 0x3FFF) >= 0x3F00) data = dataBuffer_;
            vram_.reg += ctrl_.incrementMode ? 32 : 1;
            break;
        default:
            break;
    }
    return data;
}

void PPU::cpuWrite(uint16_t addr, uint8_t data) {
    switch (addr) {
        case 0x0000: {  // PPUCTRL
            bool wasEnabled = ctrl_.enableNmi;
            ctrl_.reg = data;
            tram_.nametableX = ctrl_.nametableX;
            tram_.nametableY = ctrl_.nametableY;
            // Rising edge of NMI-enable during vblank fires NMI immediately
            if (!wasEnabled && ctrl_.enableNmi && status_.verticalBlank) nmi = true;
            break;
        }
        case 0x0001:  // PPUMASK
            mask_.reg = data;
            break;
        case 0x0003:  // OAMADDR
            oamAddr = data;
            break;
        case 0x0004:  // OAMDATA
            oam[oamAddr++] = data;
            break;
        case 0x0005:  // PPUSCROLL
            if (writeLatch_ == 0) {
                fineX_ = data & 0x07;
                tram_.coarseX = data >> 3;
                writeLatch_ = 1;
            } else {
                tram_.fineY = data & 0x07;
                tram_.coarseY = data >> 3;
                writeLatch_ = 0;
            }
            break;
        case 0x0006:  // PPUADDR
            if (writeLatch_ == 0) {
                tram_.reg = static_cast<uint16_t>((tram_.reg & 0x00FF) | ((data & 0x3F) << 8));
                writeLatch_ = 1;
            } else {
                tram_.reg = (tram_.reg & 0xFF00) | data;
                vram_.reg = tram_.reg;
                writeLatch_ = 0;
            }
            break;
        case 0x0007:  // PPUDATA
            ppuWrite(vram_.reg, data);
            vram_.reg += ctrl_.incrementMode ? 32 : 1;
            break;
        default:
            break;
    }
}

// ------------------------------------------------------------ scroll helpers

void PPU::incrementScrollX() {
    if (!renderingEnabled()) return;
    if (vram_.coarseX == 31) {
        vram_.coarseX = 0;
        vram_.nametableX = ~vram_.nametableX;
    } else {
        vram_.coarseX++;
    }
}

void PPU::incrementScrollY() {
    if (!renderingEnabled()) return;
    if (vram_.fineY < 7) {
        vram_.fineY++;
        return;
    }
    vram_.fineY = 0;
    if (vram_.coarseY == 29) {
        vram_.coarseY = 0;
        vram_.nametableY = ~vram_.nametableY;
    } else if (vram_.coarseY == 31) {
        vram_.coarseY = 0;  // attribute rows: wrap without nametable flip
    } else {
        vram_.coarseY++;
    }
}

void PPU::transferAddressX() {
    if (!renderingEnabled()) return;
    vram_.nametableX = tram_.nametableX;
    vram_.coarseX = tram_.coarseX;
}

void PPU::transferAddressY() {
    if (!renderingEnabled()) return;
    vram_.fineY = tram_.fineY;
    vram_.nametableY = tram_.nametableY;
    vram_.coarseY = tram_.coarseY;
}

void PPU::loadBackgroundShifters() {
    bgShifterPatternLo_ = (bgShifterPatternLo_ & 0xFF00) | bgNextTileLsb_;
    bgShifterPatternHi_ = (bgShifterPatternHi_ & 0xFF00) | bgNextTileMsb_;
    bgShifterAttribLo_ = (bgShifterAttribLo_ & 0xFF00) | ((bgNextTileAttrib_ & 0b01) ? 0xFF : 0x00);
    bgShifterAttribHi_ = (bgShifterAttribHi_ & 0xFF00) | ((bgNextTileAttrib_ & 0b10) ? 0xFF : 0x00);
}

void PPU::updateShifters() {
    if (mask_.renderBackground) {
        bgShifterPatternLo_ <<= 1;
        bgShifterPatternHi_ <<= 1;
        bgShifterAttribLo_ <<= 1;
        bgShifterAttribHi_ <<= 1;
    }
    if (mask_.renderSprites && cycle_ >= 1 && cycle_ <= 256) {
        for (int i = 0; i < spriteCount_; i++) {
            if (spriteX_[i] > 0) {
                spriteX_[i]--;
            } else {
                spriteShifterLo_[i] <<= 1;
                spriteShifterHi_[i] <<= 1;
            }
        }
    }
}

uint32_t PPU::colorFromPalette(uint8_t palette, uint8_t pixel) {
    return kPalette[ppuRead(0x3F00 + (palette << 2) + pixel) & 0x3F];
}

std::string PPU::nametableText() const {
    std::string out;
    for (int row = 0; row < 30; row++) {
        std::string line;
        for (int col = 0; col < 32; col++) {
            uint8_t t = nametable_[row * 32 + col];
            line += (t >= 0x20 && t < 0x7F) ? static_cast<char>(t) : ' ';
        }
        while (!line.empty() && line.back() == ' ') line.pop_back();
        out += line + "\n";
    }
    return out;
}

// ------------------------------------------------------------ sprites

void PPU::evaluateSprites() {
    secondaryOam_.fill(0xFF);
    spriteCount_ = 0;
    spriteZeroInLine_ = false;
    for (int i = 0; i < 8; i++) {
        spriteShifterLo_[i] = spriteShifterHi_[i] = 0;
    }
    int height = ctrl_.spriteSize ? 16 : 8;
    for (int entry = 0; entry < 64; entry++) {
        int diff = scanline_ - oam[entry * 4];
        if (diff < 0 || diff >= height) continue;
        if (spriteCount_ == 8) {
            status_.spriteOverflow = 1;  // simplified: no hardware diagonal-scan bug
            break;
        }
        if (entry == 0) spriteZeroInLine_ = true;
        std::memcpy(&secondaryOam_[spriteCount_ * 4], &oam[entry * 4], 4);
        spriteCount_++;
    }
}

void PPU::fetchSpritePatterns() {
    int height = ctrl_.spriteSize ? 16 : 8;
    for (int i = 0; i < spriteCount_; i++) {
        uint8_t sy = secondaryOam_[i * 4 + 0];
        uint8_t id = secondaryOam_[i * 4 + 1];
        uint8_t at = secondaryOam_[i * 4 + 2];
        spriteAttrib_[i] = at;
        spriteX_[i] = secondaryOam_[i * 4 + 3];

        int row = scanline_ - sy;
        if (at & 0x80) row = height - 1 - row;  // vertical flip

        uint16_t addr;
        if (!ctrl_.spriteSize) {
            addr = static_cast<uint16_t>((ctrl_.patternSprite << 12) | (id << 4) | row);
        } else {
            // 8x16: bit 0 of id selects the pattern table; two stacked tiles
            uint16_t table = (id & 0x01) << 12;
            uint8_t tile = id & 0xFE;
            if (row >= 8) { tile++; row -= 8; }
            addr = static_cast<uint16_t>(table | (tile << 4) | row);
        }

        uint8_t lo = ppuRead(addr);
        uint8_t hi = ppuRead(addr + 8);
        if (at & 0x40) {  // horizontal flip: reverse bit order
            auto rev = [](uint8_t b) {
                b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
                b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
                b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
                return b;
            };
            lo = rev(lo);
            hi = rev(hi);
        }
        spriteShifterLo_[i] = lo;
        spriteShifterHi_[i] = hi;
    }
}

// ------------------------------------------------------------ dot clock

void PPU::clock() {
    if (scanline_ >= -1 && scanline_ < 240) {
        // Odd-frame cycle skip at the end of pre-render
        if (scanline_ == -1 && cycle_ == 339 && oddFrame_ && renderingEnabled()) {
            cycle_ = 340;
        }

        if (scanline_ == -1 && cycle_ == 1) {
            status_.verticalBlank = 0;
            status_.spriteZeroHit = 0;
            status_.spriteOverflow = 0;
            spriteCount_ = 0;
            spriteZeroInLine_ = false;
            for (int i = 0; i < 8; i++) spriteShifterLo_[i] = spriteShifterHi_[i] = 0;
        }

        if ((cycle_ >= 2 && cycle_ < 258) || (cycle_ >= 321 && cycle_ < 338)) {
            updateShifters();
            switch ((cycle_ - 1) % 8) {
                case 0:
                    loadBackgroundShifters();
                    bgNextTileId_ = ppuRead(0x2000 | (vram_.reg & 0x0FFF));
                    break;
                case 2: {
                    uint16_t attrAddr = 0x23C0 | (vram_.nametableY << 11) | (vram_.nametableX << 10)
                                      | ((vram_.coarseY >> 2) << 3) | (vram_.coarseX >> 2);
                    bgNextTileAttrib_ = ppuRead(attrAddr);
                    if (vram_.coarseY & 0x02) bgNextTileAttrib_ >>= 4;
                    if (vram_.coarseX & 0x02) bgNextTileAttrib_ >>= 2;
                    bgNextTileAttrib_ &= 0x03;
                    break;
                }
                case 4:
                    bgNextTileLsb_ = ppuRead((ctrl_.patternBackground << 12)
                                           + (static_cast<uint16_t>(bgNextTileId_) << 4)
                                           + vram_.fineY);
                    break;
                case 6:
                    bgNextTileMsb_ = ppuRead((ctrl_.patternBackground << 12)
                                           + (static_cast<uint16_t>(bgNextTileId_) << 4)
                                           + vram_.fineY + 8);
                    break;
                case 7:
                    incrementScrollX();
                    break;
            }
        }

        if (cycle_ == 256) incrementScrollY();
        if (cycle_ == 257) {
            loadBackgroundShifters();
            transferAddressX();
            if (scanline_ >= 0) evaluateSprites();
        }
        if (cycle_ == 340 && scanline_ >= 0) fetchSpritePatterns();

        // Mapper scanline hook (MMC3 IRQ counter), once per rendered line
        if (cycle_ == 260 && renderingEnabled() && cart_ && cart_->mapper())
            cart_->mapper()->onScanline();
        if (cycle_ == 338 || cycle_ == 340) {
            bgNextTileId_ = ppuRead(0x2000 | (vram_.reg & 0x0FFF));  // dummy NT fetches
        }
        if (scanline_ == -1 && cycle_ >= 280 && cycle_ < 305) transferAddressY();
    }

    if (scanline_ == 241 && cycle_ == 1) {
        status_.verticalBlank = 1;
        if (ctrl_.enableNmi) nmi = true;
    }

    // Produce a pixel during visible scanlines
    if (scanline_ >= 0 && scanline_ < 240 && cycle_ >= 1 && cycle_ <= 256) {
        uint8_t bgPixel = 0, bgPalette = 0;
        if (mask_.renderBackground &&
            (mask_.renderBackgroundLeft || cycle_ > 8)) {
            uint16_t bit = 0x8000 >> fineX_;
            uint8_t p0 = (bgShifterPatternLo_ & bit) ? 1 : 0;
            uint8_t p1 = (bgShifterPatternHi_ & bit) ? 1 : 0;
            bgPixel = static_cast<uint8_t>((p1 << 1) | p0);
            uint8_t a0 = (bgShifterAttribLo_ & bit) ? 1 : 0;
            uint8_t a1 = (bgShifterAttribHi_ & bit) ? 1 : 0;
            bgPalette = static_cast<uint8_t>((a1 << 1) | a0);
        }

        uint8_t fgPixel = 0, fgPalette = 0, fgPriority = 0;
        spriteZeroRendering_ = false;
        if (mask_.renderSprites &&
            (mask_.renderSpritesLeft || cycle_ > 8)) {
            for (int i = 0; i < spriteCount_; i++) {
                if (spriteX_[i] != 0) continue;
                uint8_t p0 = (spriteShifterLo_[i] & 0x80) ? 1 : 0;
                uint8_t p1 = (spriteShifterHi_[i] & 0x80) ? 1 : 0;
                uint8_t px = static_cast<uint8_t>((p1 << 1) | p0);
                if (px == 0) continue;      // transparent; lower-priority sprite may show
                fgPixel = px;
                fgPalette = (spriteAttrib_[i] & 0x03) + 4;
                fgPriority = !(spriteAttrib_[i] & 0x20);
                if (i == 0 && spriteZeroInLine_) spriteZeroRendering_ = true;
                break;                      // first opaque sprite wins
            }
        }

        uint8_t pixel = 0, palette = 0;
        if (bgPixel == 0 && fgPixel > 0) {
            pixel = fgPixel; palette = fgPalette;
        } else if (bgPixel > 0 && fgPixel == 0) {
            pixel = bgPixel; palette = bgPalette;
        } else if (bgPixel > 0 && fgPixel > 0) {
            if (fgPriority) { pixel = fgPixel; palette = fgPalette; }
            else { pixel = bgPixel; palette = bgPalette; }
            // Sprite 0 hit: opaque bg + opaque sprite-0 pixel, both renders on
            if (spriteZeroRendering_ && mask_.renderBackground && mask_.renderSprites &&
                cycle_ != 256) {
                if ((mask_.renderBackgroundLeft && mask_.renderSpritesLeft) || cycle_ > 8)
                    status_.spriteZeroHit = 1;
            }
        }
        framebuffer_[scanline_ * 256 + (cycle_ - 1)] = colorFromPalette(palette, pixel);
    }

    cycle_++;
    if (cycle_ > 340) {
        cycle_ = 0;
        scanline_++;
        if (scanline_ > 260) {
            scanline_ = -1;
            frameComplete = true;
            oddFrame_ = !oddFrame_;
        }
    }
}
