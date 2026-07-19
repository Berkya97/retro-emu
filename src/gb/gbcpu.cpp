#include "gbcpu.h"
#include "gb.h"

void GbCpu::reset() {
    // DMG boot ROM sonrası kayıt değerleri
    a = 0x01; f = 0xB0;
    b = 0x00; c = 0x13;
    d = 0x00; e = 0xD8;
    h = 0x01; l = 0x4D;
    sp = 0xFFFE;
    pc = 0x0100;
    ime = false;
    imePending_ = false;
    halted = false;
    haltBug_ = false;
}

uint8_t GbCpu::rd(uint16_t addr) { return gb_->read(addr); }
void GbCpu::wr(uint16_t addr, uint8_t v) { gb_->write(addr, v); }

uint8_t GbCpu::fetch8() { return rd(pc++); }
uint16_t GbCpu::fetch16() {
    uint16_t lo = fetch8(), hi = fetch8();
    return static_cast<uint16_t>((hi << 8) | lo);
}
void GbCpu::push16(uint16_t v) { wr(--sp, v >> 8); wr(--sp, v & 0xFF); }
uint16_t GbCpu::pop16() {
    uint16_t lo = rd(sp++), hi = rd(sp++);
    return static_cast<uint16_t>((hi << 8) | lo);
}

uint8_t GbCpu::getR(int i) {
    switch (i) {
        case 0: return b; case 1: return c; case 2: return d; case 3: return e;
        case 4: return h; case 5: return l; case 6: return rd(hl()); default: return a;
    }
}
void GbCpu::setR(int i, uint8_t v) {
    switch (i) {
        case 0: b = v; break; case 1: c = v; break; case 2: d = v; break;
        case 3: e = v; break; case 4: h = v; break; case 5: l = v; break;
        case 6: wr(hl(), v); break; default: a = v; break;
    }
}

// ---------------------------------------------------------------- ALU

void GbCpu::alu(int op, uint8_t v) {
    switch (op) {
        case 0: {  // ADD
            uint16_t r = a + v;
            setFlag(FH, (a & 0xF) + (v & 0xF) > 0xF);
            setFlag(FC, r > 0xFF);
            a = r & 0xFF;
            setFlag(FZ, a == 0); setFlag(FN, false);
            break;
        }
        case 1: {  // ADC
            uint8_t cy = (f & FC) ? 1 : 0;
            uint16_t r = a + v + cy;
            setFlag(FH, (a & 0xF) + (v & 0xF) + cy > 0xF);
            setFlag(FC, r > 0xFF);
            a = r & 0xFF;
            setFlag(FZ, a == 0); setFlag(FN, false);
            break;
        }
        case 2: {  // SUB
            setFlag(FH, (a & 0xF) < (v & 0xF));
            setFlag(FC, a < v);
            a -= v;
            setFlag(FZ, a == 0); setFlag(FN, true);
            break;
        }
        case 3: {  // SBC
            uint8_t cy = (f & FC) ? 1 : 0;
            int r = a - v - cy;
            setFlag(FH, (a & 0xF) < (v & 0xF) + cy);
            setFlag(FC, r < 0);
            a = static_cast<uint8_t>(r);
            setFlag(FZ, a == 0); setFlag(FN, true);
            break;
        }
        case 4:  // AND
            a &= v;
            f = FH | (a == 0 ? FZ : 0);
            break;
        case 5:  // XOR
            a ^= v;
            f = (a == 0 ? FZ : 0);
            break;
        case 6:  // OR
            a |= v;
            f = (a == 0 ? FZ : 0);
            break;
        case 7: {  // CP
            setFlag(FZ, a == v);
            setFlag(FN, true);
            setFlag(FH, (a & 0xF) < (v & 0xF));
            setFlag(FC, a < v);
            break;
        }
    }
}

uint8_t GbCpu::inc8(uint8_t v) {
    uint8_t r = v + 1;
    setFlag(FZ, r == 0); setFlag(FN, false); setFlag(FH, (v & 0xF) == 0xF);
    return r;
}
uint8_t GbCpu::dec8(uint8_t v) {
    uint8_t r = v - 1;
    setFlag(FZ, r == 0); setFlag(FN, true); setFlag(FH, (v & 0xF) == 0);
    return r;
}

void GbCpu::addHl(uint16_t v) {
    uint32_t r = hl() + v;
    setFlag(FN, false);
    setFlag(FH, (hl() & 0x0FFF) + (v & 0x0FFF) > 0x0FFF);
    setFlag(FC, r > 0xFFFF);
    setHl(r & 0xFFFF);
}

