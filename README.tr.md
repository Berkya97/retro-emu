# NES + Game Boy / Game Boy Color Emülatörü

🇬🇧 [Click here for the English README](README.md)

C++17 / SDL2 ile yazılmış NES (Nintendo Entertainment System) ve Game Boy /
Game Boy Color emülatörü. Tek çalıştırılabilir dosya; ROM uzantısına göre
(.nes / .gb / .gbc) doğru çekirdek otomatik seçilir. CGB bayraklı oyunlar
renkli, klasik oyunlar DMG yeşili ile çalışır.

## Özellikler

### NES çekirdeği

- **CPU (6502/2A03)** — Tüm resmî opcode'lar + kararlı gayriresmî opcode'lar
  (LAX, SAX, DCP, ISB, SLO, RLA, SRE, RRA, çok baytlı NOP'lar). Komut bazlı
  cycle sayımı (sayfa aşımı ve branch cezaları dahil). NMI / IRQ / RESET.
  - ✅ nestest doğrulaması: 8.991 satırın tamamı referans logla birebir
    eşleşiyor (register + cycle sayacı).
- **PPU (2C02)** — Dot bazlı scanline durum makinesi, loopy v/t/x/w kaydırma
  kaydedicileri, arka plan shifterları, sprite değerlendirmesi (8 sprite/satır,
  overflow bayrağı), sprite 0 hit, 8×16 sprite modu, OAM DMA (513/514 cycle
  CPU stall), tek/çift kare cycle atlaması.
  - ✅ blargg sprite_hit testleri: 11/11 (zamanlama testleri dahil).
- **APU (2A03)** — 2 pulse (duty, envelope, sweep, length), triangle (linear
  counter), noise (LFSR), 4/5-step frame counter + IRQ. NesDev lineer mikser,
  44,1 kHz'e kesirli örnekleyici, SDL2 ses kuyruğu. Kare hızı ses saatine
  kilitli (~60,0988 FPS).
  - ✅ blargg APU davranış testleri (len_ctr, len_table, irq_flag, reset_timing): geçti.
  - ⚠️ Komut-altı cycle hassasiyeti isteyen APU zamanlama testleri
    (clock_jitter vb.) geçmez — bilinen sınırlama, oyunları pratikte
    etkilemez. DMC kanalı stub'dur.
- **Mapper'lar** — 0 (NROM), 1 (MMC1), 4 (MMC3, basitleştirilmiş scanline
  IRQ). `Mapper` arayüzü yeni mapper eklemeye uygun.
- **Giriş** — 2 kumanda, klavyeden.

### Game Boy / Game Boy Color çekirdeği

- **CPU (SM83)** — Tüm opcode'lar (256 temel + 256 CB), kesmeler, EI
  gecikmesi, HALT bug.
  - ✅ blargg cpu_instrs: 11/11 test geçti; instr_timing (cycle doğrulaması) geçti.
- **PPU** — Mode 0-3 durum makinesi, STAT/LYC kesmeleri, BG + pencere +
  sprite (10 sprite/satır, DMG X-öncelik kuralı), 8×16 sprite, OAM DMA. DMG
  modunda klasik yeşil palet.
  - ✅ dmg-acid2 görsel testi referansla birebir eşleşiyor.
- **APU** — CH1 pulse+sweep, CH2 pulse, CH3 wave RAM, CH4 noise; 512 Hz frame
  sequencer; NR50/NR51 mikser. (Ton frekansı ölçümle doğrulandı.)
- **Kartuş** — MBC yok / MBC1 / MBC3 (RTC hariç) / MBC5; pilli kartuşlarda
  harici RAM `<rom>.sav` dosyasına otomatik kaydedilir/yüklenir.
- **Game Boy Color (CGB)** — 8 BG + 8 OBJ renk paleti (BCPS/BCPD, OCPS/OCPD,
  RGB555), 2 banklı VRAM + BG tile öznitelikleri (palet, bank, flip, öncelik),
  8 banklı WRAM (SVBK), çift hız modu (KEY1 + STOP), HDMA (genel + HBlank),
  CGB sprite öncelik kuralları (OAM sırası, LCDC bit0 master priority).
  - ✅ cgb-acid2 görsel testi referansla birebir eşleşiyor.
- Bilinen sınırlar: seri bağlantı (link kablosu) yok, MBC3 RTC yok, mode-3
  süresi sabit yaklaşımlı, CGB'de DMG oyunları için boot-ROM renklendirmesi
  yok (yeşil DMG paletiyle çalışırlar).

