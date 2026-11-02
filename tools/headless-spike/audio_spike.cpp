// Spike: run the engine WITHOUT any window and play the sound through SDL2 audio (callback, ring buffer).
// Usage: audio_spike <entry.mr> [seconds] [out.wav]
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include "scripting/include/compiler.h"
#include "include/simulator.h"
#include "include/engine.h"
#include "include/ignition_module.h"
#include "include/impulse_response.h"
#include "include/exhaust_system.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

// ---- minimal portable wav reader (16 bit PCM, first channel) -------------------------------------
static bool readWav(const char *path, std::vector<int16_t> *out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char riff[12]; f.read(riff, 12);
    if (std::memcmp(riff, "RIFF", 4) || std::memcmp(riff + 8, "WAVE", 4)) return false;
    uint16_t channels = 1, bits = 16;
    while (f) {
        char id[4]; uint32_t size;
        if (!f.read(id, 4) || !f.read(reinterpret_cast<char *>(&size), 4)) break;
        if (!std::memcmp(id, "fmt ", 4)) {
            std::vector<char> b(size); f.read(b.data(), size);
            std::memcpy(&channels, b.data() + 2, 2); std::memcpy(&bits, b.data() + 14, 2);
        } else if (!std::memcmp(id, "data", 4)) {
            if (bits != 16) return false;
            std::vector<int16_t> raw(size / 2); f.read(reinterpret_cast<char *>(raw.data()), size);
            out->clear();
            for (size_t i = 0; i < raw.size(); i += channels) out->push_back(raw[i]);
            return true;
        } else f.seekg(size + (size & 1), std::ios::cur);
    }
    return false;
}

// ---- ring buffer between the simulation thread and the audio callback ----------------------------
struct Ring {
    std::vector<int16_t> data = std::vector<int16_t>(44100 * 2);
    size_t r = 0, w = 0; std::mutex m;
    size_t size() const { return (w + data.size() - r) % data.size(); }
    size_t space() const { return data.size() - 1 - size(); }
    void push(const int16_t *s, size_t n) { std::lock_guard<std::mutex> l(m); for (size_t i = 0; i < n && space(); ++i) { data[w] = s[i]; w = (w + 1) % data.size(); } }
    size_t pop(int16_t *d, size_t n) { std::lock_guard<std::mutex> l(m); size_t i = 0; for (; i < n && size(); ++i) { d[i] = data[r]; r = (r + 1) % data.size(); } return i; }
};
static Ring g_ring;
static std::atomic<int> g_underruns{0};
static std::atomic<long long> g_played{0};

static void audioCallback(void *, Uint8 *stream, int len) {
    int16_t *d = reinterpret_cast<int16_t *>(stream);
    const size_t n = len / 2;
    const size_t got = g_ring.pop(d, n);
    if (got < n) { std::memset(d + got, 0, (n - got) * 2); if (g_played > 22050) ++g_underruns; }
    g_played += got;
}

