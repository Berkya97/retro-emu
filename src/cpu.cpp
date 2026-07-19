#include "cpu.h"
#include "bus.h"

uint8_t CPU::read(uint16_t addr) { return bus_->cpuRead(addr); }
void CPU::write(uint16_t addr, uint8_t data) { bus_->cpuWrite(addr, data); }

// ---------------------------------------------------------------- dispatch

#define OP(am, op, cyc) { &CPU::am, &CPU::op, cyc }
const CPU::Instruction CPU::lookup_[256] = {
    /*00*/ OP(IMP,BRK,7), OP(IZX,ORA,6), OP(IMP,XXX,2), OP(IZX,SLO,8), OP(ZP0,NOP,3), OP(ZP0,ORA,3), OP(ZP0,ASL,5), OP(ZP0,SLO,5),
           OP(IMP,PHP,3), OP(IMM,ORA,2), OP(IMP,ASL,2), OP(IMM,XXX,2), OP(ABS,NOP,4), OP(ABS,ORA,4), OP(ABS,ASL,6), OP(ABS,SLO,6),
    /*10*/ OP(REL,BPL,2), OP(IZY,ORA,5), OP(IMP,XXX,2), OP(IZY,SLO,8), OP(ZPX,NOP,4), OP(ZPX,ORA,4), OP(ZPX,ASL,6), OP(ZPX,SLO,6),
           OP(IMP,CLC,2), OP(ABY,ORA,4), OP(IMP,NOP,2), OP(ABY,SLO,7), OP(ABX,NOP,4), OP(ABX,ORA,4), OP(ABX,ASL,7), OP(ABX,SLO,7),
    /*20*/ OP(ABS,JSR,6), OP(IZX,AND,6), OP(IMP,XXX,2), OP(IZX,RLA,8), OP(ZP0,BIT,3), OP(ZP0,AND,3), OP(ZP0,ROL,5), OP(ZP0,RLA,5),
           OP(IMP,PLP,4), OP(IMM,AND,2), OP(IMP,ROL,2), OP(IMM,XXX,2), OP(ABS,BIT,4), OP(ABS,AND,4), OP(ABS,ROL,6), OP(ABS,RLA,6),
    /*30*/ OP(REL,BMI,2), OP(IZY,AND,5), OP(IMP,XXX,2), OP(IZY,RLA,8), OP(ZPX,NOP,4), OP(ZPX,AND,4), OP(ZPX,ROL,6), OP(ZPX,RLA,6),
           OP(IMP,SEC,2), OP(ABY,AND,4), OP(IMP,NOP,2), OP(ABY,RLA,7), OP(ABX,NOP,4), OP(ABX,AND,4), OP(ABX,ROL,7), OP(ABX,RLA,7),
    /*40*/ OP(IMP,RTI,6), OP(IZX,EOR,6), OP(IMP,XXX,2), OP(IZX,SRE,8), OP(ZP0,NOP,3), OP(ZP0,EOR,3), OP(ZP0,LSR,5), OP(ZP0,SRE,5),
           OP(IMP,PHA,3), OP(IMM,EOR,2), OP(IMP,LSR,2), OP(IMM,XXX,2), OP(ABS,JMP,3), OP(ABS,EOR,4), OP(ABS,LSR,6), OP(ABS,SRE,6),
    /*50*/ OP(REL,BVC,2), OP(IZY,EOR,5), OP(IMP,XXX,2), OP(IZY,SRE,8), OP(ZPX,NOP,4), OP(ZPX,EOR,4), OP(ZPX,LSR,6), OP(ZPX,SRE,6),
           OP(IMP,CLI,2), OP(ABY,EOR,4), OP(IMP,NOP,2), OP(ABY,SRE,7), OP(ABX,NOP,4), OP(ABX,EOR,4), OP(ABX,LSR,7), OP(ABX,SRE,7),
    /*60*/ OP(IMP,RTS,6), OP(IZX,ADC,6), OP(IMP,XXX,2), OP(IZX,RRA,8), OP(ZP0,NOP,3), OP(ZP0,ADC,3), OP(ZP0,ROR,5), OP(ZP0,RRA,5),
           OP(IMP,PLA,4), OP(IMM,ADC,2), OP(IMP,ROR,2), OP(IMM,XXX,2), OP(IND,JMP,5), OP(ABS,ADC,4), OP(ABS,ROR,6), OP(ABS,RRA,6),
    /*70*/ OP(REL,BVS,2), OP(IZY,ADC,5), OP(IMP,XXX,2), OP(IZY,RRA,8), OP(ZPX,NOP,4), OP(ZPX,ADC,4), OP(ZPX,ROR,6), OP(ZPX,RRA,6),
           OP(IMP,SEI,2), OP(ABY,ADC,4), OP(IMP,NOP,2), OP(ABY,RRA,7), OP(ABX,NOP,4), OP(ABX,ADC,4), OP(ABX,ROR,7), OP(ABX,RRA,7),
    /*80*/ OP(IMM,NOP,2), OP(IZX,STA,6), OP(IMM,NOP,2), OP(IZX,SAX,6), OP(ZP0,STY,3), OP(ZP0,STA,3), OP(ZP0,STX,3), OP(ZP0,SAX,3),
           OP(IMP,DEY,2), OP(IMM,NOP,2), OP(IMP,TXA,2), OP(IMM,XXX,2), OP(ABS,STY,4), OP(ABS,STA,4), OP(ABS,STX,4), OP(ABS,SAX,4),
    /*90*/ OP(REL,BCC,2), OP(IZY,STA,6), OP(IMP,XXX,2), OP(IZY,XXX,6), OP(ZPX,STY,4), OP(ZPX,STA,4), OP(ZPY,STX,4), OP(ZPY,SAX,4),
           OP(IMP,TYA,2), OP(ABY,STA,5), OP(IMP,TXS,2), OP(ABY,XXX,5), OP(ABX,XXX,5), OP(ABX,STA,5), OP(ABY,XXX,5), OP(ABY,XXX,5),
    /*A0*/ OP(IMM,LDY,2), OP(IZX,LDA,6), OP(IMM,LDX,2), OP(IZX,LAX,6), OP(ZP0,LDY,3), OP(ZP0,LDA,3), OP(ZP0,LDX,3), OP(ZP0,LAX,3),
           OP(IMP,TAY,2), OP(IMM,LDA,2), OP(IMP,TAX,2), OP(IMM,XXX,2), OP(ABS,LDY,4), OP(ABS,LDA,4), OP(ABS,LDX,4), OP(ABS,LAX,4),
    /*B0*/ OP(REL,BCS,2), OP(IZY,LDA,5), OP(IMP,XXX,2), OP(IZY,LAX,5), OP(ZPX,LDY,4), OP(ZPX,LDA,4), OP(ZPY,LDX,4), OP(ZPY,LAX,4),
           OP(IMP,CLV,2), OP(ABY,LDA,4), OP(IMP,TSX,2), OP(ABY,XXX,4), OP(ABX,LDY,4), OP(ABX,LDA,4), OP(ABY,LDX,4), OP(ABY,LAX,4),
    /*C0*/ OP(IMM,CPY,2), OP(IZX,CMP,6), OP(IMM,NOP,2), OP(IZX,DCP,8), OP(ZP0,CPY,3), OP(ZP0,CMP,3), OP(ZP0,DEC,5), OP(ZP0,DCP,5),
           OP(IMP,INY,2), OP(IMM,CMP,2), OP(IMP,DEX,2), OP(IMM,XXX,2), OP(ABS,CPY,4), OP(ABS,CMP,4), OP(ABS,DEC,6), OP(ABS,DCP,6),
    /*D0*/ OP(REL,BNE,2), OP(IZY,CMP,5), OP(IMP,XXX,2), OP(IZY,DCP,8), OP(ZPX,NOP,4), OP(ZPX,CMP,4), OP(ZPX,DEC,6), OP(ZPX,DCP,6),
           OP(IMP,CLD,2), OP(ABY,CMP,4), OP(IMP,NOP,2), OP(ABY,DCP,7), OP(ABX,NOP,4), OP(ABX,CMP,4), OP(ABX,DEC,7), OP(ABX,DCP,7),
    /*E0*/ OP(IMM,CPX,2), OP(IZX,SBC,6), OP(IMM,NOP,2), OP(IZX,ISB,8), OP(ZP0,CPX,3), OP(ZP0,SBC,3), OP(ZP0,INC,5), OP(ZP0,ISB,5),
           OP(IMP,INX,2), OP(IMM,SBC,2), OP(IMP,NOP,2), OP(IMM,SBC,2), OP(ABS,CPX,4), OP(ABS,SBC,4), OP(ABS,INC,6), OP(ABS,ISB,6),
    /*F0*/ OP(REL,BEQ,2), OP(IZY,SBC,5), OP(IMP,XXX,2), OP(IZY,ISB,8), OP(ZPX,NOP,4), OP(ZPX,SBC,4), OP(ZPX,INC,6), OP(ZPX,ISB,6),
           OP(IMP,SED,2), OP(ABY,SBC,4), OP(IMP,NOP,2), OP(ABY,ISB,7), OP(ABX,NOP,4), OP(ABX,SBC,4), OP(ABX,INC,7), OP(ABX,ISB,7),
};
#undef OP

