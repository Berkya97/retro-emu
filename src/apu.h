#pragma once
#include <cstdint>
#include <vector>

// 2A03 APU: 2 pulse, triangle, noise. DMC is stubbed (registers accepted,
// silent). Clocked once per CPU cycle; the frame counter runs on NTSC CPU
// cycle thresholds. Mixed with the NesDev linear approximation.
class APU {
public:
    void reset();
    void clockCpuCycle();

    uint8_t cpuRead4015();
    void cpuWrite(uint16_t addr, uint8_t data);  // $4000-$4017

    bool irqPending() const { return frameIrq_; }

    // Mixed output in [0,1] for the current CPU cycle
    float output() const;

private:
    static const uint8_t kLengthTable[32];
    static const uint8_t kDutyTable[4][8];
    static const uint16_t kNoisePeriods[16];

    struct Envelope {
        bool start = false, loop = false, constant = false;
        uint8_t volume = 0, divider = 0, decay = 0;
        void clock();
        uint8_t out() const { return constant ? volume : decay; }
    };

    struct Pulse {
        bool enabled = false;
        bool isPulse1 = true;
        uint8_t duty = 0, dutyPos = 0;
        uint8_t lengthCounter = 0;
        bool lengthHalt = false;
        Envelope env;
        bool sweepEnable = false, sweepNegate = false, sweepReload = false;
        uint8_t sweepPeriod = 0, sweepShift = 0, sweepDivider = 0;
        uint16_t timerPeriod = 0, timer = 0;

        void clockTimer();      // every APU cycle (2 CPU cycles)
        void clockLength();
        void clockSweep();
        uint16_t sweepTarget() const;
        bool muted() const;
        uint8_t out() const;
    } pulse1_, pulse2_;

    struct TriangleCh {
        bool enabled = false;
        uint8_t lengthCounter = 0;
        bool control = false;           // also length halt
        uint8_t linearReload = 0, linearCounter = 0;
        bool linearReloadFlag = false;
        uint16_t timerPeriod = 0, timer = 0;
        uint8_t seqPos = 0;
        void clockTimer();              // every CPU cycle
        void clockLength();
        void clockLinear();
        uint8_t out() const;
    } triangle_;

    struct NoiseCh {
        bool enabled = false;
        uint8_t lengthCounter = 0;
        bool lengthHalt = false;
        Envelope env;
        bool mode = false;
        uint16_t timerPeriod = 0, timer = 0;
        uint16_t lfsr = 1;
        void clockTimer();              // every CPU cycle (period table is in CPU cycles)
        void clockLength();
        uint8_t out() const;
    } noise_;

    // frame counter
    uint32_t frameCycle_ = 0;
    bool mode5_ = false;
    bool irqInhibit_ = false;
    bool frameIrq_ = false;
    bool apuCycleToggle_ = false;  // pulse timers tick every other CPU cycle

    void quarterFrame();
    void halfFrame();
};
