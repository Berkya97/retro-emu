# NES + Game Boy / Game Boy Color Emulator

🇹🇷 [Türkçe README için tıklayın](README.tr.md)

A NES (Nintendo Entertainment System) and Game Boy / Game Boy Color emulator
written in C++17 with SDL2. Single executable; the correct core is selected
automatically from the ROM extension (.nes / .gb / .gbc). CGB-flagged games run
in full color, classic games run with the DMG green palette.

## Features

### NES core

- **CPU (6502/2A03)** — All official opcodes + stable unofficial opcodes (LAX,
  SAX, DCP, ISB, SLO, RLA, SRE, RRA, multi-byte NOPs). Per-instruction cycle
  counting including page-cross and branch penalties. NMI / IRQ / RESET.
  - ✅ nestest verification: all 8,991 lines match the reference log exactly
    (registers + cycle counter).
- **PPU (2C02)** — Dot-based scanline state machine, loopy v/t/x/w scroll
  registers, background shifters, sprite evaluation (8 sprites/line, overflow
  flag), sprite 0 hit, 8×16 sprites, OAM DMA (513/514-cycle CPU stall),
  odd-frame cycle skip.
  - ✅ blargg sprite_hit tests: 11/11 (including the timing tests).
- **APU (2A03)** — 2 pulse channels (duty, envelope, sweep, length), triangle
  (linear counter), noise (LFSR), 4/5-step frame counter + IRQ. NesDev linear
  mixer, fractional resampler to 44.1 kHz, SDL2 audio queue. Frame rate is
  locked to the audio clock (~60.0988 FPS).
  - ✅ blargg APU behavioral tests (len_ctr, len_table, irq_flag, reset_timing): passed.
  - ⚠️ APU timing tests requiring sub-instruction cycle accuracy (clock_jitter
    etc.) do not pass — a known limitation that does not affect games in
    practice. The DMC channel is a stub.
- **Mappers** — 0 (NROM), 1 (MMC1), 4 (MMC3 with simplified scanline IRQ).
  The `Mapper` interface is designed for adding more.
- **Input** — 2 controllers via keyboard.

### Game Boy / Game Boy Color core

- **CPU (SM83)** — All opcodes (256 base + 256 CB), interrupts, EI delay,
  HALT bug.
  - ✅ blargg cpu_instrs: 11/11 tests passed; instr_timing (cycle verification) passed.
- **PPU** — Mode 0-3 state machine, STAT/LYC interrupts, BG + window + sprites
  (10 sprites/line, DMG X-priority rule), 8×16 sprites, OAM DMA. Classic green
  palette in DMG mode.
  - ✅ dmg-acid2 visual test matches the reference image exactly.
- **APU** — CH1 pulse+sweep, CH2 pulse, CH3 wave RAM, CH4 noise; 512 Hz frame
  sequencer; NR50/NR51 mixer. (Tone frequency verified by measurement.)
- **Cartridge** — No MBC / MBC1 / MBC3 (no RTC) / MBC5; battery-backed
  external RAM is saved/loaded automatically as `<rom>.sav`.
- **Game Boy Color (CGB)** — 8 BG + 8 OBJ color palettes (BCPS/BCPD,
  OCPS/OCPD, RGB555), dual-bank VRAM + BG tile attributes (palette, bank,
  flips, priority), 8-bank WRAM (SVBK), double-speed mode (KEY1 + STOP), HDMA
  (general purpose + HBlank), CGB sprite priority rules (OAM order, LCDC bit 0
  master priority).
  - ✅ cgb-acid2 visual test matches the reference image exactly.
- Known limits: no link cable, no MBC3 RTC, fixed-length mode 3 approximation,
  no boot-ROM colorization for DMG games on CGB (they run with the green DMG
  palette).

## Building (Windows / MSYS2 UCRT64)

Requirements: CMake, MSYS2 UCRT64
(`pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-SDL2`).

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;C:\msys64\usr\bin;$env:PATH"
cmake -S . -B build -G "Unix Makefiles" `
    -DCMAKE_MAKE_PROGRAM=C:/msys64/usr/bin/make.exe `
    -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe `
    -DCMAKE_PREFIX_PATH=C:/msys64/ucrt64
cmake --build build -j
```

`SDL2.dll` is copied next to the executable automatically after the build.
The GCC runtime is statically linked, so the `build` folder is self-contained.

On Linux/macOS a plain `cmake -S . -B build && cmake --build build` with SDL2
development packages installed should work (the code has no Windows-specific
dependencies).

## Usage

```
build\nes.exe <rom.nes|rom.gb|rom.gbc>         # run a game in a window
build\nes.exe --nestest <rom.nes> <ref.log>    # verify the NES CPU against the nestest log
build\nes.exe <rom> --frames N [--dump out.bmp] [--text] [--wav out.wav] [--autostart]
                                               # headless test mode: run N frames;
                                               # dump the frame/audio, print the NES
                                               # nametable or GB serial output (--text)
```

### Controls

| NES / GB     | Controller 1  | Controller 2 (NES) |
|--------------|---------------|--------------------|
| D-Pad        | Arrow keys    | W A S D            |
| A            | X             | H                  |
| B            | Z             | G                  |
| Start        | Enter         | Y                  |
| Select       | Right Shift   | T                  |

`R` = reset, `Esc` = quit.

## Architecture

```
src/
├── main.cpp        SDL2 frontend + Machine interface (.nes/.gb dispatch), test modes
├── bus.h/.cpp      NES: CPU memory map, 3:1 PPU:CPU clock, OAM DMA, IRQ, audio resampler
├── cpu.h/.cpp      NES: 6502, 256-entry {addressing, operation, cycles} table
├── ppu.h/.cpp      NES: 2C02 dot-based renderer, PPU memory map, palette
├── apu.h/.cpp      NES: channels + frame counter + mixer
├── cartridge.h/.cpp NES: iNES loader
├── mapper.h        NES: mapper interface; mapper000/001/004 = NROM, MMC1, MMC3
└── gb/
    ├── gb.h/.cpp       GB machine: MMU, timers, joypad, serial, frame loop, CGB (speed/HDMA/WRAM banks)
    ├── gbcpu.h/.cpp    SM83 CPU
    ├── gbppu.h/.cpp    GB PPU (mode machine + scanline renderer, DMG+CGB)
    ├── gbapu.h/.cpp    GB APU (4 channels + frame sequencer)
    └── gbcart.h/.cpp   GB cartridge: MBC0/1/3/5 + .sav
```

Clocking: NES — `Bus::clock()` advances the PPU every tick and the CPU+APU
every 3rd tick. GB — the CPU executes an instruction, then the PPU/APU/timers
advance by the consumed T-cycles (halved in CGB double-speed mode). Audio
samples are produced with a fractional accumulator down to 44.1 kHz; the main
loop paces emulation by the SDL audio queue level, locking speed to real time.

## Test ROMs

Test ROMs are not included in the repository. The ones used for verification
are freely distributed:

- NES: [nestest, blargg sprite_hit & APU tests](https://github.com/christopherpow/nes-test-roms)
- GB: [blargg cpu_instrs & instr_timing](https://github.com/retrio/gb-test-roms),
  [dmg-acid2](https://github.com/mattcurrie/dmg-acid2),
  [cgb-acid2](https://github.com/mattcurrie/cgb-acid2)

Drop them into `roms/` and run the headless commands shown above.
