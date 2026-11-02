#ifndef ATG_ENGINE_SIM_SIMULATION_SESSION_H
#define ATG_ENGINE_SIM_SIMULATION_SESSION_H

#include "application_settings.h"
#include "dyno_sweep.h"
#include "engine.h"
#include "live_server.h"
#include "simulator.h"
#include "transmission.h"
#include "vehicle.h"

#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

// Everything that does not need a window: script loading, the running simulator, live tuning,
// the virtual dyno, the HTTP server of the engine builder and the audio output of the synthesizer.
//
// The windowed application (and later the windowless server) only drive it: step() every frame,
// readAudio() to fetch the sound, updateLive() to serve the engine builder.
class SimulationSession {
    public:
        struct Callbacks {
            std::function<void(const std::string &)> log;                     // short status messages
            std::function<void()> beforeEngineReplaced;                       // old engine is about to be destroyed
            std::function<void()> engineLoaded;                               // a new engine/simulator is ready
            std::function<void(const ApplicationSettings &)> settings;        // script changed application settings
            std::function<void()> reloadBegin;                                // live reload starts (e.g. mute audio)
            std::function<void()> reloadEnd;                                  // live reload finished
        };

        struct StepResult {
            int iterations = 0;
            double seconds = 0.0;      // wall clock time spent in the simulation steps
        };

        // What the driver does: set by the keyboard of the windowed application or by the server
        struct DriverInput {
            double throttle = 1.0;             // wanted speed control 0..1, smoothed in updateControls()
            bool starter = false;
            double clutchTarget = 1.0;         // 0 = pedal pressed, 1 = engaged
            double clutchTimeConstant = 0.001; // seconds, how fast the clutch follows
        };

        SimulationSession();
        ~SimulationSession();

        Callbacks callbacks;

        // ---- engine -------------------------------------------------------------------------------
        bool loadScript(const std::string &path = "../assets/main.mr", bool keepOnFailure = false);
        bool reloadScript();
        void shutdown();                                       // stops the audio thread and frees the simulator

        // ---- live mode (engine builder) -----------------------------------------------------------
        void toggleLive();
        bool isLive() const { return m_live; }
        void updateLive(float dt);                             // file watcher, server requests, cam messages

        // ---- driver controls ----------------------------------------------------------------------
        DriverInput &input() { return m_input; }
        void setAutoIgnition(bool enabled) { m_autoIgnition = enabled; }   // switch the ignition on whenever an engine is loaded
        void updateControls(double dt);                        // call once per frame after changing input()
        void toggleDyno();
        void toggleDynoHold();
        void adjustDynoHold(int direction);                    // -1, 0, +1 steps of the hold speed
        void toggleIgnition();
        void shift(int delta);                                 // +1 up, -1 down (-1 gear = neutral)
        double dynoSpeed() const { return m_dynoSpeed; }
        double smoothedThrottle() const { return m_speedSetting; }
        double clutchPressure() const { return m_clutchPressure; }

        // ---- simulation ---------------------------------------------------------------------------
        // dt: real time of the frame; perStep is called after every simulation step
        StepResult step(double dt, double simulationSpeed, const std::function<void()> &perStep);
        void updateDyno(float dt) { m_dynoSweep.update(dt, m_iceEngine, m_simulator); }
        int readAudio(int samples, int16_t *target);
        std::string telemetryJson();                           // current values as JSON (GET /telemetry, /events)

        Engine *engine() const { return m_iceEngine; }
        Simulator *simulator() const { return m_simulator; }
        Vehicle *vehicle() const { return m_vehicle; }
        Transmission *transmission() const { return m_transmission; }
        DynoSweep &dynoSweep() { return m_dynoSweep; }
        LiveServer &liveServer() { return m_liveServer; }
        const ApplicationSettings &applicationSettings() const { return m_applicationSettings; }

    private:
        void loadEngine(Engine *engine, Vehicle *vehicle, Transmission *transmission);
        void loadImpulseResponses();
        std::string applyLiveTuning(const std::string &path);                       // all values of a script header
        std::string applyLiveValues(const std::map<std::string, std::string> &values, bool partial);
        std::string tuningJson() const;
        std::string handleLiveRequest(const std::string &body);
        std::string handleLiveRoute(const std::string &method, const std::string &path, const std::string &body);
        std::string handleControl(const std::string &body);
        void log(const std::string &message);

        Engine *m_iceEngine = nullptr;
        Vehicle *m_vehicle = nullptr;
        Transmission *m_transmission = nullptr;
        Simulator *m_simulator = nullptr;
        ApplicationSettings m_applicationSettings;

        DriverInput m_input;
        double m_speedSetting = 1.0;           // smoothed throttle
        double m_loggedThrottle = 1.0;
        double m_clutchPressure = 1.0;
        double m_dynoSpeed = 0.0;
        bool m_lastStarter = false;
        bool m_autoIgnition = false;

        // Live mode: watches the file written by tools/engine-builder.html
        bool m_live = false;
        bool m_livePending = false;
        float m_livePollTimer = 0.0f;
        float m_publishTimer = 0.0f;           // telemetry goes to the open /events streams every 50 ms
        std::filesystem::file_time_type m_liveStamp;
        DynoSweep m_dynoSweep;                 // automatic dyno run on the running simulator
        LiveServer m_liveServer;               // lets tools/engine-builder.html push engines without a file picker
        std::deque<std::vector<Function *>> m_liveLobes;   // cam lobes rebuilt by applyLiveTuning (the newest are in use)
        std::string m_lastCamLow;                          // last "cam_low" / "cam_high" values, to skip identical updates
        std::string m_lastCamHigh;
        bool m_lastHighLift = false;                       // for the "CAM: HIGH LIFT" message
        int m_lastRepairCounter = -1;          // "engine_repair" counter of the engine builder
        std::map<std::string, std::string> m_tuning;        // live values currently in effect (GET /tune)
        std::string m_liveStructure;           // structure hash of the engine currently loaded in live mode
        Function *m_liveTimingCurve = nullptr; // timing curve created by applyLiveTuning
};

#endif /* ATG_ENGINE_SIM_SIMULATION_SESSION_H */
