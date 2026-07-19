#include <SDL.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include "bus.h"
#include "gb/gb.h"

// ---------------------------------------------------------------- Machine
// Ortak konsol arayüzü: SDL ön yüzü (pencere/ses/klavye) bu arayüz üzerinden
// çalışır; .nes -> NesMachine, .gb -> GbMachine.

struct Machine {
    virtual ~Machine() = default;
    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual const uint32_t* framebuffer() const = 0;
    // NES bit düzeni: bit0=A bit1=B bit2=Select bit3=Start bit4-7=Up,Down,Left,Right
    virtual void setController(int idx, uint8_t state) = 0;
    virtual void runFrame(std::vector<int16_t>& audioOut) = 0;
    virtual void reset() = 0;
    virtual std::string debugText() { return {}; }
    virtual void onExit() {}
};

struct NesMachine : Machine {
    Bus bus;
    explicit NesMachine(std::shared_ptr<Cartridge> cart) {
        bus.insertCartridge(std::move(cart));
        bus.reset();
    }
    int width() const override { return 256; }
    int height() const override { return 240; }
    const uint32_t* framebuffer() const override { return bus.ppu.framebuffer(); }
    void setController(int idx, uint8_t state) override { bus.controller[idx & 1] = state; }
    void runFrame(std::vector<int16_t>& audioOut) override {
        do { bus.clock(); } while (!bus.ppu.frameComplete);
        bus.ppu.frameComplete = false;
        audioOut.swap(bus.audioBuffer);
        bus.audioBuffer.clear();
    }
    void reset() override { bus.reset(); }
    std::string debugText() override { return bus.ppu.nametableText(); }
};

struct GbMachine : Machine {
    GB gb;
    explicit GbMachine(std::shared_ptr<GbCart> cart) : gb(std::move(cart)) {}
    int width() const override { return 160; }
    int height() const override { return 144; }
    const uint32_t* framebuffer() const override { return gb.framebuffer(); }
    void setController(int idx, uint8_t state) override { if (idx == 0) gb.setButtons(state); }
    void runFrame(std::vector<int16_t>& audioOut) override {
        gb.runFrame();
        audioOut.swap(gb.audioBuffer);
        gb.audioBuffer.clear();
    }
    void reset() override { gb.reset(); }
    std::string debugText() override { return "serial: " + gb.serialLog() + "\n"; }
    void onExit() override { gb.cart()->saveBattery(); }
};

static bool hasExt(const std::string& path, const char* ext) {
    size_t dot = path.rfind('.');
    if (dot == std::string::npos) return false;
    std::string e = path.substr(dot);
    for (auto& ch : e) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return e == ext;
}

static std::unique_ptr<Machine> loadMachine(const char* romPath) {
    std::string path(romPath);
    if (hasExt(path, ".gb") || hasExt(path, ".gbc")) {
        auto cart = std::make_shared<GbCart>(path);
        if (!cart->valid()) {
            std::fprintf(stderr, "Failed to load '%s': %s\n", romPath, cart->error().c_str());
            return nullptr;
        }
        std::printf("Loaded %s: Game Boy \"%s\", MBC%d\n", romPath, cart->title().c_str(), cart->mbc());
        std::fflush(stdout);
        return std::make_unique<GbMachine>(std::move(cart));
    }
    auto cart = std::make_shared<Cartridge>(path);
    if (!cart->valid()) {
        std::fprintf(stderr, "Failed to load '%s': %s\n", romPath, cart->error().c_str());
        return nullptr;
    }
    std::printf("Loaded %s: NES mapper %d, PRG %d x 16KB, CHR %d x 8KB, %s mirroring\n",
        romPath, cart->mapperId(), cart->prgBanks(), cart->chrBanks(),
        cart->mirror() == Mirror::VERTICAL ? "vertical" : "horizontal");
    std::fflush(stdout);
    return std::make_unique<NesMachine>(std::move(cart));
}

// ---------------------------------------------------------------- nestest

