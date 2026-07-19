#include "gbapu.h"

static const uint8_t kDuty[4][8] = {
    {0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 1, 1, 1},
    {0, 1, 1, 1, 1, 1, 1, 0},
};

void GbApu::reset() { *this = GbApu{}; }

void GbApu::Envelope::clock() {
    if (period == 0) return;
    if (timer > 0) timer--;
    if (timer == 0) {
        timer = period;
        if (up && volume < 15) volume++;
        else if (!up && volume > 0) volume--;
    }
}

// ---------------------------------------------------------------- square

void GbApu::Square::stepTimer(int t) {
    if (!enabled) return;
    timer -= t;
    while (timer <= 0) {
        timer += (2048 - freq) * 4;
        dutyPos = (dutyPos + 1) & 7;
    }
}

void GbApu::Square::clockLength() {
    if (lengthEnable && lengthCounter > 0 && --lengthCounter == 0) enabled = false;
}

uint16_t GbApu::Square::sweepCalc() {
    uint16_t d = sweepShadow >> sweepShift;
    uint16_t next = sweepNegate ? sweepShadow - d : sweepShadow + d;
    if (next > 2047) enabled = false;
    return next;
}

void GbApu::Square::clockSweep() {
    if (sweepTimer > 0) sweepTimer--;
    if (sweepTimer == 0) {
        sweepTimer = sweepPeriod ? sweepPeriod : 8;
        if (sweepOn && sweepPeriod) {
            uint16_t next = sweepCalc();
            if (next <= 2047 && sweepShift) {
                sweepShadow = next;
                freq = next;
                sweepCalc();   // ikinci taşma kontrolü
            }
        }
    }
}

int GbApu::Square::out() const {
    if (!enabled || !dacOn) return 0;
    return kDuty[duty][dutyPos] ? env.volume : 0;
}

// ---------------------------------------------------------------- wave

void GbApu::Wave::stepTimer(int t) {
    if (!enabled) return;
    timer -= t;
    while (timer <= 0) {
        timer += (2048 - freq) * 2;
        pos = (pos + 1) & 31;
    }
}

void GbApu::Wave::clockLength() {
    if (lengthEnable && lengthCounter > 0 && --lengthCounter == 0) enabled = false;
}

int GbApu::Wave::out() const {
    if (!enabled || !dacOn || volumeCode == 0) return 0;
    uint8_t s = ram[pos / 2];
    s = (pos & 1) ? (s & 0x0F) : (s >> 4);
    return s >> (volumeCode - 1);
}

// ---------------------------------------------------------------- noise

void GbApu::Noise::stepTimer(int t) {
    if (!enabled) return;
    static const int kDiv[8] = {8, 16, 32, 48, 64, 80, 96, 112};
    timer -= t;
    while (timer <= 0) {
        timer += kDiv[divisorCode] << clockShift;
        uint16_t bit = (lfsr ^ (lfsr >> 1)) & 1;
        lfsr >>= 1;
        lfsr |= bit << 14;
        if (widthMode) {
            lfsr &= ~(1 << 6);
            lfsr |= bit << 6;
        }
    }
}

void GbApu::Noise::clockLength() {
    if (lengthEnable && lengthCounter > 0 && --lengthCounter == 0) enabled = false;
}

int GbApu::Noise::out() const {
    if (!enabled || !dacOn) return 0;
    return (lfsr & 1) ? 0 : env.volume;
}

// ---------------------------------------------------------------- APU

void GbApu::step(int tcycles) {
    if (!powerOn_) return;
    ch1_.stepTimer(tcycles);
    ch2_.stepTimer(tcycles);
    ch3_.stepTimer(tcycles);
    ch4_.stepTimer(tcycles);

    seqTimer_ += tcycles;
    while (seqTimer_ >= 8192) {
        seqTimer_ -= 8192;
        // adımlar: 0,2,4,6 length; 2,6 sweep; 7 envelope
        if ((seqStep_ & 1) == 0) {
            ch1_.clockLength(); ch2_.clockLength();
            ch3_.clockLength(); ch4_.clockLength();
        }
        if (seqStep_ == 2 || seqStep_ == 6) ch1_.clockSweep();
        if (seqStep_ == 7) {
            ch1_.env.clock(); ch2_.env.clock(); ch4_.env.clock();
        }
        seqStep_ = (seqStep_ + 1) & 7;
    }
}

float GbApu::output() const {
    if (!powerOn_) return 0.0f;
    // NR51 panning: mono çıkışta L/R ortalaması
    float mix = 0.0f;
    int outs[4] = {ch1_.out(), ch2_.out(), ch3_.out(), ch4_.out()};
    for (int i = 0; i < 4; i++) {
        float dac = outs[i] / 15.0f;   // 0..1
        int lr = ((nr51_ >> i) & 1) + ((nr51_ >> (i + 4)) & 1);
        mix += dac * lr * 0.5f;
    }
    float volL = ((nr50_ >> 4) & 7) / 7.0f, volR = (nr50_ & 7) / 7.0f;
    return mix * 0.25f * (volL + volR) * 0.5f;
}

uint8_t GbApu::readReg(uint16_t addr) {
    if (addr >= 0xFF30 && addr <= 0xFF3F) return ch3_.ram[addr - 0xFF30];
    switch (addr) {
        case 0xFF26: {
            uint8_t v = powerOn_ ? 0x80 : 0x00;
            if (ch1_.enabled) v |= 1;
            if (ch2_.enabled) v |= 2;
            if (ch3_.enabled) v |= 4;
            if (ch4_.enabled) v |= 8;
            return v | 0x70;
        }
        case 0xFF24: return nr50_;
        case 0xFF25: return nr51_;
        default: return 0xFF;   // basitleştirme: tam okuma maskeleri atlandı
    }
}

