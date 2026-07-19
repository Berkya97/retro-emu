#include "bus.h"

uint8_t Bus::cpuRead(uint16_t addr) {
    uint8_t data = 0;
    if (addr <= 0x1FFF) {
        data = ram_[addr & 0x07FF];
    } else if (addr <= 0x3FFF) {
        data = ppu.cpuRead(addr & 0x0007);
    } else if (addr == 0x4015) {
        data = apu.cpuRead4015();
    } else if (addr == 0x4016 || addr == 0x4017) {
        int i = addr & 1;
        if (controllerStrobe_) controllerShift_[i] = controller[i];
        data = (controllerShift_[i] & 0x01) | 0x40;  // 0x40: open-bus upper bits
        controllerShift_[i] >>= 1;
        controllerShift_[i] |= 0x80;  // reads past bit 8 return 1
    } else if (addr <= 0x401F) {
        // other I/O: open bus
    } else if (cart_) {
        cart_->cpuRead(addr, data);
    }
    return data;
}

void Bus::cpuWrite(uint16_t addr, uint8_t data) {
    if (addr <= 0x1FFF) {
        ram_[addr & 0x07FF] = data;
    } else if (addr <= 0x3FFF) {
        ppu.cpuWrite(addr & 0x0007, data);
    } else if (addr == 0x4014) {
        dmaPage_ = data;
        dmaAddr_ = 0;
        dmaActive_ = true;
        dmaDummy_ = true;
    } else if (addr == 0x4016) {
        controllerStrobe_ = data & 0x01;
        if (controllerStrobe_) {
            controllerShift_[0] = controller[0];
            controllerShift_[1] = controller[1];
        }
    } else if (addr <= 0x4017) {
        apu.cpuWrite(addr, data);
    } else if (addr <= 0x401F) {
        // test-mode registers: ignored
    } else if (cart_) {
        cart_->cpuWrite(addr, data);
    }
}

void Bus::reset() {
    if (cart_) cart_->reset();
    cpu.reset();
    ppu.reset();
    apu.reset();
    clockCounter_ = 0;
    sampleAccum_ = 0;
    audioBuffer.clear();
}

void Bus::clock() {
    ppu.clock();
    if (clockCounter_ % 3 == 0) {
        apu.clockCpuCycle();

        if (dmaActive_) {
            if (dmaDummy_) {
                if (clockCounter_ % 6 == 3) dmaDummy_ = false;  // align to even CPU cycle
            } else if (clockCounter_ % 6 == 0) {
                dmaData_ = cpuRead(static_cast<uint16_t>((dmaPage_ << 8) | dmaAddr_));
            } else {
                ppu.oam[static_cast<uint8_t>(ppu.oamAddr + dmaAddr_)] = dmaData_;
                if (++dmaAddr_ == 0) dmaActive_ = false;
            }
        } else {
            bool mapperIrq = cart_ && cart_->mapper() && cart_->mapper()->irqPending();
            if ((apu.irqPending() || mapperIrq) && cpu.complete()) cpu.irq();
            cpu.clock();
        }

        // downsample APU output to 44.1 kHz
        sampleAccum_ += kSampleHz;
        if (sampleAccum_ >= kCpuHz) {
            sampleAccum_ -= kCpuHz;
            float s = apu.output();                      // 0..~1
            audioBuffer.push_back(static_cast<int16_t>((s - 0.25f) * 2.0f * 20000.0f));
        }
    }
    if (ppu.nmi) {
        ppu.nmi = false;
        cpu.nmi();
    }
    clockCounter_++;
}
