// Engine-Sim-Tuner server: the simulation without any window.
//
// Runs the engine, plays the sound through SDL audio and serves the HTTP interface (engine builder,
// dyno, controls, telemetry) on 127.0.0.1. Stop it with Ctrl+C.
//
//   engine-sim-server [--port N] [--throttle 0..1]
//
// Environment (same as the windowed application): ENGINE_SIM_LIVE_ENTRY, ENGINE_SIM_LIVE_FILE, ENGINE_SIM_LIVE_PORT
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include "../include/simulation_session.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

std::atomic<bool> g_running{true};
void onSignal(int) { g_running = false; }

// Samples travel from the simulation thread to the audio callback through this ring buffer
class AudioRing {
    public:
        size_t size() {
            std::lock_guard<std::mutex> lock(m_mutex);
            return sizeUnlocked();
        }

        void push(const int16_t *samples, size_t count) {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (size_t i = 0; i < count && sizeUnlocked() + 1 < m_data.size(); ++i) {
                m_data[m_write] = samples[i];
                m_write = (m_write + 1) % m_data.size();
            }
        }

        size_t pop(int16_t *target, size_t count) {
            std::lock_guard<std::mutex> lock(m_mutex);
            size_t i = 0;
            for (; i < count && sizeUnlocked() > 0; ++i) {
                target[i] = m_data[m_read];
                m_read = (m_read + 1) % m_data.size();
            }
            return i;
        }

    private:
        size_t sizeUnlocked() const { return (m_write + m_data.size() - m_read) % m_data.size(); }

        std::vector<int16_t> m_data = std::vector<int16_t>(44100 * 2);
        size_t m_read = 0;
        size_t m_write = 0;
        std::mutex m_mutex;
};

AudioRing g_ring;
std::atomic<int> g_underruns{0};

void audioCallback(void *, Uint8 *stream, int length) {
    int16_t *target = reinterpret_cast<int16_t *>(stream);
    const size_t wanted = (size_t)length / sizeof(int16_t);
    const size_t got = g_ring.pop(target, wanted);

    if (got < wanted) {
        std::memset(target + got, 0, (wanted - got) * sizeof(int16_t));
        ++g_underruns;
    }
}

}

int main(int argc, char **argv) {
    double throttle = 0.0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
#ifdef _WIN32
            _putenv_s("ENGINE_SIM_LIVE_PORT", argv[++i]);
#else
            setenv("ENGINE_SIM_LIVE_PORT", argv[++i], 1);
#endif
        }
        else if (std::strcmp(argv[i], "--throttle") == 0 && i + 1 < argc) {
            throttle = std::atof(argv[++i]);
        }
        else {
            std::printf("usage: engine-sim-server [--port N] [--throttle 0..1]\n");
            return 1;
        }
    }

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    SimulationSession session;
    session.callbacks.log = [](const std::string &message) { std::printf("[sim] %s\n", message.c_str()); std::fflush(stdout); };

    session.setAutoIgnition(true);

    // Live mode loads assets/live_entry.mr and starts the HTTP server
    session.toggleLive();
    session.input().throttle = throttle;

    if (SDL_Init(SDL_INIT_AUDIO) != 0) {
        std::printf("audio init failed: %s\n", SDL_GetError());
        return 2;
    }

    SDL_AudioSpec wanted{}, have{};
    wanted.freq = 44100;
    wanted.format = AUDIO_S16SYS;
    wanted.channels = 1;
    wanted.samples = 512;
    wanted.callback = audioCallback;
    const SDL_AudioDeviceID device = SDL_OpenAudioDevice(nullptr, 0, &wanted, &have, 0);
    if (device == 0) {
        std::printf("audio device failed: %s\n", SDL_GetError());
        return 3;
    }

    std::printf("[server] audio: %s, %d Hz | http://127.0.0.1:%d | Ctrl+C to stop\n",
        SDL_GetCurrentAudioDriver(), have.freq, session.liveServer().getPort());
    std::fflush(stdout);

    bool audioStarted = false;
    std::vector<int16_t> buffer;
    auto last = std::chrono::steady_clock::now();

    while (g_running) {
        const auto now = std::chrono::steady_clock::now();
        const double dt = std::min(0.05, std::chrono::duration<double>(now - last).count());
        last = now;

        session.updateLive((float)dt);

        if (session.engine() != nullptr && session.simulator() != nullptr) {
            session.updateControls(dt);
            session.updateDyno((float)dt);
            session.step(dt, 1.0, [&session]() {
                session.dynoSweep().sample(session.simulator(), session.engine());
            });

            // keep about 0.1 s of sound queued
            const size_t queued = g_ring.size();
            if (queued < 4410) {
                const size_t wantedSamples = 4410 - queued + 1024;
                buffer.assign(wantedSamples, 0);
                const int got = session.readAudio((int)wantedSamples, buffer.data());
                if (got > 0) {
                    g_ring.push(buffer.data(), (size_t)got);
                    if (!audioStarted) {
                        SDL_PauseAudioDevice(device, 0);
                        audioStarted = true;
                    }
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }

    std::printf("[server] stopping (audio underruns: %d)\n", g_underruns.load());
    SDL_PauseAudioDevice(device, 1);
    SDL_CloseAudioDevice(device);
    session.shutdown();
    SDL_Quit();
    return 0;
}
