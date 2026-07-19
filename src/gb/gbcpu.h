#pragma once
#include <cstdint>

class GB;

// Sharp SM83 (GB CPU, Z80 benzeri). step() bir komut yürütür ve harcanan
// T-cycle sayısını döndürür; makine bileşenleri o kadar ilerletilir.
class GbCpu {
public:
    void connect(GB* gb) { gb_ = gb; }
    void reset();
    int step();               // T-cycles

    uint8_t a = 0, f = 0, b = 0, c = 0, d = 0, e = 0, h = 0, l = 0;
    uint16_t sp = 0, pc = 0;
    bool ime = false;
    bool halted = false;

    enum Flag : uint8_t { FZ = 0x80, FN = 0x40, FH = 0x20, FC = 0x10 };

private:
    GB* gb_ = nullptr;
    bool imePending_ = false;   // EI bir komut gecikmeli
    bool haltBug_ = false;

    uint8_t rd(uint16_t addr);
    void wr(uint16_t addr, uint8_t v);
    uint8_t fetch8();
    uint16_t fetch16();
    void push16(uint16_t v);
    uint16_t pop16();

    uint16_t hl() const { return static_cast<uint16_t>((h << 8) | l); }
    uint16_t bc() const { return static_cast<uint16_t>((b << 8) | c); }
    uint16_t de() const { return static_cast<uint16_t>((d << 8) | e); }
    void setHl(uint16_t v) { h = v >> 8; l = v & 0xFF; }
    void setBc(uint16_t v) { b = v >> 8; c = v & 0xFF; }
    void setDe(uint16_t v) { d = v >> 8; e = v & 0xFF; }

    void setFlag(uint8_t fl, bool v) { if (v) f |= fl; else f &= ~fl; }

    uint8_t getR(int i);        // 0=B..5=L, 6=(HL), 7=A
    void setR(int i, uint8_t v);

    // ALU
    void alu(int op, uint8_t v);
    uint8_t inc8(uint8_t v);
    uint8_t dec8(uint8_t v);
    void addHl(uint16_t v);
    uint16_t addSpImm(int8_t ofs);
    void daa();

    // rotates/shifts (CB + akü varyantları)
    uint8_t rlc(uint8_t v); uint8_t rrc(uint8_t v);
    uint8_t rl(uint8_t v);  uint8_t rr(uint8_t v);
    uint8_t sla(uint8_t v); uint8_t sra(uint8_t v);
    uint8_t swap(uint8_t v); uint8_t srl(uint8_t v);

    int execute(uint8_t op);
    int executeCB();
    int serviceInterrupts();
};
