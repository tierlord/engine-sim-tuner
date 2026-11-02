#include "../include/dyno_sweep.h"

#include "../include/crankshaft.h"
#include "../include/engine.h"
#include "../include/simulator.h"
#include "../include/units.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace {
    constexpr double MinSettleSeconds = 0.5;
    constexpr double MinMeasureSeconds = 0.4;
    constexpr double MaxExtraSettleSeconds = 2.0;
    constexpr double StartingTimeout = 2.5;
    constexpr double StartedRpm = 800.0;
    constexpr double SpeedTolerance = 0.03;

    constexpr double RampSettleSeconds = 1.0;     // hold the start speed before the pull begins
    constexpr double BinWidthRpm = 50.0;          // ramp mode: the curve is averaged in bins of this width
    constexpr long MinBinSamples = 50;

    std::string num(double v) {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%.4g", std::isfinite(v) ? v : 0.0);
        return buffer;
    }
}

DynoSweep::DynoSweep() {
    m_state = State::Idle;
    m_rpm = 0;
    m_simTime = 0;
    m_phaseStart = 0;
    m_settleTime = 0;
    m_measureTime = 0;
    m_extraSettle = 0;
    m_measuring = false;
    m_startingTime = 0;
    m_torqueSum = 0;
    m_speedSum = 0;
    m_samples = 0;
    m_manifoldSum = 0;
    m_advanceSum = 0;
    m_frames = 0;
    m_rampActive = false;
    m_rampStartTime = 0;
    m_inertia = 0;
    m_timestep = 1e-4;
    m_cylinders = 1;
    m_knockStart = 0;
    m_lastKnockEvents = 0;
    m_damage = 0;
    m_savedDynoEnabled = false;
    m_savedDynoHold = false;
}

// Text format used by the engine builder: lines of "key=value"
bool DynoSweep::parseParameters(const std::string &text, Parameters *params) {
    std::stringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        const std::string key = line.substr(0, eq);
        const std::string valueText = line.substr(eq + 1);
        const double value = std::atof(valueText.c_str());

        if (key == "start") params->rpmStart = value;
        else if (key == "end") params->rpmEnd = value;
        else if (key == "step") params->rpmStep = value;
        else if (key == "settle") params->settleCycles = (int)value;
        else if (key == "measure") params->measureCycles = (int)value;
        else if (key == "throttle") params->throttle = value;
        else if (key == "mode") params->ramp = valueText.find("ramp") != std::string::npos;
        else if (key == "rate") params->rampRate = value;
    }

    if (!(params->rpmStart > 0) || !(params->throttle >= 0.0 && params->throttle <= 1.0)) return false;

    if (params->ramp) {
        return params->rampRate >= 20 && params->rampRate <= 5000
            && (params->rpmEnd <= 0 || params->rpmEnd > params->rpmStart);
    }

    return params->rpmEnd >= params->rpmStart && params->rpmStep >= 50
        && params->settleCycles >= 1 && params->measureCycles >= 1;
}

bool DynoSweep::start(const Parameters &params, Engine *engine, Simulator *simulator, std::string *error) {
    if (engine == nullptr || simulator == nullptr) { *error = "no engine loaded"; return false; }
    if (isActive()) { *error = "a dyno run is already active"; return false; }

    m_params = params;

    // Above the rev limiter the ignition is cut and the result would be meaningless
    const double limiter = units::toRpm(engine->getIgnitionModule()->getRevLimit()) * 0.97;
    m_params.rpmEnd = (m_params.rpmEnd > 0) ? std::min(m_params.rpmEnd, limiter) : limiter;
    if (m_params.rpmEnd < m_params.rpmStart + (m_params.ramp ? 200.0 : 0.0)) {
        *error = "start speed is too close to (or above) the rev limiter";
        return false;
    }

    m_points.clear();
    m_message.clear();
    m_simTime = 0;
    m_startingTime = 0;
    m_measuring = false;
    m_rpm = m_params.rpmStart;
    m_rampActive = false;
    m_rampStartTime = 0;

    // Rotating mass the engine has to accelerate (crankshaft with flywheel, as set in the engine script)
    m_inertia = engine->getOutputCrankshaft() != nullptr ? engine->getOutputCrankshaft()->getMomentOfInertia() : 0.0;
    m_timestep = simulator->getTimestep();
    m_cylinders = std::max(engine->getCylinderCount(), 1);
    m_lastKnockEvents = engine->getKnockEventCount();
    m_bins.assign((size_t)std::ceil((m_params.rpmEnd - m_params.rpmStart) / BinWidthRpm) + 3, Bin());

    m_savedDynoEnabled = simulator->m_dyno.m_enabled;
    m_savedDynoHold = simulator->m_dyno.m_hold;

    m_state = State::Starting;
    return true;
}

