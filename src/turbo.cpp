#include "../include/turbo.h"

#include "../include/engine.h"
#include "../include/exhaust_system.h"
#include "../include/intake.h"
#include "../include/units.h"

#include <algorithm>
#include <cmath>

namespace {
    constexpr double AirCp = 1005.0;               // [J/(kg K)]
    constexpr double ExhaustCp = 1150.0;
    constexpr double MolarMass = 0.029;            // [kg/mol]
    constexpr double AirKappa = 0.2857;            // (gamma - 1) / gamma, air
    constexpr double ExhaustKappa = 0.25;
    constexpr double TurbineEfficiency = 0.68;
    constexpr double CompressorEfficiency = 0.72;
    constexpr double WheelRadius = 0.030;          // [m] compressor wheel
    constexpr double HeadCoefficient = 0.55;
    constexpr double BaseInertia = 8.0E-5;         // [kg m^2]
    constexpr double BearingFriction = 1.5E-5;     // [W / (rad/s)^2]
    constexpr double WastegateConductance = 0.9;   // conductance of the open wastegate relative to the open exhaust
    constexpr double WastegateBand = 0.12;         // [bar] above the target the wastegate is fully open
    constexpr double WastegateLag = 0.05;          // [s]
    constexpr double IdleOmega = 400.0;            // [rad/s] the shaft never stops completely
    constexpr double AmbientPressure = 101325.0;
    constexpr double AmbientTemperature = 298.15;
}

Turbo::Turbo() {
    m_kineticEnergy = 0.5 * BaseInertia * IdleOmega * IdleOmega;
    m_omega = IdleOmega;
    m_pressureRatio = 1.0;
    m_backPressureRatio = 1.0;
    m_wastegate = 0.0;
    m_blowOff = false;
    m_applied = false;
}

void Turbo::setSettings(const Settings &s) {
    m_settings = s;
    m_settings.targetBoost = std::clamp(s.targetBoost, 0.05, 3.0);
    m_settings.turbineSize = std::clamp(s.turbineSize, 0.1, 1.0);
    m_settings.inertiaScale = std::clamp(s.inertiaScale, 0.2, 5.0);
    m_settings.intercooler = std::clamp(s.intercooler, 0.0, 1.0);
}

void Turbo::step(Engine *engine, double dt) {
    if (!m_settings.enabled) {
        if (m_applied) {
            for (int i = 0; i < engine->getExhaustSystemCount(); ++i) engine->getExhaustSystem(i)->setOutletScale(1.0);
            for (int i = 0; i < engine->getIntakeCount(); ++i) engine->getIntake(i)->setAmbient(AmbientPressure, AmbientTemperature);
            m_pressureRatio = 1.0;
            m_backPressureRatio = 1.0;
            m_wastegate = 0.0;
            m_omega = IdleOmega;
            m_kineticEnergy = 0.5 * BaseInertia * m_omega * m_omega;
            m_applied = false;
        }
        return;
    }
    m_applied = true;

    const double inertia = BaseInertia * m_settings.inertiaScale;

    // --- turbine: exhaust that leaves through the turbine, pressure ratio from the collector pressure
    double outflow = 0.0, pressureSum = 0.0, temperatureSum = 0.0;
    const int exhaustCount = engine->getExhaustSystemCount();
    for (int i = 0; i < exhaustCount; ++i) {
        ExhaustSystem *ex = engine->getExhaustSystem(i);
        outflow += std::max(0.0, -ex->getFlow());
        pressureSum += ex->getSystem()->pressure();
        temperatureSum += ex->getSystem()->temperature();
    }
    const double collectorPressure = exhaustCount ? pressureSum / exhaustCount : AmbientPressure;
    const double collectorTemperature = exhaustCount ? temperatureSum / exhaustCount : AmbientTemperature;
    m_backPressureRatio = std::max(1.0, collectorPressure / AmbientPressure);

    const double total = m_settings.turbineSize + m_wastegate * WastegateConductance;
    const double turbineShare = m_settings.turbineSize / total;
    const double expansion = 1.0 - std::pow(m_backPressureRatio, -ExhaustKappa);
    const double turbineEnergy =
        outflow * turbineShare * MolarMass * ExhaustCp * collectorTemperature * expansion * TurbineEfficiency;

    // --- compressor: air that the intakes took from the boosted side during this step
    double inflow = 0.0;
    for (int i = 0; i < engine->getIntakeCount(); ++i) inflow += std::max(0.0, engine->getIntake(i)->m_flow);

    const double wheelSpeed = m_omega * WheelRadius;
    const double headRise = HeadCoefficient * wheelSpeed * wheelSpeed / (AirCp * AmbientTemperature);
    const double idealRatio = std::pow(1.0 + CompressorEfficiency * headRise, 1.0 / AirKappa);

    // The pedal is released: the blow-off valve vents the boost, the compressor runs unloaded
    const double plate = engine->getIntakeCount() ? engine->getIntake(0)->m_throttle : 1.0;
    m_blowOff = plate > 0.9;
    m_pressureRatio = m_blowOff ? 1.0 : std::max(1.0, idealRatio);

    const double compressorTemperatureRise =
        AmbientTemperature * (std::pow(m_pressureRatio, AirKappa) - 1.0) / CompressorEfficiency;
    const double compressorEnergy = inflow * MolarMass * AirCp * compressorTemperatureRise;

    const double frictionEnergy = BearingFriction * m_omega * m_omega * dt;
    m_kineticEnergy = std::max(
        0.5 * inertia * IdleOmega * IdleOmega,
        m_kineticEnergy + turbineEnergy - compressorEnergy - frictionEnergy);
    m_omega = std::sqrt(2.0 * m_kineticEnergy / inertia);

    // --- wastegate: spring actuator, opens when the boost passes the target
    const double boost = boostBar();
    const double wantedGate = std::clamp((boost - m_settings.targetBoost) / WastegateBand + 0.05, 0.0, 1.0);
    m_wastegate += (wantedGate - m_wastegate) * std::min(1.0, dt / WastegateLag);

    // --- results to the engine
    const double outletTemperature =
        AmbientTemperature + compressorTemperatureRise * (1.0 - m_settings.intercooler);
    for (int i = 0; i < engine->getIntakeCount(); ++i) {
        engine->getIntake(i)->setAmbient(AmbientPressure * m_pressureRatio, outletTemperature);
    }
    for (int i = 0; i < exhaustCount; ++i) {
        engine->getExhaustSystem(i)->setOutletScale(std::min(1.0, m_settings.turbineSize + m_wastegate * WastegateConductance));
    }
}