// ---------------------------------------------------------------- core

void CPU::clock() {
    if (cycles_ == 0) {
        opcode_ = read(pc++);
        setFlag(U, true);
        const Instruction& ins = lookup_[opcode_];
        cycles_ = ins.cycles;
        uint8_t extra1 = (this->*ins.addrmode)();
        uint8_t extra2 = (this->*ins.operate)();
        cycles_ += (extra1 & extra2);
        setFlag(U, true);
    }
    cycles_--;
    totalCycles_++;
}

void CPU::stepInstruction() {
    do { clock(); } while (cycles_ != 0);
}

void CPU::reset() {
    uint16_t lo = read(0xFFFC), hi = read(0xFFFD);
    pc = static_cast<uint16_t>((hi << 8) | lo);
    a = x = y = 0;
    stkp = 0xFD;
    status = U | I;
    addrAbs_ = addrRel_ = 0;
    fetched_ = 0;
    cycles_ = 7;          // reset sequence
    totalCycles_ = 0;
}

void CPU::irq() {
    if (getFlag(I)) return;
    write(0x0100 + stkp--, (pc >> 8) & 0xFF);
    write(0x0100 + stkp--, pc & 0xFF);
    write(0x0100 + stkp--, (status & ~B) | U);
    setFlag(I, true);
    uint16_t lo = read(0xFFFE), hi = read(0xFFFF);
    pc = static_cast<uint16_t>((hi << 8) | lo);
    cycles_ = 7;
}