## Derleme (Windows / MSYS2 UCRT64)

Gereksinimler: CMake, MSYS2 UCRT64
(`pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-SDL2`).

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;C:\msys64\usr\bin;$env:PATH"
cmake -S . -B build -G "Unix Makefiles" `
    -DCMAKE_MAKE_PROGRAM=C:/msys64/usr/bin/make.exe `
    -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe `
    -DCMAKE_PREFIX_PATH=C:/msys64/ucrt64
cmake --build build -j
```

`SDL2.dll` derleme sonrası otomatik olarak exe'nin yanına kopyalanır. GCC
çalışma zamanı statik bağlandığı için `build` klasörü kendi başına taşınabilir.

Linux/macOS'ta SDL2 geliştirme paketleri kuruluysa düz
`cmake -S . -B build && cmake --build build` çalışmalıdır (kodda Windows'a
özgü bağımlılık yok).

## Kullanım

```
build\nes.exe <rom.nes|rom.gb|rom.gbc>         # oyunu pencere modunda çalıştır
build\nes.exe --nestest <rom.nes> <ref.log>    # NES CPU'yu nestest loguna karşı doğrula
build\nes.exe <rom> --frames N [--dump out.bmp] [--text] [--wav out.wav] [--autostart]
                                               # başsız test modu: N kare çalıştır;
                                               # kare/ses dökümü, NES nametable veya
                                               # GB seri port çıktısı (--text)
```

### Tuşlar

| NES / GB     | Kumanda 1     | Kumanda 2 (NES) |
|--------------|---------------|-----------------|
| D-Pad        | Ok tuşları    | W A S D         |
| A            | X             | H               |
| B            | Z             | G               |
| Start        | Enter         | Y               |
| Select       | Sağ Shift     | T               |

`R` = reset, `Esc` = çıkış.

## Mimari

```
src/
├── main.cpp        SDL2 ön yüz + Machine arayüzü (.nes/.gb seçimi), test modları
├── bus.h/.cpp      NES: CPU adres haritası, 3:1 PPU:CPU saat, OAM DMA, IRQ, ses örnekleyici
├── cpu.h/.cpp      NES: 6502, 256 girişli {adresleme, işlem, cycle} tablosu
├── ppu.h/.cpp      NES: 2C02 dot bazlı render, PPU adres haritası, palet
├── apu.h/.cpp      NES: kanallar + frame counter + mikser
├── cartridge.h/.cpp NES: iNES yükleyici
├── mapper.h        NES: mapper arayüzü; mapper000/001/004 = NROM, MMC1, MMC3
└── gb/
    ├── gb.h/.cpp       GB makinesi: MMU, timer, joypad, seri port, kare döngüsü, CGB (hız/HDMA/WRAM bankları)
    ├── gbcpu.h/.cpp    SM83 CPU
    ├── gbppu.h/.cpp    GB PPU (mode makinesi + satır render, DMG+CGB)
    ├── gbapu.h/.cpp    GB APU (4 kanal + frame sequencer)
    └── gbcart.h/.cpp   GB kartuş: MBC0/1/3/5 + .sav
```

Saat düzeni: NES — `Bus::clock()` her tick'te PPU'yu, her 3. tick'te CPU+APU'yu
ilerletir. GB — CPU bir komut yürütür, harcanan T-cycle kadar PPU/APU/timer
ilerler (CGB çift hız modunda yarıya bölünür). Ses örnekleri kesirli
akümülatörle 44,1 kHz'e düşürülür; ana döngü SDL ses kuyruğunun doluluğuna
göre bekleyerek emülasyonu gerçek zamana kilitler.

## Test ROM'ları

Test ROM'ları depoya dahil değildir. Doğrulamada kullanılanlar serbestçe
dağıtılmaktadır:

- NES: [nestest, blargg sprite_hit & APU testleri](https://github.com/christopherpow/nes-test-roms)
- GB: [blargg cpu_instrs & instr_timing](https://github.com/retrio/gb-test-roms),
  [dmg-acid2](https://github.com/mattcurrie/dmg-acid2),
  [cgb-acid2](https://github.com/mattcurrie/cgb-acid2)

Dosyaları `roms/` klasörüne atıp yukarıdaki başsız test komutlarıyla
çalıştırabilirsiniz.
