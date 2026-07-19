#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <vector>
#include "apu.h"
#include "cartridge.h"
#include "cpu.h"
#include "ppu.h"

// CPU-side system bus. Owns internal RAM and routes accesses:
//   $0000-$1FFF  2 KB RAM, mirrored every $0800
//   $2000-$3FFF  PPU registers, mirrored every 8
//   $4000-$401F  APU + I/O
//   $4020-$FFFF  cartridge (PRG RAM/ROM via mapper)
class Bus {
public:
    Bus() { cpu.connectBus(this); }

    CPU cpu;
    PPU ppu;
    APU apu;

    // Audio samples produced since last drain (int16 mono @ 44.1 kHz)
    std::vector<int16_t> audioBuffer;

    // Live controller state, set by the frontend each frame.
    // bit0=A, bit1=B, bit2=Select, bit3=Start, bit4=Up, bit5=Down, bit6=Left, bit7=Right
    uint8_t controller[2] = {0, 0};

    void insertCartridge(std::shared_ptr<Cartridge> cart) {
        cart_ = std::move(cart);
        ppu.connectCartridge(cart_.get());
    }
    Cartridge* cartridge() { return cart_.get(); }

    uint8_t cpuRead(uint16_t addr);
    void cpuWrite(uint16_t addr, uint8_t data);

    void reset();
    void clock();       // one PPU tick; CPU runs every 3rd tick

private:
    std::array<uint8_t, 2048> ram_{};
    std::shared_ptr<Cartridge> cart_;
    uint32_t clockCounter_ = 0;

    uint8_t controllerShift_[2] = {0, 0};
    bool controllerStrobe_ = false;

    // OAM DMA ($4014): halts the CPU for 513/514 cycles while 256 bytes
    // are copied from CPU page XX00 into PPU OAM.
    bool dmaActive_ = false;
    bool dmaDummy_ = true;      // wait for an even CPU cycle before starting
    uint8_t dmaPage_ = 0;
    uint8_t dmaAddr_ = 0;
    uint8_t dmaData_ = 0;

    // Fractional downsampler: 1.789773 MHz CPU rate -> 44.1 kHz
    uint32_t sampleAccum_ = 0;
    static constexpr uint32_t kCpuHz = 1789773;
    static constexpr uint32_t kSampleHz = 44100;
};
