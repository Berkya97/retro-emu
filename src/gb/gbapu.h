#pragma once
#include <array>
#include <cstdint>

// GB APU: 2 pulse (biri sweep'li), wave, noise. Frame sequencer her
// 8192 T-cycle'da (512 Hz) length/envelope/sweep saatler.
class GbApu {
public:
    void reset();
    void step(int tcycles);
    uint8_t readReg(uint16_t addr);        // FF10-FF3F
    void writeReg(uint16_t addr, uint8_t v);
    float output() const;                  // [-1, 1] mono

private:
    struct Envelope {
        uint8_t initial = 0, period = 0, timer = 0;
        bool up = false;
        uint8_t volume = 0;
        void clock();
        void trigger() { volume = initial; timer = period; }
    };

    struct Square {
        bool enabled = false, dacOn = false;
        uint8_t duty = 0, dutyPos = 0;
        int lengthCounter = 0;
        bool lengthEnable = false;
        Envelope env;
        uint16_t freq = 0;
        int timer = 0;
        // sweep (yalnız CH1)
        bool sweepOn = false;
        uint8_t sweepPeriod = 0, sweepShift = 0;
        bool sweepNegate = false;
        int sweepTimer = 0;
        uint16_t sweepShadow = 0;

        void stepTimer(int t);
        void clockLength();
        void clockSweep();
        uint16_t sweepCalc();
        int out() const;
    } ch1_, ch2_;

    struct Wave {
        bool enabled = false, dacOn = false;
        int lengthCounter = 0;
        bool lengthEnable = false;
        uint8_t volumeCode = 0;
        uint16_t freq = 0;
        int timer = 0;
        uint8_t pos = 0;
        std::array<uint8_t, 16> ram{};
        void stepTimer(int t);
        void clockLength();
        int out() const;
    } ch3_;

    struct Noise {
        bool enabled = false, dacOn = false;
        int lengthCounter = 0;
        bool lengthEnable = false;
        Envelope env;
        uint8_t clockShift = 0, divisorCode = 0;
        bool widthMode = false;
        int timer = 0;
        uint16_t lfsr = 0x7FFF;
        void stepTimer(int t);
        void clockLength();
        int out() const;
    } ch4_;

    bool powerOn_ = true;
    uint8_t nr50_ = 0x77, nr51_ = 0xF3;
    int seqTimer_ = 0;
    int seqStep_ = 0;
};
