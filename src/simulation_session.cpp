#include "../include/simulation_session.h"

#include "../include/cam_lobe.h"
#include "../include/exhaust_system.h"
#include "../include/impulse_response.h"
#include "../include/piston_engine_simulator.h"
#include "../include/units.h"
#include "../include/utilities.h"
#include "../include/valvetrain.h"

#include "../scripting/include/compiler.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

// ENGINE_SIM_LIVE_ENTRY / ENGINE_SIM_LIVE_FILE let a second instance (e.g. for tests) use its own files
static std::string liveEntryScript() {
    const char *value = std::getenv("ENGINE_SIM_LIVE_ENTRY");
    return (value != nullptr && value[0] != 0) ? value : "../assets/live_entry.mr";
}

static std::string liveEngineFile() {
    const char *value = std::getenv("ENGINE_SIM_LIVE_FILE");
    return (value != nullptr && value[0] != 0) ? value : "../assets/engines/custom/live_engine.mr";
}
static constexpr int LiveServerPort = 8765;

// Header lines written by tools/engine-builder.html:
//   // builder:structure <hash>         hash of everything that needs a full reload
//   // builder:live <key> <values...>   values that can be changed while the engine runs
struct LiveHeader {
    std::string structure;
    std::map<std::string, std::string> values;
};

static LiveHeader readLiveHeader(const char *path) {
    LiveHeader header;
    std::ifstream file(path);
    std::string line;
    for (int i = 0; i < 40 && std::getline(file, line); ++i) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        const std::string structureTag = "// builder:structure ";
        const std::string liveTag = "// builder:live ";
        if (line.compare(0, structureTag.size(), structureTag) == 0) {
            header.structure = line.substr(structureTag.size());
        }
        else if (line.compare(0, liveTag.size(), liveTag) == 0) {
            const std::string rest = line.substr(liveTag.size());
            const size_t space = rest.find(' ');
            if (space != std::string::npos) {
                header.values[rest.substr(0, space)] = rest.substr(space + 1);
            }
        }
    }

    return header;
}


// Portable reader for the impulse responses: 16 bit PCM, first channel (the script may use \ or / in paths)
static bool readImpulseResponse(const std::string &path, std::vector<int16_t> *samples) {
    std::string normalized = path;
    for (char &c : normalized) if (c == '\\') c = '/';

    std::ifstream file(normalized, std::ios::binary);
    if (!file) return false;

    char riff[12];
    file.read(riff, 12);
    if (!file || std::memcmp(riff, "RIFF", 4) != 0 || std::memcmp(riff + 8, "WAVE", 4) != 0) return false;

    uint16_t channels = 1, bits = 16;
    while (file) {
        char id[4];
        uint32_t size = 0;
        if (!file.read(id, 4) || !file.read(reinterpret_cast<char *>(&size), 4)) break;

        if (std::memcmp(id, "fmt ", 4) == 0) {
            std::vector<char> format(size);
            file.read(format.data(), size);
            if (size < 16) return false;
            std::memcpy(&channels, format.data() + 2, 2);
            std::memcpy(&bits, format.data() + 14, 2);
        }
        else if (std::memcmp(id, "data", 4) == 0) {
            if (bits != 16 || channels == 0) return false;

            std::vector<int16_t> raw(size / 2);
            file.read(reinterpret_cast<char *>(raw.data()), size);

            samples->clear();
            for (size_t i = 0; i + channels <= raw.size(); i += channels) samples->push_back(raw[i]);
            return true;
        }
        else {
            file.seekg(size + (size & 1), std::ios::cur);
        }
    }

    return false;
}

SimulationSession::SimulationSession() {
    /* void */
}

SimulationSession::~SimulationSession() {
    /* void */
}

void SimulationSession::log(const std::string &message) {
    if (callbacks.log) callbacks.log(message);
}