void CPU::nmi() {
    write(0x0100 + stkp--, (pc >> 8) & 0xFF);
    write(0x0100 + stkp--, pc & 0xFF);
    write(0x0100 + stkp--, (status & ~B) | U);
    setFlag(I, true);
    uint16_t lo = read(0xFFFA), hi = read(0xFFFB);
    pc = static_cast<uint16_t>((hi << 8) | lo);
    cycles_ = 8;
}

uint8_t CPU::fetch() {
    if (lookup_[opcode_].addrmode != &CPU::IMP)
        fetched_ = read(addrAbs_);
    return fetched_;
}

// ---------------------------------------------------------------- addressing

uint8_t CPU::IMP() { fetched_ = a; return 0; }
uint8_t CPU::IMM() { addrAbs_ = pc++; return 0; }
uint8_t CPU::ZP0() { addrAbs_ = read(pc++); return 0; }
uint8_t CPU::ZPX() { addrAbs_ = (read(pc++) + x) & 0x00FF; return 0; }
uint8_t CPU::ZPY() { addrAbs_ = (read(pc++) + y) & 0x00FF; return 0; }

uint8_t CPU::REL() {
    addrRel_ = read(pc++);
    if (addrRel_ & 0x80) addrRel_ |= 0xFF00;
    return 0;
}

uint8_t CPU::ABS() {
    uint16_t lo = read(pc++), hi = read(pc++);
    addrAbs_ = static_cast<uint16_t>((hi << 8) | lo);
    return 0;
}

