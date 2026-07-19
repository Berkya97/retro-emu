#include "gb.h"

GB::GB(std::shared_ptr<GbCart> cart) : cart_(std::move(cart)) {
    cpu_.connect(this);
    cgb_ = cart_->cgb();
    ppu_.cgb = cgb_;
    reset();
}

void GB::reset() {
    cpu_.reset();
    if (cgb_) cpu_.a = 0x11;   // boot ROM CGB kimliği A'da bırakır
    ppu_.reset();
    apu_.reset();
    wram_.fill(0);
    hram_.fill(0);
    ie_ = 0;
    if_ = 0xE1;
    divCounter_ = 0xAB00;
    tima_ = tma_ = tac_ = 0;
    timaCounter_ = 0;
    joypSelect_ = 0x30;
    serialData_ = 0;
    serialLog_.clear();
    sampleAccum_ = 0;
    audioBuffer.clear();
    svbk_ = 1;
    doubleSpeed_ = speedPrep_ = false;
    hdmaSrc_ = hdmaDst_ = 0;
    hdmaActive_ = false;
    hdmaRemain_ = 0;
    hdma5_ = 0xFF;
}

uint32_t GB::wramIndex(uint16_t addr) const {
    uint16_t off = addr & 0x1FFF;             // C000-DFFF (echo çağıran ayarlar)
    if (off < 0x1000) return off;             // bank 0
    uint8_t bank = cgb_ ? (svbk_ ? svbk_ : 1) : 1;
    return bank * 0x1000u + (off - 0x1000);
}

void GB::onStop() {
    if (cgb_ && speedPrep_) {
        doubleSpeed_ = !doubleSpeed_;
        speedPrep_ = false;
        divCounter_ = 0;
    }
}

void GB::hdmaCopyBlock() {
    for (int i = 0; i < 16; i++) {
        ppu_.vramWrite((hdmaDst_ + i) & 0x1FFF, read(hdmaSrc_ + i));
    }
    hdmaSrc_ += 16;
    hdmaDst_ = (hdmaDst_ + 16) & 0x1FFF;
    if (--hdmaRemain_ <= 0) {
        hdmaActive_ = false;
        hdma5_ = 0xFF;
    }
}

// ---------------------------------------------------------------- timers

void GB::stepTimers(int tcycles) {
    divCounter_ = static_cast<uint16_t>(divCounter_ + tcycles);
    if (!(tac_ & 0x04)) return;
    static const int kPeriods[4] = {1024, 16, 64, 256};
    int period = kPeriods[tac_ & 3];
    timaCounter_ += tcycles;
    while (timaCounter_ >= period) {
        timaCounter_ -= period;
        if (++tima_ == 0) {
            tima_ = tma_;
            requestInterrupt(2);
        }
    }
}

// ---------------------------------------------------------------- joypad

uint8_t GB::readJoyp() const {
    uint8_t v = 0xC0 | joypSelect_ | 0x0F;
    if (!(joypSelect_ & 0x10)) {   // yön tuşları
        if (buttons_ & 0x80) v &= ~0x01;   // Right
        if (buttons_ & 0x40) v &= ~0x02;   // Left
        if (buttons_ & 0x10) v &= ~0x04;   // Up
        if (buttons_ & 0x20) v &= ~0x08;   // Down
    }
    if (!(joypSelect_ & 0x20)) {   // aksiyon tuşları
        if (buttons_ & 0x01) v &= ~0x01;   // A
        if (buttons_ & 0x02) v &= ~0x02;   // B
        if (buttons_ & 0x04) v &= ~0x04;   // Select
        if (buttons_ & 0x08) v &= ~0x08;   // Start
    }
    return v;
}

// ---------------------------------------------------------------- MMU

uint8_t GB::read(uint16_t addr) {
    if (addr < 0x8000) return cart_->read(addr);
    if (addr < 0xA000) return ppu_.vramRead(addr - 0x8000);
    if (addr < 0xC000) return cart_->read(addr);
    if (addr < 0xE000) return wram_[wramIndex(addr)];
    if (addr < 0xFE00) return wram_[wramIndex(addr)];     // echo
    if (addr < 0xFEA0) return ppu_.oam[addr - 0xFE00];
    if (addr < 0xFF00) return 0xFF;
    if (addr == 0xFFFF) return ie_;
    if (addr >= 0xFF80) return hram_[addr - 0xFF80];

    // IO
    switch (addr) {
        case 0xFF00: return readJoyp();
        case 0xFF01: return serialData_;
        case 0xFF02: return 0x7E;
        case 0xFF04: return divCounter_ >> 8;
        case 0xFF05: return tima_;
        case 0xFF06: return tma_;
        case 0xFF07: return tac_ | 0xF8;
        case 0xFF0F: return if_ | 0xE0;
        case 0xFF4D: return cgb_ ? static_cast<uint8_t>((doubleSpeed_ ? 0x80 : 0) |
                                       (speedPrep_ ? 1 : 0) | 0x7E) : 0xFF;
        case 0xFF55:
            if (!cgb_) return 0xFF;
            return hdmaActive_ ? static_cast<uint8_t>(hdmaRemain_ - 1) : hdma5_;
        case 0xFF70: return cgb_ ? (svbk_ | 0xF8) : 0xFF;
        default:
            if (addr >= 0xFF10 && addr <= 0xFF3F) return apu_.readReg(addr);
            if ((addr >= 0xFF40 && addr <= 0xFF4B) || addr == 0xFF4F ||
                (addr >= 0xFF68 && addr <= 0xFF6B)) return ppu_.readReg(addr);
            return 0xFF;
    }
}