bool SimulationSession::loadScript(const std::string &path, bool keepOnFailure) {
    Engine *engine = nullptr;
    Vehicle *vehicle = nullptr;
    Transmission *transmission = nullptr;

#ifdef ATG_ENGINE_SIM_PIRANHA_ENABLED
    es_script::Compiler compiler;
    compiler.initialize();
    const bool compiled = compiler.compile(path);
    if (compiled) {
        const es_script::Compiler::Output output = compiler.execute();

        if (keepOnFailure && output.engine == nullptr) {
            // Script compiled but did not produce an engine: keep the running one
            compiler.destroy();
            return false;
        }

        m_applicationSettings = output.applicationSettings;
        if (callbacks.settings) callbacks.settings(output.applicationSettings);

        engine = output.engine;
        vehicle = output.vehicle;
        transmission = output.transmission;
    }
    else {
        engine = nullptr;
        vehicle = nullptr;
        transmission = nullptr;

        if (keepOnFailure) {
            // Syntax errors are listed in error_log.log, the running engine stays untouched
            compiler.destroy();
            return false;
        }
    }

    compiler.destroy();
#endif /* ATG_ENGINE_SIM_PIRANHA_ENABLED */

    if (vehicle == nullptr) {
        Vehicle::Parameters vehParams;
        vehParams.mass = units::mass(1597, units::kg);
        vehParams.diffRatio = 3.42;
        vehParams.tireRadius = units::distance(10, units::inch);
        vehParams.dragCoefficient = 0.25;
        vehParams.crossSectionArea = units::distance(6.0, units::foot) * units::distance(6.0, units::foot);
        vehParams.rollingResistance = 2000.0;
        vehicle = new Vehicle;
        vehicle->initialize(vehParams);
    }

    if (transmission == nullptr) {
        const double gearRatios[] = { 2.97, 2.07, 1.43, 1.00, 0.84, 0.56 };
        Transmission::Parameters tParams;
        tParams.GearCount = 6;
        tParams.GearRatios = gearRatios;
        tParams.MaxClutchTorque = units::torque(1000.0, units::ft_lb);
        transmission = new Transmission;
        transmission->initialize(tParams);
    }

    loadEngine(engine, vehicle, transmission);
    if (callbacks.engineLoaded) callbacks.engineLoaded();

    return true;
}

void SimulationSession::loadEngine(Engine *engine, Vehicle *vehicle, Transmission *transmission) {
    m_dynoSweep.abort("engine reloaded", nullptr, nullptr);

    if (callbacks.beforeEngineReplaced) callbacks.beforeEngineReplaced();

    if (m_simulator != nullptr) {
        m_simulator->releaseSimulation();
        delete m_simulator;
    }

    if (m_vehicle != nullptr) {
        delete m_vehicle;
        m_vehicle = nullptr;
    }

    if (m_transmission != nullptr) {
        delete m_transmission;
        m_transmission = nullptr;
    }

    if (m_iceEngine != nullptr) {
        m_iceEngine->destroy();
        delete m_iceEngine;
    }

    m_iceEngine = engine;
    m_vehicle = vehicle;
    m_transmission = transmission;

    if (engine == nullptr || vehicle == nullptr || transmission == nullptr) {
        m_iceEngine = nullptr;
        m_simulator = nullptr;
        return;
    }

    m_simulator = engine->createSimulator(vehicle, transmission);

    engine->calculateDisplacement();
    m_simulator->setSimulationFrequency(engine->getSimulationFrequency());

    Synthesizer::AudioParameters audioParams = m_simulator->synthesizer().getAudioParameters();
    audioParams.inputSampleNoise = static_cast<float>(engine->getInitialJitter());
    audioParams.airNoise = static_cast<float>(engine->getInitialNoise());
    audioParams.dF_F_mix = static_cast<float>(engine->getInitialHighFrequencyGain());
    m_simulator->synthesizer().setAudioParameters(audioParams);

    loadImpulseResponses();
    m_simulator->startAudioRenderingThread();
}