uint8_t CPU::ABX() {
    uint16_t lo = read(pc++), hi = read(pc++);
    addrAbs_ = static_cast<uint16_t>(((hi << 8) | lo) + x);
    return ((addrAbs_ & 0xFF00) != (hi << 8)) ? 1 : 0;
}

uint8_t CPU::ABY() {
    uint16_t lo = read(pc++), hi = read(pc++);
    addrAbs_ = static_cast<uint16_t>(((hi << 8) | lo) + y);
    return ((addrAbs_ & 0xFF00) != (hi << 8)) ? 1 : 0;
}

uint8_t CPU::IND() {
    uint16_t lo = read(pc++), hi = read(pc++);
    uint16_t ptr = static_cast<uint16_t>((hi << 8) | lo);
    // 6502 bug: the high byte read wraps within the page
    uint16_t hiAddr = (lo == 0x00FF) ? (ptr & 0xFF00) : ptr + 1;
    addrAbs_ = static_cast<uint16_t>((read(hiAddr) << 8) | read(ptr));
    return 0;
}

uint8_t CPU::IZX() {
    uint16_t t = read(pc++);
    uint16_t lo = read((t + x) & 0x00FF);
    uint16_t hi = read((t + x + 1) & 0x00FF);
    addrAbs_ = static_cast<uint16_t>((hi << 8) | lo);
    return 0;
}

uint8_t CPU::IZY() {
    uint16_t t = read(pc++);
    uint16_t lo = read(t & 0x00FF);
    uint16_t hi = read((t + 1) & 0x00FF);
    addrAbs_ = static_cast<uint16_t>(((hi << 8) | lo) + y);
    return ((addrAbs_ & 0xFF00) != (hi << 8)) ? 1 : 0;
}

// ---------------------------------------------------------------- operations

uint8_t CPU::ADC() {
    fetch();
    uint16_t sum = static_cast<uint16_t>(a) + fetched_ + getFlag(C);
    setFlag(C, sum > 0xFF);
    setFlag(V, (~(a ^ fetched_) & (a ^ sum)) & 0x80);
    a = sum & 0xFF;
    setZN(a);
    return 1;
}

uint8_t CPU::SBC() {
    fetch();
    uint16_t inv = fetched_ ^ 0xFF;
    uint16_t sum = static_cast<uint16_t>(a) + inv + getFlag(C);
    setFlag(C, sum > 0xFF);
    setFlag(V, (~(a ^ inv) & (a ^ sum)) & 0x80);
    a = sum & 0xFF;
    setZN(a);
    return 1;
}

uint8_t CPU::AND() { fetch(); a &= fetched_; setZN(a); return 1; }
uint8_t CPU::ORA() { fetch(); a |= fetched_; setZN(a); return 1; }
uint8_t CPU::EOR() { fetch(); a ^= fetched_; setZN(a); return 1; }

uint8_t CPU::ASL() {
    fetch();
    uint16_t r = static_cast<uint16_t>(fetched_) << 1;
    setFlag(C, r & 0x100);
    setZN(r & 0xFF);
    if (lookup_[opcode_].addrmode == &CPU::IMP) a = r & 0xFF;
    else write(addrAbs_, r & 0xFF);
    return 0;
}

uint8_t CPU::LSR() {
    fetch();
    setFlag(C, fetched_ & 0x01);
    uint8_t r = fetched_ >> 1;
    setZN(r);
    if (lookup_[opcode_].addrmode == &CPU::IMP) a = r;
    else write(addrAbs_, r);
    return 0;
}

uint8_t CPU::ROL() {
    fetch();
    uint16_t r = (static_cast<uint16_t>(fetched_) << 1) | getFlag(C);
    setFlag(C, r & 0x100);
    setZN(r & 0xFF);
    if (lookup_[opcode_].addrmode == &CPU::IMP) a = r & 0xFF;
    else write(addrAbs_, r & 0xFF);
    return 0;
}

uint8_t CPU::ROR() {
    fetch();
    uint8_t r = (fetched_ >> 1) | (getFlag(C) << 7);
    setFlag(C, fetched_ & 0x01);
    setZN(r);
    if (lookup_[opcode_].addrmode == &CPU::IMP) a = r;
    else write(addrAbs_, r);
    return 0;
}

