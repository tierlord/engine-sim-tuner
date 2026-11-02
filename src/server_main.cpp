// Engine-Sim-Tuner server: the simulation without any window.
//
// Runs the engine, plays the sound through SDL audio and serves the HTTP interface (engine builder,
// dyno, controls, telemetry) on 127.0.0.1. Stop it with Ctrl+C.
//
//   engine-sim-server [--port N] [--throttle 0..1] [--exit-when-idle] [--grace S] [--startup-timeout S]
//                     [--open URL]
//
// Lifetime: every open GET /events stream is a user interface that is alive. With --exit-when-idle the
// server fades the sound out and quits when the last stream has been gone for --grace seconds (default 8,
// long enough for a page reload) or when none ever connected within --startup-timeout seconds (default 20).
// --open starts the interface (a Chromium based browser in app mode on Windows, the default browser elsewhere).
//
// Environment (same as the windowed application): ENGINE_SIM_LIVE_ENTRY, ENGINE_SIM_LIVE_FILE, ENGINE_SIM_LIVE_PORT
#ifndef NOMINMAX
#define NOMINMAX
#endif
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

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace {

std::atomic<bool> g_running{true};
void onSignal(int) { g_running = false; }

// Starts the user interface. Only loopback addresses are accepted.
void launchInterface(const std::string &url) {
    if (url.compare(0, 17, "http://127.0.0.1:") != 0 && url.compare(0, 17, "http://localhost:") != 0) {
        std::printf("[server] --open ignored: only http://127.0.0.1:... or http://localhost:... allowed\n");
        return;
    }

#ifdef _WIN32
    // Edge ships with Windows: its app mode gives a window without tabs and address bar
    const std::string arguments = "--app=" + url;
    const HINSTANCE result = ShellExecuteA(nullptr, "open", "msedge.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL);
    if ((INT_PTR)result <= 32) ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    const std::string command = "xdg-open '" + url + "' >/dev/null 2>&1 &";
    if (std::system(command.c_str()) != 0) std::printf("[server] could not start a browser, open %s yourself\n", url.c_str());
#endif
}

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
    bool exitWhenIdle = false;
    double graceSeconds = 8.0;
    double startupTimeout = 20.0;
    std::string openUrl;
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
        else if (std::strcmp(argv[i], "--exit-when-idle") == 0) {
            exitWhenIdle = true;
        }
        else if (std::strcmp(argv[i], "--grace") == 0 && i + 1 < argc) {
            graceSeconds = std::atof(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--startup-timeout") == 0 && i + 1 < argc) {
            startupTimeout = std::atof(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--open") == 0 && i + 1 < argc) {
            openUrl = argv[++i];
        }
        else {
            std::printf("usage: engine-sim-server [--port N] [--throttle 0..1] [--exit-when-idle] [--grace S] [--startup-timeout S] [--open URL]\n");
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

    if (!openUrl.empty()) launchInterface(openUrl);

    bool audioStarted = false;
    std::vector<int16_t> buffer;
    const auto startTime = std::chrono::steady_clock::now();
    auto last = startTime;
    auto idleSince = startTime;
    bool everConnected = false;

    // Fade-out: the sound is ramped down before the program ends, otherwise it would stop with a click
    constexpr double FadeSeconds = 0.4;
    bool fading = false;
    double gain = 1.0;
    const char *stopReason = "Ctrl+C";

    while (true) {
        const auto now = std::chrono::steady_clock::now();
        const double dt = std::min(0.05, std::chrono::duration<double>(now - last).count());
        last = now;

        session.updateLive((float)dt);

        // lifetime: an open event stream means a user interface is alive
        if (!fading) {
            if (!g_running) {
                fading = true;
            }
            else if (exitWhenIdle) {
                const int streams = session.liveServer().streamCount();
                const double sinceStart = std::chrono::duration<double>(now - startTime).count();
                if (streams > 0) {
                    everConnected = true;
                    idleSince = now;
                }
                else if (!everConnected && sinceStart > startupTimeout) {
                    stopReason = "no interface connected";
                    fading = true;
                }
                else if (everConnected && std::chrono::duration<double>(now - idleSince).count() > graceSeconds) {
                    stopReason = "interface closed";
                    fading = true;
                }
            }

            if (fading) std::printf("[server] stopping: %s\n", stopReason);
        }


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
                if (fading) {
                    for (int i = 0; i < got; ++i) {
                        buffer[i] = (int16_t)(buffer[i] * gain);
                        gain = std::max(0.0, gain - 1.0 / (FadeSeconds * 44100.0));
                    }
                }

                if (got > 0) {
                    g_ring.push(buffer.data(), (size_t)got);
                    if (!audioStarted) {
                        SDL_PauseAudioDevice(device, 0);
                        audioStarted = true;
                    }
                }
            }
        }

        if (fading && gain <= 0.0) {
            while (g_ring.size() > 0) std::this_thread::sleep_for(std::chrono::milliseconds(5));   // let the silence play out
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }

    std::printf("[server] stopped (audio underruns: %d)\n", g_underruns.load());
    SDL_PauseAudioDevice(device, 1);
    SDL_CloseAudioDevice(device);
    session.shutdown();
    SDL_Quit();
    return 0;
}