void SimulationSession::loadImpulseResponses() {
    for (int i = 0; i < m_iceEngine->getExhaustSystemCount(); ++i) {
        ImpulseResponse *response = m_iceEngine->getExhaustSystem(i)->getImpulseResponse();

        std::vector<int16_t> samples;
        if (!readImpulseResponse(response->getFilename(), &samples)) {
            log("Cannot read impulse response " + response->getFilename());
            continue;
        }

        m_simulator->synthesizer().initializeImpulseResponse(
            samples.data(),
            (unsigned int)samples.size(),
            response->getVolume(),
            i);
    }
}

bool SimulationSession::reloadScript() {
    if (callbacks.reloadBegin) callbacks.reloadBegin();

    const bool ok = m_live
        ? loadScript(liveEntryScript(), true)
        : loadScript();

    if (m_live && ok) {
        m_liveStructure = readLiveHeader(liveEngineFile().c_str()).structure;
        m_lastCamLow.clear();
        m_lastCamHigh.clear();
        m_lastHighLift = false;

        // A fresh engine only knows what the script says: add the live values (e.g. the ignition map)
        if (m_iceEngine != nullptr) applyLiveTuning(liveEngineFile());
    }

    if (callbacks.reloadEnd) callbacks.reloadEnd();

    if (m_live) {
        log(ok
            ? "Live reload: engine loaded"
            : "Live reload failed - see error_log.log (previous engine kept)");
    }

    return ok;
}

void SimulationSession::toggleLive() {
    m_live = !m_live;
    m_liveStructure.clear();
    m_livePending = false;
    m_livePollTimer = 0.0f;

    std::error_code ec;
    m_liveStamp = std::filesystem::last_write_time(liveEngineFile().c_str(), ec);

    const bool ok = reloadScript();

    bool listening = false;
    if (m_live) {
        // ENGINE_SIM_LIVE_PORT lets a second instance (e.g. for tests) use another port
        const char *portOverride = std::getenv("ENGINE_SIM_LIVE_PORT");
        const int port = (portOverride != nullptr && std::atoi(portOverride) > 0)
            ? std::atoi(portOverride)
            : LiveServerPort;
        listening = m_liveServer.start(port);
    }
    else {
        m_liveServer.stop();
    }

    if (ok || !m_live) {
        log(m_live
            ? (listening
                ? "Live reload ON - engine builder can connect (port " + std::to_string(m_liveServer.getPort()) + ")"
                : "Live reload ON - file watch only (live port busy)")
            : "Live reload OFF");
    }
}

void SimulationSession::updateLive(float dt) {
    if (!m_live) return;

    if (m_iceEngine != nullptr && m_iceEngine->hasHighLift()) {
        const bool high = m_iceEngine->isHighLiftActive();
        if (high != m_lastHighLift) {
            m_lastHighLift = high;
            log(std::string(high ? "CAM: HIGH LIFT @ " : "CAM: LOW LIFT @ ")
                + std::to_string((int)units::toRpm(std::fabs(m_iceEngine->getSpeed()))) + " rpm");
        }
    }

    m_liveServer.poll([this](const std::string &method, const std::string &path, const std::string &body) {
        return handleLiveRoute(method, path, body);
    });

    m_livePollTimer += dt;
    if (m_livePollTimer < 0.25f) return;
    m_livePollTimer = 0.0f;

    std::error_code ec;
    const auto stamp = std::filesystem::last_write_time(liveEngineFile().c_str(), ec);
    if (ec) return;

    if (stamp != m_liveStamp) {
        // Wait for one more poll so a half-written file is not picked up
        m_liveStamp = stamp;
        m_livePending = true;
    }
    else if (m_livePending) {
        m_livePending = false;

        // Only values marked "live" changed -> patch the running engine, no restart
        const LiveHeader header = readLiveHeader(liveEngineFile().c_str());
        if (!header.structure.empty()
            && header.structure == m_liveStructure
            && m_iceEngine != nullptr)
        {
            applyLiveTuning(liveEngineFile().c_str());
        }
        else {
            reloadScript();
        }
    }
}