void DynoSweep::beginPoint() {
    const double cycle = 120.0 / m_rpm;                  // seconds per 720 degree cycle
    m_settleTime = std::max(m_params.settleCycles * cycle, MinSettleSeconds);
    m_measureTime = std::max(m_params.measureCycles * cycle, MinMeasureSeconds);
    m_extraSettle = 0;
    m_phaseStart = m_simTime;
    m_measuring = false;
}

void DynoSweep::update(float dt, Engine *engine, Simulator *simulator) {
    if (!isActive()) return;

    if (engine == nullptr || simulator == nullptr) {
        m_state = State::Aborted;
        m_message = "engine was removed";
        return;
    }

    engine->getIgnitionModule()->m_enabled = true;
    m_damage = engine->getKnockDamage();

    if (engine->isDestroyed()) {
        finish(State::Aborted, "engine destroyed by knock", simulator);
        return;
    }

    if (m_state == State::Starting) {
        // An engine that is already running goes straight on; otherwise crank it for a moment.
        // The dyno takes over in any case, so an engine that does not fire shows up as negative torque.
        m_startingTime += dt;
        simulator->m_dyno.m_enabled = false;

        if (engine->getRpm() >= StartedRpm || m_startingTime > StartingTimeout) {
            simulator->m_starterMotor.m_enabled = false;
            m_state = State::Running;
            m_phaseStart = m_simTime;
            if (!m_params.ramp) beginPoint();
        }
        else {
            simulator->m_starterMotor.m_enabled = true;
            engine->setSpeedControl(0.15);
            return;
        }
    }

    if (m_params.ramp) updateRamp(engine, simulator);
    else updateSteps(engine, simulator);
}

void DynoSweep::updateSteps(Engine *engine, Simulator *simulator) {
    // Hold the current speed with the dynamometer at the requested throttle
    simulator->m_starterMotor.m_enabled = false;
    engine->setSpeedControl(m_params.throttle);
    simulator->m_dyno.m_enabled = true;
    simulator->m_dyno.m_hold = true;
    simulator->m_dyno.m_rotationSpeed = units::rpm(m_rpm);

    const double elapsed = m_simTime - m_phaseStart;

    if (!m_measuring) {
        const bool settled = elapsed >= m_settleTime + m_extraSettle;
        if (settled) {
            const double error = std::fabs(engine->getRpm() - m_rpm) / m_rpm;
            if (error > SpeedTolerance && m_extraSettle < MaxExtraSettleSeconds) {
                m_extraSettle += 0.25;                // dyno has not caught up yet, give it more time
            }
            else {
                m_measuring = true;
                m_phaseStart = m_simTime;
                m_torqueSum = m_speedSum = m_manifoldSum = m_advanceSum = 0;
                m_samples = 0;
                m_frames = 0;
                m_knockStart = engine->getKnockEventCount();
            }
        }
    }
    else {
        m_manifoldSum += engine->getManifoldPressure();
        m_advanceSum += engine->getIgnitionModule()->getTimingAdvance();
        ++m_frames;

        if (elapsed >= m_measureTime) {
            finishPoint(engine);

            m_rpm += m_params.rpmStep;
            if (m_rpm > m_params.rpmEnd + 1e-6) {
                finish(State::Done, "", simulator);
            }
            else {
                beginPoint();
            }
        }
    }
}