void GB::write(uint16_t addr, uint8_t v) {
    if (addr < 0x8000) { cart_->write(addr, v); return; }
    if (addr < 0xA000) { ppu_.vramWrite(addr - 0x8000, v); return; }
    if (addr < 0xC000) { cart_->write(addr, v); return; }
    if (addr < 0xE000) { wram_[wramIndex(addr)] = v; return; }
    if (addr < 0xFE00) { wram_[wramIndex(addr)] = v; return; }
    if (addr < 0xFEA0) { ppu_.oam[addr - 0xFE00] = v; return; }
    if (addr < 0xFF00) return;
    if (addr == 0xFFFF) { ie_ = v; return; }
    if (addr >= 0xFF80) { hram_[addr - 0xFF80] = v; return; }

    switch (addr) {
        case 0xFF00: joypSelect_ = v & 0x30; break;
        case 0xFF01: serialData_ = v; break;
        case 0xFF02:
            if (v & 0x80) {                     // transfer başlat
                serialLog_ += static_cast<char>(serialData_);
                serialData_ = 0xFF;             // karşı taraf yok
                requestInterrupt(3);
            }
            break;
        case 0xFF04: divCounter_ = 0; break;
        case 0xFF05: tima_ = v; break;
        case 0xFF06: tma_ = v; break;
        case 0xFF07: tac_ = v & 0x07; break;
        case 0xFF0F: if_ = v & 0x1F; break;
        case 0xFF46: {                          // OAM DMA (anında kopya)
            uint16_t src = static_cast<uint16_t>(v << 8);
            for (int i = 0; i < 160; i++) ppu_.oam[i] = read(src + i);
            break;
        }
        case 0xFF4D: if (cgb_) speedPrep_ = v & 1; break;
        case 0xFF51: if (cgb_) hdmaSrc_ = static_cast<uint16_t>((hdmaSrc_ & 0x00FF) | (v << 8)); break;
        case 0xFF52: if (cgb_) hdmaSrc_ = (hdmaSrc_ & 0xFF00) | (v & 0xF0); break;
        case 0xFF53: if (cgb_) hdmaDst_ = static_cast<uint16_t>((hdmaDst_ & 0x00FF) | ((v & 0x1F) << 8)); break;
        case 0xFF54: if (cgb_) hdmaDst_ = (hdmaDst_ & 0xFF00) | (v & 0xF0); break;
        case 0xFF55:
            if (!cgb_) break;
            if (hdmaActive_ && !(v & 0x80)) {   // aktif HBlank DMA iptali
                hdmaActive_ = false;
                hdma5_ = static_cast<uint8_t>(0x80 | ((hdmaRemain_ - 1) & 0x7F));
                break;
            }
            hdmaRemain_ = (v & 0x7F) + 1;
            if (v & 0x80) {
                hdmaActive_ = true;             // her HBlank'te 16 bayt
            } else {
                while (hdmaRemain_ > 0) hdmaCopyBlock();  // genel DMA: hemen
            }
            break;
        case 0xFF70: if (cgb_) svbk_ = v & 0x07; break;
        default:
            if (addr >= 0xFF10 && addr <= 0xFF3F) apu_.writeReg(addr, v);
            else if ((addr >= 0xFF40 && addr <= 0xFF4B) || addr == 0xFF4F ||
                     (addr >= 0xFF68 && addr <= 0xFF6B)) ppu_.writeReg(addr, v);
            break;
    }
}

// ---------------------------------------------------------------- frame

void GB::runFrame() {
    ppu_.frameDone = false;
    // LCD kapalıyken frameDone hiç set olmaz; kare süresi kadar koş
    int budget = 70224;
    while (!ppu_.frameDone && budget > 0) {
        int t = cpu_.step();                 // CPU T-cycle
        int mt = doubleSpeed_ ? t / 2 : t;   // makine (PPU/APU) cycle
        budget -= mt;

        uint8_t irq = ppu_.step(mt);
        if (irq & 0x01) requestInterrupt(0);
        if (irq & 0x02) requestInterrupt(1);

        if (ppu_.hblankEvent) {
            ppu_.hblankEvent = false;
            if (hdmaActive_) hdmaCopyBlock();
        }

        stepTimers(t);                       // timer CPU hızında (çift hızda 2x)
        apu_.step(mt);

        sampleAccum_ += kSampleHz * static_cast<uint32_t>(mt);
        while (sampleAccum_ >= kClockHz) {
            sampleAccum_ -= kClockHz;
            float s = apu_.output();
            audioBuffer.push_back(static_cast<int16_t>(s * 24000.0f));
        }
    }
}