SimulationSession::StepResult SimulationSession::step(
    double dt,
    double simulationSpeed,
    const std::function<void()> &perStep)
{
    StepResult result;
    if (m_simulator == nullptr) return result;

    m_simulator->setSimulationSpeed(simulationSpeed);
    m_simulator->startFrame(dt);

    const auto t0 = std::chrono::steady_clock::now();
    result.iterations = m_simulator->getFrameIterationCount();
    while (m_simulator->simulateStep()) {
        if (perStep) perStep();
    }
    const auto t1 = std::chrono::steady_clock::now();
    result.seconds = std::chrono::duration<double>(t1 - t0).count();

    m_simulator->endFrame();
    return result;
}

int SimulationSession::readAudio(int samples, int16_t *target) {
    return (m_simulator != nullptr) ? m_simulator->readAudioOutput(samples, target) : 0;
}

void SimulationSession::shutdown() {
    m_liveServer.stop();

    if (m_simulator != nullptr) {
        m_simulator->endAudioRenderingThread();
        m_simulator->destroy();
    }
}

std::string SimulationSession::applyLiveTuning(const std::string &path) {
    const LiveHeader header = readLiveHeader(path.c_str());
    std::string applied;

    auto number = [&](const char *key, double *out) {
        const auto it = header.values.find(key);
        if (it == header.values.end()) return false;
        char *end = nullptr;
        *out = std::strtod(it->second.c_str(), &end);
        return end != it->second.c_str();
    };

    double v = 0;

    if (number("idle_throttle_plate_position", &v)) {
        for (int i = 0; i < m_iceEngine->getIntakeCount(); ++i) {
            m_iceEngine->getIntake(i)->setIdleThrottlePlatePosition(v);
        }
        applied += " idle_throttle=" + std::to_string(v).substr(0, 6);
    }

    if (number("rev_limit_rpm", &v)) {
        m_iceEngine->getIgnitionModule()->setRevLimit(units::rpm(v));
        applied += " rev_limit=" + std::to_string((int)v);
    }

    if (number("starter_torque_nm", &v)) {
        m_simulator->m_starterMotor.m_maxTorque = units::torque(v, units::Nm);
        applied += " starter=" + std::to_string((int)v) + "Nm";
    }

    if (number("friction_torque_nm", &v)) {
        PistonEngineSimulator *piston = dynamic_cast<PistonEngineSimulator *>(m_simulator);
        if (piston != nullptr) {
            for (int i = 0; i < m_iceEngine->getCrankshaftCount(); ++i) {
                m_iceEngine->getCrankshaft(i)->setFrictionTorque(units::torque(v, units::Nm));
                piston->setCrankshaftFrictionTorque(i, units::torque(v, units::Nm));
            }
            applied += " friction=" + std::to_string(v).substr(0, 5) + "Nm";
        }
    }

    // Cam profiles. cam_low / cam_high: intake lift [mm], intake duration [deg at 0.050 in], exhaust lift [mm],
    // exhaust duration [deg]; cam_switch: speed [rpm], hysteresis [rpm], minimum throttle [%], minimum manifold pressure [kPa]
    {
        const auto low = header.values.find("cam_low");
        const auto high = header.values.find("cam_high");
        const std::string lowText = low != header.values.end() ? low->second : "";
        const std::string highText = high != header.values.end() ? high->second : "";

        if (lowText != m_lastCamLow || highText != m_lastCamHigh) {
            auto parse = [](const std::string &text) {
                std::vector<double> values;
                std::stringstream ss(text);
                double value;
                while (ss >> value) values.push_back(value);
                return values;
            };
            auto make = [](double liftMm, double durationDeg) {
                return generateHarmonicCamLobe(durationDeg * units::deg, 1.0, units::distance(liftMm, units::mm), 100);
            };

            const std::vector<double> l = parse(lowText), h = parse(highText);
            Function *il = nullptr, *el = nullptr, *ih = nullptr, *eh = nullptr;
            if (l.size() == 4) { il = make(l[0], l[1]); el = make(l[2], l[3]); }
            if (h.size() == 4) { ih = make(h[0], h[1]); eh = make(h[2], h[3]); }

            const bool valid = (l.size() != 4 || (il && el)) && (h.size() != 4 || (ih && eh));
            std::vector<Function *> created;
            for (Function *f : { il, el, ih, eh }) if (f != nullptr) created.push_back(f);

            if (valid && !created.empty()) {
                for (int i = 0; i < m_iceEngine->getCylinderBankCount(); ++i) {
                    m_iceEngine->getHead(i)->getValvetrain()->setLobeProfiles(il, el, ih, eh);
                }

                m_liveLobes.push_back(created);
                while (m_liveLobes.size() > 3) {                    // the engine view picks up the new profiles at once
                    for (Function *f : m_liveLobes.front()) { f->destroy(); delete f; }
                    m_liveLobes.pop_front();
                }

                applied += " cam_lobes";
            }
            else {
                for (Function *f : created) { f->destroy(); delete f; }
                if (!created.empty() || l.size() == 4 || h.size() == 4) applied += " cam_lobes_INVALID";
            }

            m_lastCamLow = lowText;
            m_lastCamHigh = highText;
        }

        const auto sw = header.values.find("cam_switch");
        if (sw != header.values.end()) {
            std::stringstream ss(sw->second);
            double rpm = 0, hysteresis = 0, throttle = 0, manifold = 0;
            if (ss >> rpm >> hysteresis >> throttle >> manifold) {
                for (int i = 0; i < m_iceEngine->getCylinderBankCount(); ++i) {
                    m_iceEngine->getHead(i)->getValvetrain()->setSwitching(
                        units::rpm(rpm), units::rpm(hysteresis), throttle / 100.0, manifold * 1000.0);
                }
                applied += " cam_switch=" + std::to_string((int)rpm);
            }
        }
    }

    if (number("fuel_octane", &v)) {
        m_iceEngine->getFuel()->setOctane(v);
        applied += " octane=" + std::to_string((int)v);
    }

    if (number("knock_damage", &v)) {
        m_iceEngine->setKnockDamageEnabled(v > 0.5);
        applied += v > 0.5 ? " knock_damage=on" : " knock_damage=off";
    }

    if (number("engine_repair", &v)) {
        // every click on "repair" in the builder raises the counter
        const int counter = (int)v;
        if (m_lastRepairCounter >= 0 && counter > m_lastRepairCounter) {
            m_iceEngine->repairKnockDamage();
            applied += " repaired";
        }

        m_lastRepairCounter = counter;
    }

    const auto curve = header.values.find("timing_curve");
    if (curve != header.values.end()) {
        // format: "<rpm>:<deg> <rpm>:<deg> ..."
        std::vector<std::pair<double, double>> samples;
        std::stringstream ss(curve->second);
        std::string token;
        while (ss >> token) {
            const size_t colon = token.find(':');
            if (colon == std::string::npos) continue;
            samples.push_back({
                std::atof(token.substr(0, colon).c_str()),
                std::atof(token.substr(colon + 1).c_str()) });
        }

        if (!samples.empty()) {
            Function *next = new Function;
            next->initialize((int)samples.size(), units::rpm(1000.0)); // same filter radius as the generated script
            for (const auto &sample : samples) {
                next->addSample(units::rpm(sample.first), sample.second * units::deg);
            }

            Function *previous = m_iceEngine->getIgnitionModule()->getTimingCurve();
            m_iceEngine->getIgnitionModule()->setTimingCurve(next);

            if (m_liveTimingCurve != nullptr && previous == m_liveTimingCurve) {
                m_liveTimingCurve->destroy();
                delete m_liveTimingCurve;
            }

            m_liveTimingCurve = next;
            applied += " timing_curve";
        }
    }

    // Ignition map (advance over speed and manifold pressure): replaces the curve while it is present
    auto numbers = [&](const char *key, double scale) {
        std::vector<double> out;
        const auto it = header.values.find(key);
        if (it == header.values.end()) return out;

        std::stringstream ss(it->second);
        double value;
        while (ss >> value) out.push_back(value * scale);
        return out;
    };

    const std::vector<double> mapSpeeds = numbers("map_rpm", 1.0);                 // rpm
    const std::vector<double> mapLoads = numbers("map_load", 1000.0);              // kPa -> Pa
    const std::vector<double> mapAdvance = numbers("map_adv", units::deg);         // deg -> rad
    IgnitionModule *ignition = m_iceEngine->getIgnitionModule();
    if (!mapSpeeds.empty() && !mapLoads.empty() && mapAdvance.size() == mapSpeeds.size() * mapLoads.size()) {
        ignition->setTimingMap(mapSpeeds, mapLoads, mapAdvance);
        applied += ignition->hasTimingMap() ? " ignition_map" : " ignition_map_INVALID";
    }
    else if (ignition->hasTimingMap()) {
        ignition->clearTimingMap();
        applied += " ignition_map_off";
    }

    {
        log("Live (no restart):" + applied);
    }

    return applied;
}


