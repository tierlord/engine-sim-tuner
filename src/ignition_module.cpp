#include "../include/ignition_module.h"

#include "../include/utilities.h"
#include "../include/constants.h"
#include "../include/units.h"

#include <cmath>

IgnitionModule::IgnitionModule() {
    m_plugs = nullptr;
    m_crankshaft = nullptr;
    m_timingCurve = nullptr;
    m_cylinderCount = 0;
    m_lastCrankshaftAngle = 0.0;
    m_enabled = false;
    m_revLimitTimer = 0.0;
    m_revLimit = 0;
    m_limiterDuration = 0;
    m_loadPressure = units::pressure(1.0, units::atm);
    m_filteredLoad = m_loadPressure;
}

IgnitionModule::~IgnitionModule() {
    assert(m_plugs == nullptr);
}

void IgnitionModule::destroy() {
    delete[] m_plugs;

    m_plugs = nullptr;
    m_cylinderCount = 0;
}

void IgnitionModule::initialize(const Parameters &params) {
    m_cylinderCount = params.cylinderCount;
    m_plugs = new SparkPlug[m_cylinderCount];
    m_crankshaft = params.crankshaft;
    m_timingCurve = params.timingCurve;
    m_revLimit = params.revLimit;
    m_limiterDuration = params.limiterDuration;
}

void IgnitionModule::setFiringOrder(int cylinderIndex, double angle) {
    assert(cylinderIndex < m_cylinderCount);

    m_plugs[cylinderIndex].angle = angle;
    m_plugs[cylinderIndex].enabled = true;
}

void IgnitionModule::reset() {
    m_lastCrankshaftAngle = m_crankshaft->getCycleAngle();
    resetIgnitionEvents();
}

void IgnitionModule::update(double dt) {
    const double cycleAngle = m_crankshaft->getCycleAngle();

    // The manifold pressure pulses with every intake stroke; the map looks at the smoothed value
    constexpr double LoadFilterTime = 0.03;
    m_filteredLoad += (m_loadPressure - m_filteredLoad) * (1.0 - std::exp(-dt / LoadFilterTime));

    if (m_enabled && m_revLimitTimer == 0) {
        const double fourPi = 4 * constants::pi;
        const double advance = getTimingAdvance();

        for (int i = 0; i < m_cylinderCount; ++i) {
            double adjustedAngle = positiveMod(m_plugs[i].angle - advance, fourPi);
            const double r0 = m_lastCrankshaftAngle;
            double r1 = cycleAngle;

            if (m_crankshaft->m_body.v_theta < 0) {
                if (r1 < r0) {
                    r1 += fourPi;
                    adjustedAngle += fourPi;
                }

                if (adjustedAngle >= r0 && adjustedAngle < r1) {
                    m_plugs[i].ignitionEvent = m_plugs[i].enabled;
                }
            }
            else {
                if (r1 > r0) {
                    r1 -= fourPi;
                    adjustedAngle -= fourPi;
                }

                if (adjustedAngle >= r1 && adjustedAngle < r0) {
                    m_plugs[i].ignitionEvent = m_plugs[i].enabled;
                }
            }
        }
    }

    m_revLimitTimer -= dt;
    if (std::fabs(m_crankshaft->m_body.v_theta) > m_revLimit) {
        m_revLimitTimer = m_limiterDuration;
    }

    if (m_revLimitTimer < 0) {
        m_revLimitTimer = 0;
    }

    m_lastCrankshaftAngle = cycleAngle;
}

bool IgnitionModule::getIgnitionEvent(int index) const {
    return m_plugs[index].ignitionEvent;
}

void IgnitionModule::resetIgnitionEvents() {
    for (int i = 0; i < m_cylinderCount; ++i) {
        m_plugs[i].ignitionEvent = false;
    }
}

double IgnitionModule::getTimingAdvance() {
    const double speed = -m_crankshaft->m_body.v_theta;       // rad/s, positive while the engine runs

    if (!m_mapSpeeds.empty()) {
        return sampleTimingMap(units::toRpm(speed), m_filteredLoad);
    }

    return m_timingCurve->sampleTriangle(speed);
}

void IgnitionModule::setTimingMap(
    const std::vector<double> &speeds,
    const std::vector<double> &loads,
    const std::vector<double> &values)
{
    bool valid = !speeds.empty() && !loads.empty() && values.size() == speeds.size() * loads.size();
    for (size_t i = 1; i < speeds.size() && valid; ++i) valid = speeds[i] > speeds[i - 1];
    for (size_t i = 1; i < loads.size() && valid; ++i) valid = loads[i] > loads[i - 1];

    if (!valid) {
        clearTimingMap();
        return;
    }

    m_mapSpeeds = speeds;
    m_mapLoads = loads;
    m_mapValues = values;
}

void IgnitionModule::clearTimingMap() {
    m_mapSpeeds.clear();
    m_mapLoads.clear();
    m_mapValues.clear();
}

// Bilinear interpolation; outside the axes the edge value is used
double IgnitionModule::sampleTimingMap(double rpm, double pressure) const {
    auto locate = [](const std::vector<double> &axis, double v, size_t *lower, size_t *upper, double *t) {
        if (axis.size() == 1 || v <= axis.front()) { *lower = *upper = 0; *t = 0; return; }
        if (v >= axis.back()) { *lower = *upper = axis.size() - 1; *t = 0; return; }

        size_t i = 1;
        while (axis[i] < v) ++i;
        *lower = i - 1;
        *upper = i;
        *t = (v - axis[i - 1]) / (axis[i] - axis[i - 1]);
    };

    size_t r0, r1, c0, c1;
    double tr, tc;
    locate(m_mapSpeeds, rpm, &r0, &r1, &tr);
    locate(m_mapLoads, pressure, &c0, &c1, &tc);

    const size_t columns = m_mapLoads.size();
    const double a = m_mapValues[r0 * columns + c0] * (1 - tc) + m_mapValues[r0 * columns + c1] * tc;
    const double b = m_mapValues[r1 * columns + c0] * (1 - tc) + m_mapValues[r1 * columns + c1] * tc;
    return a * (1 - tr) + b * tr;
}

IgnitionModule::SparkPlug *IgnitionModule::getPlug(int i) {
    return &m_plugs[((i % m_cylinderCount) + m_cylinderCount) % m_cylinderCount];
}
