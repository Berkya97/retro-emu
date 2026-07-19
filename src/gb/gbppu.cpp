#include "gbppu.h"

// Klasik DMG yeşil tonları (shade 0-3)
static const uint32_t kShades[4] = {0xFFE0F8D0, 0xFF88C070, 0xFF346856, 0xFF081820};

void GbPpu::reset() {
    lcdc_ = 0x91; stat_ = 0x85;
    scy_ = scx_ = 0;
    ly_ = 0; lyc_ = 0;
    bgp_ = 0xFC; obp0_ = obp1_ = 0xFF;
    wy_ = wx_ = 0;
    vbk_ = 0;
    bcps_ = ocps_ = 0;
    bgPalRam_.fill(0xFF);   // boot sonrası: beyaz
    objPalRam_.fill(0xFF);
    dot_ = 0;
    windowLine_ = 0;
    statLine_ = false;
    frameDone = false;
    hblankEvent = false;
    fb_.fill(kShades[0]);
}

uint32_t GbPpu::cgbColor(const std::array<uint8_t, 64>& ram, int pal, int idx) const {
    int off = pal * 8 + idx * 2;
    uint16_t c = static_cast<uint16_t>(ram[off] | (ram[off + 1] << 8));
    uint32_t r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
    r = (r << 3) | (r >> 2); g = (g << 3) | (g >> 2); b = (b << 3) | (b >> 2);
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

uint8_t GbPpu::readReg(uint16_t addr) {
    switch (addr) {
        case 0xFF40: return lcdc_;
        case 0xFF41: return stat_ | 0x80;
        case 0xFF42: return scy_;
        case 0xFF43: return scx_;
        case 0xFF44: return ly_;
        case 0xFF45: return lyc_;
        case 0xFF47: return bgp_;
        case 0xFF48: return obp0_;
        case 0xFF49: return obp1_;
        case 0xFF4A: return wy_;
        case 0xFF4B: return wx_;
        case 0xFF4F: return cgb ? (vbk_ | 0xFE) : 0xFF;
        case 0xFF68: return cgb ? (bcps_ | 0x40) : 0xFF;
        case 0xFF69: return cgb ? bgPalRam_[bcps_ & 0x3F] : 0xFF;
        case 0xFF6A: return cgb ? (ocps_ | 0x40) : 0xFF;
        case 0xFF6B: return cgb ? objPalRam_[ocps_ & 0x3F] : 0xFF;
        default: return 0xFF;
    }
}

void GbPpu::writeReg(uint16_t addr, uint8_t v) {
    switch (addr) {
        case 0xFF40: {
            bool wasOn = lcdc_ & 0x80;
            lcdc_ = v;
            if (wasOn && !(lcdc_ & 0x80)) {   // LCD kapandı
                ly_ = 0; dot_ = 0; windowLine_ = 0;
                setMode(0);
                fb_.fill(kShades[0]);
            }
            break;
        }
        case 0xFF41: stat_ = static_cast<uint8_t>((stat_ & 0x07) | (v & 0x78)); break;
        case 0xFF42: scy_ = v; break;
        case 0xFF43: scx_ = v; break;
        case 0xFF44: break;                    // LY salt okunur
        case 0xFF45: lyc_ = v; break;
        case 0xFF47: bgp_ = v; break;
        case 0xFF48: obp0_ = v; break;
        case 0xFF49: obp1_ = v; break;
        case 0xFF4A: wy_ = v; break;
        case 0xFF4B: wx_ = v; break;
        case 0xFF4F: if (cgb) vbk_ = v & 1; break;
        case 0xFF68: if (cgb) bcps_ = v & 0xBF; break;
        case 0xFF69:
            if (cgb) {
                bgPalRam_[bcps_ & 0x3F] = v;
                if (bcps_ & 0x80) bcps_ = static_cast<uint8_t>(0x80 | ((bcps_ + 1) & 0x3F));
            }
            break;
        case 0xFF6A: if (cgb) ocps_ = v & 0xBF; break;
        case 0xFF6B:
            if (cgb) {
                objPalRam_[ocps_ & 0x3F] = v;
                if (ocps_ & 0x80) ocps_ = static_cast<uint8_t>(0x80 | ((ocps_ + 1) & 0x3F));
            }
            break;
        default: break;
    }
}

uint8_t GbPpu::checkStat() {
    bool line = false;
    if ((stat_ & 0x40) && ly_ == lyc_) line = true;
    int m = mode();
    if ((stat_ & 0x08) && m == 0) line = true;
    if ((stat_ & 0x10) && m == 1) line = true;
    if ((stat_ & 0x20) && m == 2) line = true;
    uint8_t irq = (line && !statLine_) ? 0x02 : 0x00;
    statLine_ = line;
    // LYC coincidence bayrağı
    if (ly_ == lyc_) stat_ |= 0x04; else stat_ &= ~0x04;
    return irq;
}

uint8_t GbPpu::step(int tcycles) {
    if (!(lcdc_ & 0x80)) return 0;   // LCD kapalı
    uint8_t irq = 0;

    while (tcycles-- > 0) {
        dot_++;
        if (dot_ == 456) {
            dot_ = 0;
            ly_++;
            if (ly_ == 154) {
                ly_ = 0;
                windowLine_ = 0;
            }
            if (ly_ == 144) {
                setMode(1);
                irq |= 0x01;          // VBlank kesmesi
                frameDone = true;
            }
            irq |= checkStat();
        }

        if (ly_ < 144) {
            int newMode = (dot_ < 80) ? 2 : (dot_ < 252) ? 3 : 0;
            if (newMode != mode()) {
                setMode(newMode);
                if (newMode == 0) {
                    renderScanline();
                    hblankEvent = true;   // HDMA bloğu için
                }
                irq |= checkStat();
            }
        }
    }
    return irq;
}

void GbPpu::renderScanline() {
    uint32_t* row = &fb_[ly_ * 160];
    uint8_t bgIndex[160] = {};   // ham renk indeksi (sprite önceliği için)
    uint8_t bgPrio[160] = {};    // CGB: BG özniteliği bit7 (BG üstte)

    // ---- arka plan + pencere
    // DMG'de LCDC bit0 = BG kapalı (beyaz); CGB'de "master priority" olur,
    // BG yine çizilir ama sprite'lar her zaman üstte kalır.
    bool drawBg = cgb ? true : (lcdc_ & 0x01);
    if (drawBg) {
        bool winEnabled = (lcdc_ & 0x20) && ly_ >= wy_ && wx_ <= 166;
        bool windowUsed = false;
        for (int x = 0; x < 160; x++) {
            bool inWindow = winEnabled && x >= wx_ - 7;
            uint16_t tileMap;
            uint8_t px, py;
            if (inWindow) {
                tileMap = (lcdc_ & 0x40) ? 0x1C00 : 0x1800;
                px = static_cast<uint8_t>(x - (wx_ - 7));
                py = static_cast<uint8_t>(windowLine_);
                windowUsed = true;
            } else {
                tileMap = (lcdc_ & 0x08) ? 0x1C00 : 0x1800;
                px = static_cast<uint8_t>(x + scx_);
                py = static_cast<uint8_t>(ly_ + scy_);
            }
            uint16_t mapOff = static_cast<uint16_t>(tileMap + (py / 8) * 32 + (px / 8));
            uint8_t tileId = vram[mapOff];
            uint8_t attr = cgb ? vram[0x2000 + mapOff] : 0;

            int rowInTile = py & 7;
            if (attr & 0x40) rowInTile = 7 - rowInTile;          // Y flip
            int bit = (attr & 0x20) ? (px & 7) : 7 - (px & 7);   // X flip

            uint32_t tileAddr;
            if (lcdc_ & 0x10) tileAddr = tileId * 16u;
            else tileAddr = static_cast<uint32_t>(0x1000 + static_cast<int8_t>(tileId) * 16);
            if (attr & 0x08) tileAddr += 0x2000;                 // tile verisi bank 1

            uint8_t lo = vram[tileAddr + rowInTile * 2];
            uint8_t hi = vram[tileAddr + rowInTile * 2 + 1];
            uint8_t idx = static_cast<uint8_t>((((hi >> bit) & 1) << 1) | ((lo >> bit) & 1));
            bgIndex[x] = idx;
            bgPrio[x] = attr & 0x80;
            row[x] = cgb ? cgbColor(bgPalRam_, attr & 7, idx)
                         : kShades[(bgp_ >> (idx * 2)) & 3];
        }
        if (windowUsed) windowLine_++;
    } else {
        for (int x = 0; x < 160; x++) row[x] = kShades[0];
    }

    // ---- sprite'lar
    if (!(lcdc_ & 0x02)) return;
    int height = (lcdc_ & 0x04) ? 16 : 8;

    // satırdaki ilk 10 sprite (OAM sırasıyla)
    int idxs[10], count = 0;
    for (int i = 0; i < 40 && count < 10; i++) {
        int sy = oam[i * 4] - 16;
        if (ly_ >= sy && ly_ < sy + height) idxs[count++] = i;
    }
    // DMG önceliği: küçük X kazanır (eşitse OAM sırası); CGB: yalnız OAM sırası.
    if (!cgb) {
        for (int a = 0; a < count - 1; a++)
            for (int b2 = a + 1; b2 < count; b2++)
                if (oam[idxs[b2] * 4 + 1] < oam[idxs[a] * 4 + 1]) {
                    int t = idxs[a]; idxs[a] = idxs[b2]; idxs[b2] = t;
                }
    }

    // Sondan başa çizerek yüksek öncelikli sprite'ın üstte kalmasını sağlıyoruz.
    for (int k = count - 1; k >= 0; k--) {
        int i = idxs[k];
        int sy = oam[i * 4] - 16;
        int sx = oam[i * 4 + 1] - 8;
        uint8_t tile = oam[i * 4 + 2];
        uint8_t attr = oam[i * 4 + 3];
        if (height == 16) tile &= 0xFE;

        int line = ly_ - sy;
        if (attr & 0x40) line = height - 1 - line;   // Y flip
        uint32_t tileAddr = tile * 16u + line * 2u;
        if (cgb && (attr & 0x08)) tileAddr += 0x2000;   // VRAM bank 1
        uint8_t lo = vram[tileAddr], hi = vram[tileAddr + 1];
        uint8_t dmgPal = (attr & 0x10) ? obp1_ : obp0_;

        for (int px = 0; px < 8; px++) {
            int x = sx + px;
            if (x < 0 || x >= 160) continue;
            int bit = (attr & 0x20) ? px : 7 - px;   // X flip
            uint8_t idx = static_cast<uint8_t>((((hi >> bit) & 1) << 1) | ((lo >> bit) & 1));
            if (idx == 0) continue;                  // şeffaf

            bool bgWins;
            if (cgb) {
                // LCDC bit0 = 0: sprite'lar her zaman üstte
                bgWins = (lcdc_ & 0x01) && bgIndex[x] != 0 &&
                         (bgPrio[x] || (attr & 0x80));
            } else {
                bgWins = (attr & 0x80) && bgIndex[x] != 0;
            }
            if (bgWins) continue;

            row[x] = cgb ? cgbColor(objPalRam_, attr & 7, idx)
                         : kShades[(dmgPal >> (idx * 2)) & 3];
        }
    }
}
