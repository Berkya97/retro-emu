#include "mapper004.h"

void Mapper004::reset() {
    bankSelect_ = 0;
    for (auto& r : regs_) r = 0;
    mirror_ = Mirror::HARDWARE;
    irqLatch_ = irqCounter_ = 0;
    irqReload_ = irqEnable_ = irqActive_ = false;
}

bool Mapper004::cpuMapRead(uint16_t addr, uint32_t& mapped) {
    if (addr < 0x8000) return false;
    uint32_t nBanks = static_cast<uint32_t>(prgBanks_) * 2;  // 8 KB units
    bool mode = bankSelect_ & 0x40;
    uint32_t bank;
    switch ((addr >> 13) & 0x03) {
        case 0: bank = mode ? nBanks - 2 : (regs_[6] & 0x3F); break;  // $8000
        case 1: bank = regs_[7] & 0x3F; break;                        // $A000
        case 2: bank = mode ? (regs_[6] & 0x3F) : nBanks - 2; break;  // $C000
        default: bank = nBanks - 1; break;                            // $E000
    }
    mapped = bank * 0x2000 + (addr & 0x1FFF);
    return true;
}

bool Mapper004::cpuMapWrite(uint16_t addr, uint32_t& mapped, uint8_t data) {
    (void)mapped;
    if (addr < 0x8000) return false;
    bool odd = addr & 1;
    switch (addr & 0xE000) {
        case 0x8000:
            if (!odd) bankSelect_ = data;
            else regs_[bankSelect_ & 0x07] = data;
            break;
        case 0xA000:
            if (!odd) mirror_ = (data & 1) ? Mirror::HORIZONTAL : Mirror::VERTICAL;
            // odd: PRG RAM protect — ignored
            break;
        case 0xC000:
            if (!odd) irqLatch_ = data;
            else { irqCounter_ = 0; irqReload_ = true; }
            break;
        case 0xE000:
            if (!odd) { irqEnable_ = false; irqActive_ = false; }
            else irqEnable_ = true;
            break;
    }
    return false;
}

uint32_t Mapper004::mapChr(uint16_t addr) const {
    bool invert = bankSelect_ & 0x80;
    uint16_t a = addr;
    if (invert) a ^= 0x1000;
    uint32_t bank1k;
    if (a < 0x0800) bank1k = (regs_[0] & 0xFE) + ((a >> 10) & 1);
    else if (a < 0x1000) bank1k = (regs_[1] & 0xFE) + ((a >> 10) & 1);
    else bank1k = regs_[2 + ((a - 0x1000) >> 10)];
    return bank1k * 0x0400 + (a & 0x03FF);
}

bool Mapper004::ppuMapRead(uint16_t addr, uint32_t& mapped) {
    if (addr > 0x1FFF) return false;
    mapped = mapChr(addr);
    return true;
}

bool Mapper004::ppuMapWrite(uint16_t addr, uint32_t& mapped) {
    if (addr > 0x1FFF || chrBanks_ != 0) return false;
    mapped = mapChr(addr);
    return true;
}

void Mapper004::onScanline() {
    if (irqCounter_ == 0 || irqReload_) {
        irqCounter_ = irqLatch_;
        irqReload_ = false;
    } else {
        irqCounter_--;
    }
    if (irqCounter_ == 0 && irqEnable_) irqActive_ = true;
}