uint8_t CPU::branch(bool taken) {
    if (taken) {
        cycles_++;
        addrAbs_ = pc + addrRel_;
        if ((addrAbs_ & 0xFF00) != (pc & 0xFF00)) cycles_++;
        pc = addrAbs_;
    }
    return 0;
}

uint8_t CPU::BCC() { return branch(!getFlag(C)); }
uint8_t CPU::BCS() { return branch(getFlag(C)); }
uint8_t CPU::BNE() { return branch(!getFlag(Z)); }
uint8_t CPU::BEQ() { return branch(getFlag(Z)); }
uint8_t CPU::BPL() { return branch(!getFlag(N)); }
uint8_t CPU::BMI() { return branch(getFlag(N)); }
uint8_t CPU::BVC() { return branch(!getFlag(V)); }
uint8_t CPU::BVS() { return branch(getFlag(V)); }

uint8_t CPU::BIT() {
    fetch();
    setFlag(Z, (a & fetched_) == 0);
    setFlag(N, fetched_ & 0x80);
    setFlag(V, fetched_ & 0x40);
    return 0;
}

uint8_t CPU::BRK() {
    pc++;
    write(0x0100 + stkp--, (pc >> 8) & 0xFF);
    write(0x0100 + stkp--, pc & 0xFF);
    write(0x0100 + stkp--, status | B | U);
    setFlag(I, true);
    pc = static_cast<uint16_t>(read(0xFFFE) | (read(0xFFFF) << 8));
    return 0;
}

uint8_t CPU::CLC() { setFlag(C, false); return 0; }
uint8_t CPU::CLD() { setFlag(D, false); return 0; }
uint8_t CPU::CLI() { setFlag(I, false); return 0; }
uint8_t CPU::CLV() { setFlag(V, false); return 0; }
uint8_t CPU::SEC() { setFlag(C, true); return 0; }
uint8_t CPU::SED() { setFlag(D, true); return 0; }
uint8_t CPU::SEI() { setFlag(I, true); return 0; }

uint8_t CPU::CMP() {
    fetch();
    uint16_t r = static_cast<uint16_t>(a) - fetched_;
    setFlag(C, a >= fetched_);
    setZN(r & 0xFF);
    return 1;
}
uint8_t CPU::CPX() {
    fetch();
    uint16_t r = static_cast<uint16_t>(x) - fetched_;
    setFlag(C, x >= fetched_);
    setZN(r & 0xFF);
    return 0;
}
uint8_t CPU::CPY() {
    fetch();
    uint16_t r = static_cast<uint16_t>(y) - fetched_;
    setFlag(C, y >= fetched_);
    setZN(r & 0xFF);
    return 0;
}

uint8_t CPU::DEC() { fetch(); uint8_t r = fetched_ - 1; write(addrAbs_, r); setZN(r); return 0; }
uint8_t CPU::INC() { fetch(); uint8_t r = fetched_ + 1; write(addrAbs_, r); setZN(r); return 0; }
uint8_t CPU::DEX() { x--; setZN(x); return 0; }
uint8_t CPU::DEY() { y--; setZN(y); return 0; }
uint8_t CPU::INX() { x++; setZN(x); return 0; }
uint8_t CPU::INY() { y++; setZN(y); return 0; }

uint8_t CPU::JMP() { pc = addrAbs_; return 0; }

uint8_t CPU::JSR() {
    pc--;
    write(0x0100 + stkp--, (pc >> 8) & 0xFF);
    write(0x0100 + stkp--, pc & 0xFF);
    pc = addrAbs_;
    return 0;
}

uint8_t CPU::RTS() {
    uint16_t lo = read(0x0100 + ++stkp);
    uint16_t hi = read(0x0100 + ++stkp);
    pc = static_cast<uint16_t>(((hi << 8) | lo) + 1);
    return 0;
}

uint8_t CPU::RTI() {
    status = read(0x0100 + ++stkp);
    status &= ~B;
    status |= U;
    uint16_t lo = read(0x0100 + ++stkp);
    uint16_t hi = read(0x0100 + ++stkp);
    pc = static_cast<uint16_t>((hi << 8) | lo);
    return 0;
}

