#include "apu.h"

const uint8_t APU::kLengthTable[32] = {
    10, 254, 20, 2, 40, 4, 80, 6, 160, 8, 60, 10, 14, 12, 26, 14,
    12, 16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30,
};

const uint8_t APU::kDutyTable[4][8] = {
    {0, 1, 0, 0, 0, 0, 0, 0},   // 12.5%
    {0, 1, 1, 0, 0, 0, 0, 0},   // 25%
    {0, 1, 1, 1, 1, 0, 0, 0},   // 50%
    {1, 0, 0, 1, 1, 1, 1, 1},   // 75% (25% negated)
};

// NTSC, in CPU cycles
const uint16_t APU::kNoisePeriods[16] = {
    4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068,
};

void APU::reset() {
    *this = APU{};
}

// ------------------------------------------------------------ envelope

void APU::Envelope::clock() {
    if (start) {
        start = false;
        decay = 15;
        divider = volume;
        return;
    }
    if (divider == 0) {
        divider = volume;
        if (decay > 0) decay--;
        else if (loop) decay = 15;
    } else {
        divider--;
    }
}

// ------------------------------------------------------------ pulse

void APU::Pulse::clockTimer() {
    if (timer == 0) {
        timer = timerPeriod;
        dutyPos = (dutyPos + 1) & 7;
    } else {
        timer--;
    }
}

void APU::Pulse::clockLength() {
    if (!lengthHalt && lengthCounter > 0) lengthCounter--;
}

uint16_t APU::Pulse::sweepTarget() const {
    uint16_t change = timerPeriod >> sweepShift;
    if (!sweepNegate) return timerPeriod + change;
    // pulse 1 uses one's-complement negate (subtracts change+1)
    return timerPeriod - change - (isPulse1 ? 1 : 0);
}

void APU::Pulse::clockSweep() {
    if (sweepDivider == 0 && sweepEnable && sweepShift > 0 && !muted()) {
        timerPeriod = sweepTarget() & 0x7FF;
    }
    if (sweepDivider == 0 || sweepReload) {
        sweepDivider = sweepPeriod;
        sweepReload = false;
    } else {
        sweepDivider--;
    }
}

bool APU::Pulse::muted() const {
    return timerPeriod < 8 || (!sweepNegate && sweepTarget() > 0x7FF);
}

uint8_t APU::Pulse::out() const {
    if (!enabled || lengthCounter == 0 || muted()) return 0;
    return kDutyTable[duty][dutyPos] ? env.out() : 0;
}

// ------------------------------------------------------------ triangle

void APU::TriangleCh::clockTimer() {
    if (timer == 0) {
        timer = timerPeriod;
        if (lengthCounter > 0 && linearCounter > 0)
            seqPos = (seqPos + 1) & 31;
    } else {
        timer--;
    }
}

void APU::TriangleCh::clockLength() {
    if (!control && lengthCounter > 0) lengthCounter--;
}

void APU::TriangleCh::clockLinear() {
    if (linearReloadFlag) linearCounter = linearReload;
    else if (linearCounter > 0) linearCounter--;
    if (!control) linearReloadFlag = false;
}

uint8_t APU::TriangleCh::out() const {
    if (!enabled) return 0;
    // 32-step: 15..0 then 0..15
    uint8_t s = seqPos;
    return (s < 16) ? (15 - s) : (s - 16);
}

// ------------------------------------------------------------ noise

void APU::NoiseCh::clockTimer() {
    if (timer == 0) {
        timer = timerPeriod;
        uint16_t feedback = (lfsr & 1) ^ ((lfsr >> (mode ? 6 : 1)) & 1);
        lfsr >>= 1;
        lfsr |= feedback << 14;
    } else {
        timer--;
    }
}

void APU::NoiseCh::clockLength() {
    if (!lengthHalt && lengthCounter > 0) lengthCounter--;
}

uint8_t APU::NoiseCh::out() const {
    if (!enabled || lengthCounter == 0 || (lfsr & 1)) return 0;
    return env.out();
}

// ------------------------------------------------------------ frame counter

void APU::quarterFrame() {
    pulse1_.env.clock();
    pulse2_.env.clock();
    noise_.env.clock();
    triangle_.clockLinear();
}

void APU::halfFrame() {
    pulse1_.clockLength();
    pulse1_.clockSweep();
    pulse2_.clockLength();
    pulse2_.clockSweep();
    triangle_.clockLength();
    noise_.clockLength();
}