// CPU'yu nestest referans loguna karşı doğrular (PC $C000'a zorlanır).
static int runNestest(const char* romPath, const char* logPath) {
    auto cart = std::make_shared<Cartridge>(romPath);
    if (!cart->valid()) {
        std::fprintf(stderr, "Failed to load '%s': %s\n", romPath, cart->error().c_str());
        return 1;
    }
    std::ifstream ref(logPath);
    if (!ref) {
        std::fprintf(stderr, "Cannot open reference log '%s'\n", logPath);
        return 1;
    }

    Bus bus;
    bus.insertCartridge(cart);
    bus.reset();
    bus.cpu.pc = 0xC000;
    while (!bus.cpu.complete()) bus.cpu.clock();

    auto hexAt = [](const std::string& s, const char* key) -> int {
        size_t p = s.find(key);
        if (p == std::string::npos) return -1;
        return static_cast<int>(std::stoul(s.substr(p + std::strlen(key), 2), nullptr, 16));
    };

    std::string line;
    int lineNo = 0;
    while (std::getline(ref, line)) {
        lineNo++;
        if (line.size() < 10) continue;
        int refPC = static_cast<int>(std::stoul(line.substr(0, 4), nullptr, 16));
        int refA = hexAt(line, " A:"), refX = hexAt(line, " X:"), refY = hexAt(line, " Y:");
        int refP = hexAt(line, " P:"), refSP = hexAt(line, " SP:");
        uint64_t refCYC = 0;
        size_t cp = line.find("CYC:");
        if (cp != std::string::npos) refCYC = std::stoull(line.substr(cp + 4));

        const CPU& c = bus.cpu;
        bool ok = c.pc == refPC && c.a == refA && c.x == refX && c.y == refY &&
                  c.status == refP && c.stkp == refSP && c.totalCycles() == refCYC;
        if (!ok) {
            std::printf("MISMATCH at line %d\n  ref: %s\n  got: %04X A:%02X X:%02X Y:%02X P:%02X SP:%02X CYC:%llu\n",
                lineNo, line.c_str(), c.pc, c.a, c.x, c.y, c.status, c.stkp,
                static_cast<unsigned long long>(c.totalCycles()));
            return 1;
        }
        bus.cpu.stepInstruction();
    }
    std::printf("nestest PASSED: %d lines matched\n", lineNo);
    return 0;
}

// ---------------------------------------------------------------- dumps

static bool writeWav(const char* path, const std::vector<int16_t>& pcm, int rate) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    uint32_t dataSize = static_cast<uint32_t>(pcm.size() * 2);
    uint8_t h[44] = {'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',
                     16,0,0,0, 1,0, 1,0, 0,0,0,0, 0,0,0,0, 2,0, 16,0,
                     'd','a','t','a',0,0,0,0};
    auto put32 = [&](int off, uint32_t v) {
        h[off] = v & 0xFF; h[off+1] = (v >> 8) & 0xFF; h[off+2] = (v >> 16) & 0xFF; h[off+3] = (v >> 24) & 0xFF;
    };
    put32(4, 36 + dataSize);
    put32(24, static_cast<uint32_t>(rate));
    put32(28, static_cast<uint32_t>(rate * 2));
    put32(40, dataSize);
    f.write(reinterpret_cast<char*>(h), 44);
    f.write(reinterpret_cast<const char*>(pcm.data()), dataSize);
    return f.good();
}

static bool writeBmp(const char* path, const uint32_t* argb, int w, int h) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    uint32_t imgSize = static_cast<uint32_t>(w) * h * 4;
    uint32_t fileSize = 54 + imgSize;
    uint8_t hdr[54] = {'B', 'M'};
    auto put32 = [&](int off, uint32_t v) {
        hdr[off] = v & 0xFF; hdr[off + 1] = (v >> 8) & 0xFF;
        hdr[off + 2] = (v >> 16) & 0xFF; hdr[off + 3] = (v >> 24) & 0xFF;
    };
    put32(2, fileSize); put32(10, 54); put32(14, 40);
    put32(18, static_cast<uint32_t>(w)); put32(22, static_cast<uint32_t>(-h));  // top-down
    hdr[26] = 1; hdr[28] = 32;
    put32(34, imgSize);
    f.write(reinterpret_cast<char*>(hdr), 54);
    f.write(reinterpret_cast<const char*>(argb), imgSize);
    return f.good();
}

// ---------------------------------------------------------------- main