uint8_t CPU::LDA() { fetch(); a = fetched_; setZN(a); return 1; }
uint8_t CPU::LDX() { fetch(); x = fetched_; setZN(x); return 1; }
uint8_t CPU::LDY() { fetch(); y = fetched_; setZN(y); return 1; }
uint8_t CPU::STA() { write(addrAbs_, a); return 0; }
uint8_t CPU::STX() { write(addrAbs_, x); return 0; }
uint8_t CPU::STY() { write(addrAbs_, y); return 0; }

uint8_t CPU::NOP() { return 1; }  // multi-byte NOPs take the page-cross penalty

uint8_t CPU::PHA() { write(0x0100 + stkp--, a); return 0; }
uint8_t CPU::PHP() { write(0x0100 + stkp--, status | B | U); return 0; }
uint8_t CPU::PLA() { a = read(0x0100 + ++stkp); setZN(a); return 0; }
uint8_t CPU::PLP() { status = (read(0x0100 + ++stkp) & ~B) | U; return 0; }

uint8_t CPU::TAX() { x = a; setZN(x); return 0; }
uint8_t CPU::TAY() { y = a; setZN(y); return 0; }
uint8_t CPU::TSX() { x = stkp; setZN(x); return 0; }
uint8_t CPU::TXA() { a = x; setZN(a); return 0; }
uint8_t CPU::TXS() { stkp = x; return 0; }
uint8_t CPU::TYA() { a = y; setZN(a); return 0; }

// -------- stable unofficial opcodes (combinations of official ops) --------

uint8_t CPU::LAX() { fetch(); a = x = fetched_; setZN(a); return 1; }
uint8_t CPU::SAX() { write(addrAbs_, a & x); return 0; }

uint8_t CPU::DCP() {  // DEC then CMP
    fetch();
    uint8_t r = fetched_ - 1;
    write(addrAbs_, r);
    setFlag(C, a >= r);
    setZN(static_cast<uint8_t>(a - r));
    return 0;
}

uint8_t CPU::ISB() {  // INC then SBC
    fetch();
    uint8_t r = fetched_ + 1;
    write(addrAbs_, r);
    uint16_t inv = r ^ 0xFF;
    uint16_t sum = static_cast<uint16_t>(a) + inv + getFlag(C);
    setFlag(C, sum > 0xFF);
    setFlag(V, (~(a ^ inv) & (a ^ sum)) & 0x80);
    a = sum & 0xFF;
    setZN(a);
    return 0;
}

uint8_t CPU::SLO() {  // ASL then ORA
    fetch();
    uint16_t r = static_cast<uint16_t>(fetched_) << 1;
    setFlag(C, r & 0x100);
    write(addrAbs_, r & 0xFF);
    a |= (r & 0xFF);
    setZN(a);
    return 0;
}

uint8_t CPU::RLA() {  // ROL then AND
    fetch();
    uint16_t r = (static_cast<uint16_t>(fetched_) << 1) | getFlag(C);
    setFlag(C, r & 0x100);
    write(addrAbs_, r & 0xFF);
    a &= (r & 0xFF);
    setZN(a);
    return 0;
}

uint8_t CPU::SRE() {  // LSR then EOR
    fetch();
    setFlag(C, fetched_ & 0x01);
    uint8_t r = fetched_ >> 1;
    write(addrAbs_, r);
    a ^= r;
    setZN(a);
    return 0;
}

uint8_t CPU::RRA() {  // ROR then ADC
    fetch();
    uint8_t r = (fetched_ >> 1) | (getFlag(C) << 7);
    setFlag(C, fetched_ & 0x01);
    write(addrAbs_, r);
    uint16_t sum = static_cast<uint16_t>(a) + r + getFlag(C);
    setFlag(C, sum > 0xFF);
    setFlag(V, (~(a ^ r) & (a ^ sum)) & 0x80);
    a = sum & 0xFF;
    setZN(a);
    return 0;
}

uint8_t CPU::XXX() { return 0; }