void DynoSweep::updateRamp(Engine *engine, Simulator *simulator) {
    simulator->m_starterMotor.m_enabled = false;
    engine->setSpeedControl(m_params.throttle);
    simulator->m_dyno.m_enabled = true;
    simulator->m_dyno.m_hold = true;

    if (!m_rampActive) {
        // Hold the start speed until the engine runs steadily, then begin the pull
        m_rpm = m_params.rpmStart;
        simulator->m_dyno.m_rotationSpeed = units::rpm(m_rpm);

        const double elapsed = m_simTime - m_phaseStart;
        if (elapsed >= RampSettleSeconds) {
            const double error = std::fabs(engine->getRpm() - m_rpm) / m_rpm;
            if (error <= SpeedTolerance * 1.5 || elapsed >= RampSettleSeconds + MaxExtraSettleSeconds) {
                m_rampActive = true;
                m_rampStartTime = m_simTime;
            }
        }
        return;
    }

    // The application limits the dyno speed to its own range before every frame; set the ramp again so the
    // first simulation step of the frame already runs with the right speed (sample() does it for the rest)
    simulator->m_dyno.m_rotationSpeed = units::rpm(m_rpm);

    // Manifold pressure and ignition advance are recorded once per frame into the bin of the current speed
    const double rpmNow = units::toRpm(std::fabs(engine->getSpeed()));
    const long index = (long)std::floor((rpmNow - m_params.rpmStart) / BinWidthRpm);
    if (index >= 0 && index < (long)m_bins.size()) {
        m_bins[index].manifoldSum += engine->getManifoldPressure();
        m_bins[index].advanceSum += engine->getIgnitionModule()->getTimingAdvance();
        ++m_bins[index].frames;
        m_bins[index].knockEvents += engine->getKnockEventCount() - m_lastKnockEvents;
    }
    m_lastKnockEvents = engine->getKnockEventCount();

    if (m_rpm >= m_params.rpmEnd) {
        finish(State::Done, "", simulator);
    }
}

void DynoSweep::sample(Simulator *simulator, Engine *engine) {
    if (m_state != State::Running) return;

    m_simTime += simulator->getTimestep();

    if (m_params.ramp) {
        if (!m_rampActive) return;

        // The dyno speed follows the ramp in every simulation step
        m_rpm = m_params.rpmStart + m_params.rampRate * (m_simTime - m_rampStartTime);
        simulator->m_dyno.m_rotationSpeed = units::rpm(m_rpm);

        const double omega = std::fabs(engine->getSpeed());
        const long index = (long)std::floor((units::toRpm(omega) - m_params.rpmStart) / BinWidthRpm);
        if (index >= 0 && index < (long)m_bins.size()) {
            // The dyno takes up what the engine produces minus what accelerates the rotating mass
            const double acceleration = units::rpm(m_params.rampRate);            // rad/s^2
            m_bins[index].torqueSum += simulator->m_dyno.getTorque() + m_inertia * acceleration;
            m_bins[index].speedSum += omega;
            ++m_bins[index].samples;
        }
        return;
    }

    if (m_measuring) {
        m_torqueSum += simulator->m_dyno.getTorque();
        m_speedSum += std::fabs(engine->getSpeed());
        ++m_samples;
    }
}

void DynoSweep::finishPoint(Engine *engine) {
    Point point;
    point.rpm = m_rpm;

    if (m_samples > 0 && m_frames > 0) {
        point.torqueNm = m_torqueSum / m_samples;
        const double omega = m_speedSum / m_samples;
        point.powerKw = point.torqueNm * omega / 1000.0;
        point.manifoldKpa = (m_manifoldSum / m_frames) / 1000.0;
        point.advanceDeg = (m_advanceSum / m_frames) / units::deg;
        point.held = std::fabs(units::toRpm(omega) - m_rpm) / m_rpm <= SpeedTolerance * 2;

        // knocking cycles per cylinder cycle during the window
        const double cylinderCycles = m_cylinders * (m_samples * m_timestep) * (m_rpm / 120.0);
        if (cylinderCycles > 0) point.knockShare = std::fmin((double)(engine->getKnockEventCount() - m_knockStart) / cylinderCycles, 1.0);
    }
    else {
        point.held = false;
    }

    m_points.push_back(point);
}