void APU::clockCpuCycle() {
    // channel timers
    triangle_.clockTimer();
    noise_.clockTimer();
    apuCycleToggle_ = !apuCycleToggle_;
    if (apuCycleToggle_) {
        pulse1_.clockTimer();
        pulse2_.clockTimer();
    }

    // frame sequencer (NTSC CPU-cycle thresholds)
    frameCycle_++;
    if (!mode5_) {
        switch (frameCycle_) {
            case 7457: quarterFrame(); break;
            case 14913: quarterFrame(); halfFrame(); break;
            case 22371: quarterFrame(); break;
            case 29829:
                quarterFrame(); halfFrame();
                if (!irqInhibit_) frameIrq_ = true;
                break;
            case 29830: frameCycle_ = 0; break;
        }
    } else {
        switch (frameCycle_) {
            case 7457: quarterFrame(); break;
            case 14913: quarterFrame(); halfFrame(); break;
            case 22371: quarterFrame(); break;
            case 37281: quarterFrame(); halfFrame(); break;
            case 37282: frameCycle_ = 0; break;
        }
    }
}

// ------------------------------------------------------------ registers

void APU::cpuWrite(uint16_t addr, uint8_t data) {
    switch (addr) {
        // pulse 1 / pulse 2
        case 0x4000: case 0x4004: {
            Pulse& p = (addr == 0x4000) ? pulse1_ : pulse2_;
            p.duty = (data >> 6) & 3;
            p.lengthHalt = data & 0x20;
            p.env.loop = data & 0x20;
            p.env.constant = data & 0x10;
            p.env.volume = data & 0x0F;
            break;
        }
        case 0x4001: case 0x4005: {
            Pulse& p = (addr == 0x4001) ? pulse1_ : pulse2_;
            p.sweepEnable = data & 0x80;
            p.sweepPeriod = (data >> 4) & 7;
            p.sweepNegate = data & 0x08;
            p.sweepShift = data & 7;
            p.sweepReload = true;
            break;
        }
        case 0x4002: case 0x4006: {
            Pulse& p = (addr == 0x4002) ? pulse1_ : pulse2_;
            p.timerPeriod = (p.timerPeriod & 0x0700) | data;
            break;
        }
        case 0x4003: case 0x4007: {
            Pulse& p = (addr == 0x4003) ? pulse1_ : pulse2_;
            p.timerPeriod = static_cast<uint16_t>((p.timerPeriod & 0x00FF) | ((data & 7) << 8));
            if (p.enabled) p.lengthCounter = kLengthTable[data >> 3];
            p.dutyPos = 0;
            p.env.start = true;
            break;
        }
        // triangle
        case 0x4008:
            triangle_.control = data & 0x80;
            triangle_.linearReload = data & 0x7F;
            break;
        case 0x400A:
            triangle_.timerPeriod = (triangle_.timerPeriod & 0x0700) | data;
            break;
        case 0x400B:
            triangle_.timerPeriod = static_cast<uint16_t>((triangle_.timerPeriod & 0x00FF) | ((data & 7) << 8));
            if (triangle_.enabled) triangle_.lengthCounter = kLengthTable[data >> 3];
            triangle_.linearReloadFlag = true;
            break;
        // noise
        case 0x400C:
            noise_.lengthHalt = data & 0x20;
            noise_.env.loop = data & 0x20;
            noise_.env.constant = data & 0x10;
            noise_.env.volume = data & 0x0F;
            break;
        case 0x400E:
            noise_.mode = data & 0x80;
            noise_.timerPeriod = kNoisePeriods[data & 0x0F];
            break;
        case 0x400F:
            if (noise_.enabled) noise_.lengthCounter = kLengthTable[data >> 3];
            noise_.env.start = true;
            break;
        // DMC stub
        case 0x4010: case 0x4011: case 0x4012: case 0x4013:
            break;
        // status
        case 0x4015:
            pulse1_.enabled = data & 0x01;
            pulse2_.enabled = data & 0x02;
            triangle_.enabled = data & 0x04;
            noise_.enabled = data & 0x08;
            if (!pulse1_.enabled) pulse1_.lengthCounter = 0;
            if (!pulse2_.enabled) pulse2_.lengthCounter = 0;
            if (!triangle_.enabled) triangle_.lengthCounter = 0;
            if (!noise_.enabled) noise_.lengthCounter = 0;
            break;
        // frame counter
        case 0x4017:
            mode5_ = data & 0x80;
            irqInhibit_ = data & 0x40;
            if (irqInhibit_) frameIrq_ = false;
            frameCycle_ = 0;
            if (mode5_) { quarterFrame(); halfFrame(); }
            break;
        default:
            break;
    }
}

uint8_t APU::cpuRead4015() {
    uint8_t data = 0;
    if (pulse1_.lengthCounter > 0) data |= 0x01;
    if (pulse2_.lengthCounter > 0) data |= 0x02;
    if (triangle_.lengthCounter > 0) data |= 0x04;
    if (noise_.lengthCounter > 0) data |= 0x08;
    if (frameIrq_) data |= 0x40;
    frameIrq_ = false;
    return data;
}

// ------------------------------------------------------------ mixer

float APU::output() const {
    // NesDev linear approximation
    float pulseOut = 0.00752f * (pulse1_.out() + pulse2_.out());
    float tndOut = 0.00851f * triangle_.out() + 0.00494f * noise_.out();
    return pulseOut + tndOut;
}