uint16_t GbCpu::addSpImm(int8_t ofs) {
    uint16_t r = static_cast<uint16_t>(sp + ofs);
    setFlag(FZ, false); setFlag(FN, false);
    setFlag(FH, (sp & 0x0F) + (static_cast<uint8_t>(ofs) & 0x0F) > 0x0F);
    setFlag(FC, (sp & 0xFF) + (static_cast<uint8_t>(ofs) & 0xFF) > 0xFF);
    return r;
}

void GbCpu::daa() {
    int adj = 0;
    bool carry = f & FC;
    if (!(f & FN)) {
        if ((f & FH) || (a & 0x0F) > 9) adj |= 0x06;
        if (carry || a > 0x99) { adj |= 0x60; carry = true; }
        a += adj;
    } else {
        if (f & FH) adj |= 0x06;
        if (carry) adj |= 0x60;
        a -= adj;
    }
    setFlag(FZ, a == 0);
    setFlag(FH, false);
    setFlag(FC, carry);
}

uint8_t GbCpu::rlc(uint8_t v) {
    uint8_t r = static_cast<uint8_t>((v << 1) | (v >> 7));
    f = ((v & 0x80) ? FC : 0) | (r == 0 ? FZ : 0);
    return r;
}
uint8_t GbCpu::rrc(uint8_t v) {
    uint8_t r = static_cast<uint8_t>((v >> 1) | (v << 7));
    f = ((v & 1) ? FC : 0) | (r == 0 ? FZ : 0);
    return r;
}
uint8_t GbCpu::rl(uint8_t v) {
    uint8_t r = static_cast<uint8_t>((v << 1) | ((f & FC) ? 1 : 0));
    f = ((v & 0x80) ? FC : 0) | (r == 0 ? FZ : 0);
    return r;
}
uint8_t GbCpu::rr(uint8_t v) {
    uint8_t r = static_cast<uint8_t>((v >> 1) | ((f & FC) ? 0x80 : 0));
    f = ((v & 1) ? FC : 0) | (r == 0 ? FZ : 0);
    return r;
}
uint8_t GbCpu::sla(uint8_t v) {
    uint8_t r = static_cast<uint8_t>(v << 1);
    f = ((v & 0x80) ? FC : 0) | (r == 0 ? FZ : 0);
    return r;
}
uint8_t GbCpu::sra(uint8_t v) {
    uint8_t r = static_cast<uint8_t>((v >> 1) | (v & 0x80));
    f = ((v & 1) ? FC : 0) | (r == 0 ? FZ : 0);
    return r;
}
uint8_t GbCpu::swap(uint8_t v) {
    uint8_t r = static_cast<uint8_t>((v << 4) | (v >> 4));
    f = (r == 0 ? FZ : 0);
    return r;
}
uint8_t GbCpu::srl(uint8_t v) {
    uint8_t r = v >> 1;
    f = ((v & 1) ? FC : 0) | (r == 0 ? FZ : 0);
    return r;
}

// ---------------------------------------------------------------- interrupts

int GbCpu::serviceInterrupts() {
    uint8_t pending = gb_->interruptEnable() & gb_->interruptFlags() & 0x1F;
    if (!pending) return 0;
    halted = false;
    if (!ime) return 0;
    ime = false;
    int bit = 0;
    while (!(pending & (1 << bit))) bit++;
    gb_->clearInterrupt(bit);
    push16(pc);
    pc = static_cast<uint16_t>(0x40 + bit * 8);
    return 20;
}

// ---------------------------------------------------------------- step

int GbCpu::step() {
    int icycles = serviceInterrupts();
    if (icycles) return icycles;

    if (halted) return 4;

    bool enableIme = imePending_;
    imePending_ = false;

    uint8_t op = rd(pc);
    if (haltBug_) haltBug_ = false;   // PC bir kez ilerlemez
    else pc++;

    int cycles = execute(op);

    if (enableIme) ime = true;
    return cycles;
}