static std::string jsonEscape(const std::string &in) {
    std::string out;
    for (const char c : in) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': break;
            case '\t': out += "\\t"; break;
            default: out += ((unsigned char)c < 0x20) ? ' ' : c;
        }
    }
    return out;
}


// Called on the main thread for every engine pushed by the engine builder
std::string SimulationSession::handleLiveRequest(const std::string &body) {
    {
        std::ofstream file(liveEngineFile().c_str(), std::ios::binary | std::ios::trunc);
        file.write(body.data(), (std::streamsize)body.size());
        if (!file.good()) {
            return "{\"ok\":false,\"error\":\"could not write assets/engines/custom/live_engine.mr\"}";
        }
    }

    std::string result;
    const LiveHeader header = readLiveHeader(liveEngineFile().c_str());

    if (!header.structure.empty() && header.structure == m_liveStructure && m_iceEngine != nullptr) {
        const std::string applied = applyLiveTuning(liveEngineFile().c_str());
        result = "{\"ok\":true,\"mode\":\"live\",\"message\":\"" + jsonEscape(applied) + "\"}";
    }
    else {
        const bool ok = reloadScript();
        if (ok) {
            result = "{\"ok\":true,\"mode\":\"reload\",\"message\":\"engine reloaded\"}";
        }
        else {
            std::ifstream log("error_log.log");
            std::string text((std::istreambuf_iterator<char>(log)), std::istreambuf_iterator<char>());
            if (text.size() > 3000) text.resize(3000);
            if (text.empty()) text = "script compiled but did not produce an engine";
            result = "{\"ok\":false,\"mode\":\"reload\",\"error\":\"" + jsonEscape(text) + "\"}";
        }
    }

    // The file was written by us: do not let the file watcher process it a second time
    std::error_code ec;
    const auto stamp = std::filesystem::last_write_time(liveEngineFile().c_str(), ec);
    if (!ec) m_liveStamp = stamp;
    m_livePending = false;

    return result;
}

// All requests of the engine builder, executed on the main thread
std::string SimulationSession::handleLiveRoute(
    const std::string &method,
    const std::string &path,
    const std::string &body)
{
    if (method == "POST" && path == "/engine") {
        return handleLiveRequest(body);
    }

    if (method == "GET" && path == "/dyno") {
        return m_dynoSweep.statusJson();
    }

    if (method == "POST" && path == "/dyno/stop") {
        m_dynoSweep.abort("stopped by user", m_iceEngine, m_simulator);
        return "{\"ok\":true}";
    }

    if (method == "POST" && path == "/dyno") {
        DynoSweep::Parameters params;
        std::string error;
        if (!DynoSweep::parseParameters(body, &params)) {
            return "{\"ok\":false,\"error\":\"invalid dyno parameters\"}";
        }

        if (!m_dynoSweep.start(params, m_iceEngine, m_simulator, &error)) {
            return "{\"ok\":false,\"error\":\"" + jsonEscape(error) + "\"}";
        }

        {
            log("Dyno run started from the engine builder");
        }

        return "{\"ok\":true}";
    }

    return "{\"ok\":false,\"error\":\"unknown route\"}";
}