void GbApu::writeReg(uint16_t addr, uint8_t v) {
    if (addr == 0xFF26) {
        bool on = v & 0x80;
        if (!on && powerOn_) { uint8_t save[16]; for (int i=0;i<16;i++) save[i]=ch3_.ram[i]; *this = GbApu{}; for (int i=0;i<16;i++) ch3_.ram[i]=save[i]; powerOn_ = false; }
        else if (on) powerOn_ = true;
        return;
    }
    if (!powerOn_) return;
    if (addr >= 0xFF30 && addr <= 0xFF3F) { ch3_.ram[addr - 0xFF30] = v; return; }

    switch (addr) {
        // CH1
        case 0xFF10:
            ch1_.sweepPeriod = (v >> 4) & 7;
            ch1_.sweepNegate = v & 8;
            ch1_.sweepShift = v & 7;
            break;
        case 0xFF11:
            ch1_.duty = v >> 6;
            ch1_.lengthCounter = 64 - (v & 0x3F);
            break;
        case 0xFF12:
            ch1_.env.initial = v >> 4;
            ch1_.env.up = v & 8;
            ch1_.env.period = v & 7;
            ch1_.dacOn = (v & 0xF8) != 0;
            if (!ch1_.dacOn) ch1_.enabled = false;
            break;
        case 0xFF13: ch1_.freq = (ch1_.freq & 0x700) | v; break;
        case 0xFF14:
            ch1_.freq = static_cast<uint16_t>((ch1_.freq & 0xFF) | ((v & 7) << 8));
            ch1_.lengthEnable = v & 0x40;
            if (v & 0x80) {
                ch1_.enabled = ch1_.dacOn;
                if (ch1_.lengthCounter == 0) ch1_.lengthCounter = 64;
                ch1_.timer = (2048 - ch1_.freq) * 4;
                ch1_.env.trigger();
                ch1_.sweepShadow = ch1_.freq;
                ch1_.sweepTimer = ch1_.sweepPeriod ? ch1_.sweepPeriod : 8;
                ch1_.sweepOn = ch1_.sweepPeriod || ch1_.sweepShift;
                if (ch1_.sweepShift) ch1_.sweepCalc();
            }
            break;
        // CH2
        case 0xFF16:
            ch2_.duty = v >> 6;
            ch2_.lengthCounter = 64 - (v & 0x3F);
            break;
        case 0xFF17:
            ch2_.env.initial = v >> 4;
            ch2_.env.up = v & 8;
            ch2_.env.period = v & 7;
            ch2_.dacOn = (v & 0xF8) != 0;
            if (!ch2_.dacOn) ch2_.enabled = false;
            break;
        case 0xFF18: ch2_.freq = (ch2_.freq & 0x700) | v; break;
        case 0xFF19:
            ch2_.freq = static_cast<uint16_t>((ch2_.freq & 0xFF) | ((v & 7) << 8));
            ch2_.lengthEnable = v & 0x40;
            if (v & 0x80) {
                ch2_.enabled = ch2_.dacOn;
                if (ch2_.lengthCounter == 0) ch2_.lengthCounter = 64;
                ch2_.timer = (2048 - ch2_.freq) * 4;
                ch2_.env.trigger();
            }
            break;
        // CH3
        case 0xFF1A:
            ch3_.dacOn = v & 0x80;
            if (!ch3_.dacOn) ch3_.enabled = false;
            break;
        case 0xFF1B: ch3_.lengthCounter = 256 - v; break;
        case 0xFF1C: ch3_.volumeCode = (v >> 5) & 3; break;
        case 0xFF1D: ch3_.freq = (ch3_.freq & 0x700) | v; break;
        case 0xFF1E:
            ch3_.freq = static_cast<uint16_t>((ch3_.freq & 0xFF) | ((v & 7) << 8));
            ch3_.lengthEnable = v & 0x40;
            if (v & 0x80) {
                ch3_.enabled = ch3_.dacOn;
                if (ch3_.lengthCounter == 0) ch3_.lengthCounter = 256;
                ch3_.timer = (2048 - ch3_.freq) * 2;
                ch3_.pos = 0;
            }
            break;
        // CH4
        case 0xFF20: ch4_.lengthCounter = 64 - (v & 0x3F); break;
        case 0xFF21:
            ch4_.env.initial = v >> 4;
            ch4_.env.up = v & 8;
            ch4_.env.period = v & 7;
            ch4_.dacOn = (v & 0xF8) != 0;
            if (!ch4_.dacOn) ch4_.enabled = false;
            break;
        case 0xFF22:
            ch4_.clockShift = v >> 4;
            ch4_.widthMode = v & 8;
            ch4_.divisorCode = v & 7;
            break;
        case 0xFF23:
            ch4_.lengthEnable = v & 0x40;
            if (v & 0x80) {
                ch4_.enabled = ch4_.dacOn;
                if (ch4_.lengthCounter == 0) ch4_.lengthCounter = 64;
                static const int kDiv[8] = {8, 16, 32, 48, 64, 80, 96, 112};
                ch4_.timer = kDiv[ch4_.divisorCode] << ch4_.clockShift;
                ch4_.lfsr = 0x7FFF;
                ch4_.env.trigger();
            }
            break;
        case 0xFF24: nr50_ = v; break;
        case 0xFF25: nr51_ = v; break;
        default: break;
    }
}