int GbCpu::execute(uint8_t op) {
    // 0x40-0x7F: LD r,r' (0x76 = HALT)
    if (op >= 0x40 && op <= 0x7F && op != 0x76) {
        int dst = (op >> 3) & 7, src = op & 7;
        setR(dst, getR(src));
        return (dst == 6 || src == 6) ? 8 : 4;
    }
    // 0x80-0xBF: ALU A,r
    if (op >= 0x80 && op <= 0xBF) {
        alu((op >> 3) & 7, getR(op & 7));
        return (op & 7) == 6 ? 8 : 4;
    }

    switch (op) {
        case 0x00: return 4;                                     // NOP
        case 0x10: pc++; gb_->onStop(); return 4;                // STOP: CGB hız değişimi
        case 0x76:                                               // HALT
            if (!ime && (gb_->interruptEnable() & gb_->interruptFlags() & 0x1F))
                haltBug_ = true;
            else halted = true;
            return 4;

        // 16-bit LD
        case 0x01: setBc(fetch16()); return 12;
        case 0x11: setDe(fetch16()); return 12;
        case 0x21: setHl(fetch16()); return 12;
        case 0x31: sp = fetch16(); return 12;
        case 0x08: {                                             // LD (a16),SP
            uint16_t addr = fetch16();
            wr(addr, sp & 0xFF); wr(addr + 1, sp >> 8);
            return 20;
        }
        case 0xF9: sp = hl(); return 8;                          // LD SP,HL
        case 0xF8: setHl(addSpImm(static_cast<int8_t>(fetch8()))); return 12;  // LD HL,SP+r8
        case 0xE8: sp = addSpImm(static_cast<int8_t>(fetch8())); return 16;    // ADD SP,r8

        // 8-bit LD (indirect)
        case 0x02: wr(bc(), a); return 8;
        case 0x12: wr(de(), a); return 8;
        case 0x22: wr(hl(), a); setHl(hl() + 1); return 8;
        case 0x32: wr(hl(), a); setHl(hl() - 1); return 8;
        case 0x0A: a = rd(bc()); return 8;
        case 0x1A: a = rd(de()); return 8;
        case 0x2A: a = rd(hl()); setHl(hl() + 1); return 8;
        case 0x3A: a = rd(hl()); setHl(hl() - 1); return 8;

        // LD r,d8
        case 0x06: b = fetch8(); return 8;
        case 0x0E: c = fetch8(); return 8;
        case 0x16: d = fetch8(); return 8;
        case 0x1E: e = fetch8(); return 8;
        case 0x26: h = fetch8(); return 8;
        case 0x2E: l = fetch8(); return 8;
        case 0x36: wr(hl(), fetch8()); return 12;
        case 0x3E: a = fetch8(); return 8;

        // INC/DEC 8-bit
        case 0x04: b = inc8(b); return 4;
        case 0x0C: c = inc8(c); return 4;
        case 0x14: d = inc8(d); return 4;
        case 0x1C: e = inc8(e); return 4;
        case 0x24: h = inc8(h); return 4;
        case 0x2C: l = inc8(l); return 4;
        case 0x34: wr(hl(), inc8(rd(hl()))); return 12;
        case 0x3C: a = inc8(a); return 4;
        case 0x05: b = dec8(b); return 4;
        case 0x0D: c = dec8(c); return 4;
        case 0x15: d = dec8(d); return 4;
        case 0x1D: e = dec8(e); return 4;
        case 0x25: h = dec8(h); return 4;
        case 0x2D: l = dec8(l); return 4;
        case 0x35: wr(hl(), dec8(rd(hl()))); return 12;
        case 0x3D: a = dec8(a); return 4;

        // INC/DEC 16-bit
        case 0x03: setBc(bc() + 1); return 8;
        case 0x13: setDe(de() + 1); return 8;
        case 0x23: setHl(hl() + 1); return 8;
        case 0x33: sp++; return 8;
        case 0x0B: setBc(bc() - 1); return 8;
        case 0x1B: setDe(de() - 1); return 8;
        case 0x2B: setHl(hl() - 1); return 8;
        case 0x3B: sp--; return 8;

        // ADD HL,rr
        case 0x09: addHl(bc()); return 8;
        case 0x19: addHl(de()); return 8;
        case 0x29: addHl(hl()); return 8;
        case 0x39: addHl(sp); return 8;

        // akü rotasyonları (Z her zaman 0)
        case 0x07: a = rlc(a); f &= ~FZ; return 4;
        case 0x0F: a = rrc(a); f &= ~FZ; return 4;
        case 0x17: a = rl(a); f &= ~FZ; return 4;
        case 0x1F: a = rr(a); f &= ~FZ; return 4;

        case 0x27: daa(); return 4;
        case 0x2F: a = ~a; setFlag(FN, true); setFlag(FH, true); return 4;   // CPL
        case 0x37: setFlag(FN, false); setFlag(FH, false); setFlag(FC, true); return 4;  // SCF
        case 0x3F: setFlag(FN, false); setFlag(FH, false); setFlag(FC, !(f & FC)); return 4;  // CCF

        // JR
        case 0x18: { int8_t o = static_cast<int8_t>(fetch8()); pc += o; return 12; }
        case 0x20: { int8_t o = static_cast<int8_t>(fetch8()); if (!(f & FZ)) { pc += o; return 12; } return 8; }
        case 0x28: { int8_t o = static_cast<int8_t>(fetch8()); if (f & FZ) { pc += o; return 12; } return 8; }
        case 0x30: { int8_t o = static_cast<int8_t>(fetch8()); if (!(f & FC)) { pc += o; return 12; } return 8; }
        case 0x38: { int8_t o = static_cast<int8_t>(fetch8()); if (f & FC) { pc += o; return 12; } return 8; }

        // JP
        case 0xC3: pc = fetch16(); return 16;
        case 0xC2: { uint16_t t = fetch16(); if (!(f & FZ)) { pc = t; return 16; } return 12; }
        case 0xCA: { uint16_t t = fetch16(); if (f & FZ) { pc = t; return 16; } return 12; }
        case 0xD2: { uint16_t t = fetch16(); if (!(f & FC)) { pc = t; return 16; } return 12; }
        case 0xDA: { uint16_t t = fetch16(); if (f & FC) { pc = t; return 16; } return 12; }
        case 0xE9: pc = hl(); return 4;                          // JP HL

        // CALL / RET / RST
        case 0xCD: { uint16_t t = fetch16(); push16(pc); pc = t; return 24; }
        case 0xC4: { uint16_t t = fetch16(); if (!(f & FZ)) { push16(pc); pc = t; return 24; } return 12; }
        case 0xCC: { uint16_t t = fetch16(); if (f & FZ) { push16(pc); pc = t; return 24; } return 12; }
        case 0xD4: { uint16_t t = fetch16(); if (!(f & FC)) { push16(pc); pc = t; return 24; } return 12; }
        case 0xDC: { uint16_t t = fetch16(); if (f & FC) { push16(pc); pc = t; return 24; } return 12; }
        case 0xC9: pc = pop16(); return 16;                      // RET
        case 0xD9: pc = pop16(); ime = true; return 16;          // RETI
        case 0xC0: if (!(f & FZ)) { pc = pop16(); return 20; } return 8;
        case 0xC8: if (f & FZ) { pc = pop16(); return 20; } return 8;
        case 0xD0: if (!(f & FC)) { pc = pop16(); return 20; } return 8;
        case 0xD8: if (f & FC) { pc = pop16(); return 20; } return 8;
        case 0xC7: case 0xCF: case 0xD7: case 0xDF:
        case 0xE7: case 0xEF: case 0xF7: case 0xFF:
            push16(pc); pc = op & 0x38; return 16;               // RST

        // PUSH / POP
        case 0xC5: push16(bc()); return 16;
        case 0xD5: push16(de()); return 16;
        case 0xE5: push16(hl()); return 16;
        case 0xF5: push16(static_cast<uint16_t>((a << 8) | f)); return 16;
        case 0xC1: setBc(pop16()); return 12;
        case 0xD1: setDe(pop16()); return 12;
        case 0xE1: setHl(pop16()); return 12;
        case 0xF1: { uint16_t v = pop16(); a = v >> 8; f = v & 0xF0; return 12; }

        // ALU A,d8
        case 0xC6: alu(0, fetch8()); return 8;
        case 0xCE: alu(1, fetch8()); return 8;
        case 0xD6: alu(2, fetch8()); return 8;
        case 0xDE: alu(3, fetch8()); return 8;
        case 0xE6: alu(4, fetch8()); return 8;
        case 0xEE: alu(5, fetch8()); return 8;
        case 0xF6: alu(6, fetch8()); return 8;
        case 0xFE: alu(7, fetch8()); return 8;

        // LDH / LD (C) / LD (a16)
        case 0xE0: wr(0xFF00 + fetch8(), a); return 12;
        case 0xF0: a = rd(0xFF00 + fetch8()); return 12;
        case 0xE2: wr(0xFF00 + c, a); return 8;
        case 0xF2: a = rd(0xFF00 + c); return 8;
        case 0xEA: wr(fetch16(), a); return 16;
        case 0xFA: a = rd(fetch16()); return 16;

        // IME
        case 0xF3: ime = false; imePending_ = false; return 4;   // DI
        case 0xFB: imePending_ = true; return 4;                 // EI

        case 0xCB: return executeCB();

        default: return 4;   // tanımsız opcode'lar (D3, E3...): NOP gibi
    }
}

int GbCpu::executeCB() {
    uint8_t op = fetch8();
    int x = op >> 6, y = (op >> 3) & 7, z = op & 7;
    bool mem = (z == 6);

    if (x == 0) {
        uint8_t v = getR(z), r = 0;
        switch (y) {
            case 0: r = rlc(v); break;
            case 1: r = rrc(v); break;
            case 2: r = rl(v); break;
            case 3: r = rr(v); break;
            case 4: r = sla(v); break;
            case 5: r = sra(v); break;
            case 6: r = swap(v); break;
            case 7: r = srl(v); break;
        }
        setR(z, r);
        return mem ? 16 : 8;
    }
    if (x == 1) {  // BIT y,r
        uint8_t v = getR(z);
        setFlag(FZ, !(v & (1 << y)));
        setFlag(FN, false);
        setFlag(FH, true);
        return mem ? 12 : 8;
    }
    if (x == 2) {  // RES
        setR(z, getR(z) & ~(1 << y));
        return mem ? 16 : 8;
    }
    // SET
    setR(z, getR(z) | (1 << y));
    return mem ? 16 : 8;
}