int main(int argc, char** argv) {
    if (argc == 4 && std::strcmp(argv[1], "--nestest") == 0)
        return runNestest(argv[2], argv[3]);

    const char* romPath = nullptr;
    int headlessFrames = 0;
    const char* dumpPath = nullptr;
    bool autostart = false;
    bool dumpText = false;
    const char* wavPath = nullptr;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) headlessFrames = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--dump") == 0 && i + 1 < argc) dumpPath = argv[++i];
        else if (std::strcmp(argv[i], "--autostart") == 0) autostart = true;
        else if (std::strcmp(argv[i], "--text") == 0) dumpText = true;
        else if (std::strcmp(argv[i], "--wav") == 0 && i + 1 < argc) wavPath = argv[++i];
        else romPath = argv[i];
    }
    if (!romPath) {
        std::fprintf(stderr, "usage: nes <rom.nes|rom.gb> [--frames N --dump out.bmp --text --wav out.wav]\n"
                             "       nes --nestest <rom.nes> <ref.log>\n");
        return 1;
    }

    std::unique_ptr<Machine> machine = loadMachine(romPath);
    if (!machine) return 1;
    const int W = machine->width(), H = machine->height();
    const int scale = (W <= 160) ? 4 : 3;

    // Başsız test modu
    if (headlessFrames > 0) {
        std::vector<int16_t> audio, wav;
        for (int i = 0; i < headlessFrames; i++) {
            machine->setController(0, (autostart && i >= 30 && i < 40) ? 0x08 : 0x00);
            machine->runFrame(audio);
            if (wavPath) wav.insert(wav.end(), audio.begin(), audio.end());
            audio.clear();
        }
        if (dumpPath) {
            if (!writeBmp(dumpPath, machine->framebuffer(), W, H)) {
                std::fprintf(stderr, "failed to write %s\n", dumpPath);
                return 1;
            }
            std::printf("Dumped frame %d to %s\n", headlessFrames, dumpPath);
        }
        if (wavPath) {
            writeWav(wavPath, wav, 44100);
            double rms = 0;
            for (int16_t s : wav) rms += static_cast<double>(s) * s;
            rms = wav.empty() ? 0 : std::sqrt(rms / wav.size());
            std::printf("Wrote %zu samples to %s (RMS %.0f)\n", wav.size(), wavPath, rms);
        }
        if (dumpText) std::printf("--- debug ---\n%s", machine->debugText().c_str());
        machine->onExit();
        return 0;
    }

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow(W <= 160 ? "Game Boy" : "NES",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        W * scale, H * scale, SDL_WINDOW_SHOWN);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING, W, H);

    SDL_AudioSpec want{}, have{};
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 1024;
    SDL_AudioDeviceID audioDev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (audioDev) SDL_PauseAudioDevice(audioDev, 0);
    else std::fprintf(stderr, "audio unavailable: %s\n", SDL_GetError());
    const uint32_t kAudioTarget = 4 * (44100 / 60) * sizeof(int16_t);

    bool running = true;
    uint64_t perfFreq = SDL_GetPerformanceFrequency();
    double framePeriod = static_cast<double>(perfFreq) / 60.0;
    uint64_t nextFrame = SDL_GetPerformanceCounter();
    std::vector<int16_t> audio;

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = false;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) running = false;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_r) {
                machine->reset();
                if (audioDev) SDL_ClearQueuedAudio(audioDev);
            }
        }

        // Kumanda 1: oklar + Z/X + Enter/Sağ Shift; Kumanda 2 (NES): WASD + G/H + Y/T
        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        uint8_t c1 = 0, c2 = 0;
        if (keys[SDL_SCANCODE_X]) c1 |= 0x01;
        if (keys[SDL_SCANCODE_Z]) c1 |= 0x02;
        if (keys[SDL_SCANCODE_RSHIFT]) c1 |= 0x04;
        if (keys[SDL_SCANCODE_RETURN]) c1 |= 0x08;
        if (keys[SDL_SCANCODE_UP]) c1 |= 0x10;
        if (keys[SDL_SCANCODE_DOWN]) c1 |= 0x20;
        if (keys[SDL_SCANCODE_LEFT]) c1 |= 0x40;
        if (keys[SDL_SCANCODE_RIGHT]) c1 |= 0x80;
        if (keys[SDL_SCANCODE_H]) c2 |= 0x01;
        if (keys[SDL_SCANCODE_G]) c2 |= 0x02;
        if (keys[SDL_SCANCODE_T]) c2 |= 0x04;
        if (keys[SDL_SCANCODE_Y]) c2 |= 0x08;
        if (keys[SDL_SCANCODE_W]) c2 |= 0x10;
        if (keys[SDL_SCANCODE_S]) c2 |= 0x20;
        if (keys[SDL_SCANCODE_A]) c2 |= 0x40;
        if (keys[SDL_SCANCODE_D]) c2 |= 0x80;
        machine->setController(0, c1);
        machine->setController(1, c2);

        machine->runFrame(audio);

        SDL_UpdateTexture(texture, nullptr, machine->framebuffer(), W * 4);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);

        if (audioDev) {
            if (!audio.empty()) {
                SDL_QueueAudio(audioDev, audio.data(),
                    static_cast<Uint32>(audio.size() * sizeof(int16_t)));
            }
            while (SDL_GetQueuedAudioSize(audioDev) > kAudioTarget) SDL_Delay(1);
        } else {
            nextFrame += static_cast<uint64_t>(framePeriod);
            uint64_t now = SDL_GetPerformanceCounter();
            if (now < nextFrame) {
                uint32_t ms = static_cast<uint32_t>((nextFrame - now) * 1000 / perfFreq);
                if (ms > 0) SDL_Delay(ms);
            } else {
                nextFrame = now;
            }
        }
        audio.clear();
    }

    machine->onExit();   // pilli GB kartuşlarında .sav yaz
    if (audioDev) SDL_CloseAudioDevice(audioDev);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