int main(int argc, char **argv) {
    const double seconds = argc > 2 ? std::atof(argv[2]) : 15.0;
    es_script::Compiler compiler; compiler.initialize();
    if (!compiler.compile(argv[1])) { std::printf("compile failed\n"); return 2; }
    auto out = compiler.execute(); if (!out.engine) return 3; compiler.destroy();
    Engine *e = out.engine;
    Simulator *sim = e->createSimulator(out.vehicle, out.transmission);
    sim->setSimulationFrequency(e->getSimulationFrequency());

    for (int i = 0; i < e->getExhaustSystemCount(); ++i) {
        ImpulseResponse *ir = e->getExhaustSystem(i)->getImpulseResponse();
        std::vector<int16_t> wav;
        if (!readWav(ir->getFilename().c_str(), &wav)) { std::printf("cannot read impulse response %s\n", ir->getFilename().c_str()); return 4; }
        sim->synthesizer().initializeImpulseResponse(wav.data(), (unsigned int)wav.size(), (float)ir->getVolume(), i);
        std::printf("impulse response %d: %s (%zu samples)\n", i, ir->getFilename().c_str(), wav.size());
    }
    sim->startAudioRenderingThread();
    e->getIgnitionModule()->m_enabled = true;

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_AUDIO) != 0) { std::printf("SDL_Init: %s\n", SDL_GetError()); return 5; }
    SDL_AudioSpec want{}, have{};
    want.freq = 44100; want.format = AUDIO_S16SYS; want.channels = 1; want.samples = 512; want.callback = audioCallback;
    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!dev) { std::printf("SDL_OpenAudioDevice: %s\n", SDL_GetError()); return 6; }
    std::printf("audio device: driver=%s freq=%d channels=%d buffer=%d samples\n", SDL_GetCurrentAudioDriver(), have.freq, (int)have.channels, (int)have.samples);

    std::vector<int16_t> recorded;
    auto t0 = std::chrono::steady_clock::now(), last = t0;
    bool started = false;
    double rmsAcc = 0; long long rmsN = 0; int peak = 0;
    std::vector<int16_t> buf;
    while (true) {
        const auto now = std::chrono::steady_clock::now();
        const double t = std::chrono::duration<double>(now - t0).count();
        const double dt = std::min(0.05, std::chrono::duration<double>(now - last).count());
        last = now;
        if (t > seconds) break;

        // simple scripted driver: crank for 2 s, idle, then rev up and down
        sim->m_starterMotor.m_enabled = t < 2.0;
        double thr = t < 2.0 ? 0.1 : (t < 6.0 ? 0.0 : (t < 10.0 ? 0.35 : 0.0));
        e->setSpeedControl(thr);

        sim->startFrame(dt);
        while (sim->simulateStep()) {}
        sim->endFrame();

        // keep ~0.1 s of sound queued
        const size_t queued = g_ring.size();
        const size_t want_n = queued < 4410 ? 4410 - queued + 1024 : 0;
        if (want_n) {
            buf.assign(want_n, 0);
            const int got = sim->readAudioOutput((int)want_n, buf.data());
            if (got > 0) {
                g_ring.push(buf.data(), got);
                for (int i = 0; i < got; ++i) { rmsAcc += (double)buf[i] * buf[i]; ++rmsN; peak = std::max(peak, std::abs((int)buf[i])); }
                if (argc > 3) recorded.insert(recorded.end(), buf.begin(), buf.begin() + got);
                if (!started) { SDL_PauseAudioDevice(dev, 0); started = true; }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
        static int tick = 0;
        if (++tick % 250 == 0) std::printf("t=%4.1fs rpm=%5.0f queued=%5zu underruns=%d\n", t, e->getRpm(), g_ring.size(), g_underruns.load());
    }
    SDL_PauseAudioDevice(dev, 1); SDL_CloseAudioDevice(dev); SDL_Quit();
    sim->destroy();
    std::printf("RESULT: %lld samples produced, rms=%.0f, peak=%d, underruns=%d, played=%lld\n", rmsN, rmsN ? std::sqrt(rmsAcc / rmsN) : 0.0, peak, g_underruns.load(), g_played.load());

    if (argc > 3) {                      // write a wav so the result can be checked offline
        std::ofstream f(argv[3], std::ios::binary);
        const uint32_t dataSize = (uint32_t)(recorded.size() * 2), chunk = 36 + dataSize, rate = 44100, byteRate = 88200, fmtSize = 16;
        const uint16_t pcm = 1, ch = 1, align = 2, bits = 16;
        f.write("RIFF", 4); f.write((char *)&chunk, 4); f.write("WAVEfmt ", 8); f.write((char *)&fmtSize, 4);
        f.write((char *)&pcm, 2); f.write((char *)&ch, 2); f.write((char *)&rate, 4); f.write((char *)&byteRate, 4);
        f.write((char *)&align, 2); f.write((char *)&bits, 2); f.write("data", 4); f.write((char *)&dataSize, 4);
        f.write((char *)recorded.data(), dataSize);
    }
    return 0;
}