// Curve of the pull: mean of every bin, lightly smoothed (1-2-1) against the cycle-to-cycle ripple
std::vector<DynoSweep::Point> DynoSweep::rampPoints(bool onlyFinished) const {
    // bins above the current speed are still being filled during the run
    const long current = onlyFinished
        ? (long)std::floor((m_rpm - m_params.rpmStart) / BinWidthRpm) - 1
        : (long)m_bins.size();

    std::vector<Point> raw;
    for (long i = 0; i < (long)m_bins.size() && i <= current; ++i) {
        const Bin &bin = m_bins[i];
        if (bin.samples < MinBinSamples || bin.frames == 0) continue;

        Point p;
        const double omega = bin.speedSum / bin.samples;
        p.rpm = units::toRpm(omega);
        p.torqueNm = bin.torqueSum / bin.samples;
        p.manifoldKpa = (bin.manifoldSum / bin.frames) / 1000.0;
        p.advanceDeg = (bin.advanceSum / bin.frames) / units::deg;
        const double cylinderCycles = m_cylinders * (bin.samples * m_timestep) * (p.rpm / 120.0);
        p.knockShare = cylinderCycles > 0 ? std::fmin(bin.knockEvents / cylinderCycles, 1.0) : 0.0;
        raw.push_back(p);
    }

    std::vector<Point> out = raw;
    for (size_t i = 1; i + 1 < raw.size(); ++i) {
        out[i].torqueNm = 0.25 * raw[i - 1].torqueNm + 0.5 * raw[i].torqueNm + 0.25 * raw[i + 1].torqueNm;
        out[i].knockShare = 0.25 * raw[i - 1].knockShare + 0.5 * raw[i].knockShare + 0.25 * raw[i + 1].knockShare;
    }

    for (Point &p : out) {
        p.powerKw = p.torqueNm * units::rpm(p.rpm) / 1000.0;
    }

    return out;
}

void DynoSweep::finish(State state, const char *message, Simulator *simulator) {
    m_state = state;
    m_message = message;
    m_measuring = false;

    if (state == State::Done && m_params.ramp) {
        m_points = rampPoints(false);
    }

    if (simulator != nullptr) {
        simulator->m_dyno.m_enabled = m_savedDynoEnabled;
        simulator->m_dyno.m_hold = m_savedDynoHold;
        simulator->m_starterMotor.m_enabled = false;
    }
}

void DynoSweep::abort(const char *reason, Engine *engine, Simulator *simulator) {
    (void)engine;
    if (isActive()) {
        finish(State::Aborted, reason, simulator);
    }
}

std::string DynoSweep::statusJson() const {
    const char *state = "idle";
    switch (m_state) {
        case State::Starting: state = "starting"; break;
        case State::Running: state = "running"; break;
        case State::Done: state = "done"; break;
        case State::Aborted: state = "aborted"; break;
        default: break;
    }

    double progress;
    if (m_state == State::Done) progress = 1.0;
    else if (m_state == State::Idle || m_state == State::Starting) progress = 0.0;
    else if (m_params.ramp) progress = m_rampActive ? (m_rpm - m_params.rpmStart) / std::max(m_params.rpmEnd - m_params.rpmStart, 1.0) : 0.0;
    else progress = (m_rpm - m_params.rpmStart) / (std::max(m_params.rpmEnd - m_params.rpmStart, 1.0) + m_params.rpmStep);

    const bool partial = m_params.ramp && m_state == State::Running;
    const std::vector<Point> points = partial ? rampPoints(true) : m_points;

    std::string json = "{\"state\":\"" + std::string(state) + "\",\"mode\":\"" + (m_params.ramp ? "ramp" : "steps")
        + "\",\"progress\":" + num(progress)
        + ",\"rpm\":" + num(m_rpm)
        + ",\"damage\":" + num(m_damage)
        + ",\"message\":\"" + m_message + "\",\"points\":[";

    for (size_t i = 0; i < points.size(); ++i) {
        const Point &p = points[i];
        if (i) json += ",";
        json += "{\"rpm\":" + num(p.rpm) + ",\"nm\":" + num(p.torqueNm) + ",\"kw\":" + num(p.powerKw)
            + ",\"map\":" + num(p.manifoldKpa) + ",\"adv\":" + num(p.advanceDeg) + ",\"knock\":" + num(p.knockShare)
            + ",\"held\":" + (p.held ? "true" : "false") + "}";
    }

    return json + "]}";
}
