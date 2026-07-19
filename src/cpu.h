#pragma once
#include <cstdint>

class Bus;

// MOS 6502 (2A03 variant: BCD flag exists but decimal mode is inert on NES —
// we still implement flag set/clear; ADC/SBC ignore D like the real 2A03).
// Cycle timing is per-instruction: base cycles + page-cross/branch penalties.
class CPU {
public:
    void connectBus(Bus* b) { bus_ = b; }

    void clock();          // one CPU cycle
    void reset();
    void irq();            // maskable
    void nmi();            // non-maskable

    bool complete() const { return cycles_ == 0; }
    void stepInstruction();  // run clocks until the current instruction ends

    uint64_t totalCycles() const { return totalCycles_; }

    // Registers (public for logging/debugging)
    uint8_t a = 0, x = 0, y = 0, stkp = 0;
    uint16_t pc = 0;
    uint8_t status = 0;

    enum Flag : uint8_t {
        C = 1 << 0,  // carry
        Z = 1 << 1,  // zero
        I = 1 << 2,  // interrupt disable
        D = 1 << 3,  // decimal (inert on NES)
        B = 1 << 4,  // break (only exists on the stack copy)
        U = 1 << 5,  // unused, reads as 1
        V = 1 << 6,  // overflow
        N = 1 << 7,  // negative
    };

private:
    Bus* bus_ = nullptr;
    uint8_t read(uint16_t addr);
    void write(uint16_t addr, uint8_t data);

    uint8_t getFlag(Flag f) const { return (status & f) ? 1 : 0; }
    void setFlag(Flag f, bool v) { if (v) status |= f; else status &= ~f; }
    void setZN(uint8_t v) { setFlag(Z, v == 0); setFlag(N, v & 0x80); }

    // Per-instruction working state
    uint8_t fetched_ = 0;
    uint16_t addrAbs_ = 0;
    uint16_t addrRel_ = 0;
    uint8_t opcode_ = 0;
    uint8_t cycles_ = 0;
    uint64_t totalCycles_ = 0;

    uint8_t fetch();

    // Addressing modes — return 1 if a page cross may cost an extra cycle
    uint8_t IMP(); uint8_t IMM(); uint8_t ZP0(); uint8_t ZPX();
    uint8_t ZPY(); uint8_t REL(); uint8_t ABS(); uint8_t ABX();
    uint8_t ABY(); uint8_t IND(); uint8_t IZX(); uint8_t IZY();

    // Operations — return 1 if they honor the addressing-mode extra cycle
    uint8_t ADC(); uint8_t AND(); uint8_t ASL(); uint8_t BCC();
    uint8_t BCS(); uint8_t BEQ(); uint8_t BIT(); uint8_t BMI();
    uint8_t BNE(); uint8_t BPL(); uint8_t BRK(); uint8_t BVC();
    uint8_t BVS(); uint8_t CLC(); uint8_t CLD(); uint8_t CLI();
    uint8_t CLV(); uint8_t CMP(); uint8_t CPX(); uint8_t CPY();
    uint8_t DEC(); uint8_t DEX(); uint8_t DEY(); uint8_t EOR();
    uint8_t INC(); uint8_t INX(); uint8_t INY(); uint8_t JMP();
    uint8_t JSR(); uint8_t LDA(); uint8_t LDX(); uint8_t LDY();
    uint8_t LSR(); uint8_t NOP(); uint8_t ORA(); uint8_t PHA();
    uint8_t PHP(); uint8_t PLA(); uint8_t PLP(); uint8_t ROL();
    uint8_t ROR(); uint8_t RTI(); uint8_t RTS(); uint8_t SBC();
    uint8_t SEC(); uint8_t SED(); uint8_t SEI(); uint8_t STA();
    uint8_t STX(); uint8_t STY(); uint8_t TAX(); uint8_t TAY();
    uint8_t TSX(); uint8_t TXA(); uint8_t TXS(); uint8_t TYA();

    // Stable unofficial opcodes (nestest exercises these)
    uint8_t LAX(); uint8_t SAX(); uint8_t DCP(); uint8_t ISB();
    uint8_t SLO(); uint8_t RLA(); uint8_t SRE(); uint8_t RRA();

    uint8_t XXX();  // unimplemented/unstable — behaves as NOP

    uint8_t branch(bool taken);  // shared branch logic

    struct Instruction {
        uint8_t (CPU::*addrmode)();
        uint8_t (CPU::*operate)();
        uint8_t cycles;
    };
    static const Instruction lookup_[256];
};
