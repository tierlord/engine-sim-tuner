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
    constexpr double TurbineEfficiency = 0.68;     // peak values, reached at the design wheel speed
    constexpr double CompressorEfficiency = 0.72;
    constexpr double WheelRadius = 0.030;          // [m] compressor wheel of the reference turbo (turbine size 0.35)
    constexpr double DesignWheelSpeed = 450.0;     // [m/s] tip speed with the best efficiency
    constexpr double HeadCoefficient = 0.55;
    constexpr double BaseInertia = 8.0E-5;         // [kg m^2]
    constexpr double BearingFriction = 5.0E-6;     // [W / (rad/s)^2]
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
    m_airFlow = 0.0;
    m_whistlePhase = m_humPhase = 0.0;
    m_rushLow = m_rushBand = 0.0;
    m_bovLevel = 0.0;
    m_lastBlowOffPressureRatio = 1.0;
    m_toneSmooth = m_humSmooth = m_rushSmooth = 0.0;
    m_noiseState = 12345u;
}

void Turbo::setSettings(const Settings &s) {
    m_settings = s;
    m_settings.targetBoost = std::clamp(s.targetBoost, 0.05, 3.0);
    m_settings.turbineSize = std::clamp(s.turbineSize, 0.1, 1.0);
    m_settings.inertiaScale = std::clamp(s.inertiaScale, 0.2, 5.0);
    m_settings.intercooler = std::clamp(s.intercooler, 0.0, 1.0);
    m_settings.soundVolume = std::clamp(s.soundVolume, 0.0, 3.0);
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

    // A bigger turbo has bigger wheels; the efficiency is poor while the wheels are slow and rises steeply
    // towards the design tip speed. This is what makes a spooling turbo accelerate itself.
    const double wheelRadius = WheelRadius * std::sqrt(m_settings.turbineSize / 0.35);
    const double wheelSpeed = m_omega * wheelRadius;
    const double speedShare = std::min(1.0, wheelSpeed / DesignWheelSpeed);
    const double turbineEfficiency = TurbineEfficiency * (0.45 + 0.55 * std::pow(speedShare, 1.5));
    const double compressorEfficiency = CompressorEfficiency * (0.55 + 0.45 * speedShare);

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

    // Even the biggest turbine housing is a real restriction in the exhaust
    const double turbineConductance = 0.10 + 0.32 * m_settings.turbineSize;
    const double total = turbineConductance + m_wastegate * WastegateConductance;
    const double turbineShare = turbineConductance / total;
    const double expansion = 1.0 - std::pow(m_backPressureRatio, -ExhaustKappa);
    const double turbineEnergy =
        outflow * turbineShare * MolarMass * ExhaustCp * collectorTemperature * expansion * turbineEfficiency;

    // --- compressor: air that the intakes took from the boosted side during this step
    double inflow = 0.0;
    for (int i = 0; i < engine->getIntakeCount(); ++i) inflow += std::max(0.0, engine->getIntake(i)->m_flow);

    m_airFlow += (inflow / dt - m_airFlow) * std::min(1.0, dt / 0.02);

    const double headRise = HeadCoefficient * wheelSpeed * wheelSpeed / (AirCp * AmbientTemperature);
    const double idealRatio = std::pow(1.0 + compressorEfficiency * headRise, 1.0 / AirKappa);

    // The pedal is released: the blow-off valve vents the boost, the compressor runs unloaded
    const double plate = engine->getIntakeCount() ? engine->getIntake(0)->m_throttle : 1.0;
    const bool wasBlowOff = m_blowOff;
    m_blowOff = plate > 0.9;
    if (m_blowOff && !wasBlowOff && m_pressureRatio > 1.1) m_bovLevel = std::min(1.0, (m_pressureRatio - 1.0) / 1.2);
    m_pressureRatio = m_blowOff ? 1.0 : std::max(1.0, idealRatio);

    const double compressorTemperatureRise =
        AmbientTemperature * (std::pow(m_pressureRatio, AirKappa) - 1.0) / compressorEfficiency;
    const double compressorEnergy = inflow * MolarMass * AirCp * compressorTemperatureRise;

    const double frictionEnergy = BearingFriction * m_omega * m_omega * dt;
    m_kineticEnergy = std::max(
        0.5 * inertia * IdleOmega * IdleOmega,
        m_kineticEnergy + turbineEnergy - compressorEnergy - frictionEnergy);
    m_omega = std::sqrt(2.0 * m_kineticEnergy / inertia);

    // --- wastegate: spring actuator, opens when the boost passes the target
    const double boost = boostBar();
    m_activeTarget = engine->getBoostMap()->valid()
        ? engine->getBoostMap()->sample(engine->getRpm(), 0.0)
        : m_settings.targetBoost;
    const double wantedGate = std::clamp((boost - m_activeTarget) / WastegateBand + 0.05, 0.0, 1.0);
    m_wastegate += (wantedGate - m_wastegate) * std::min(1.0, dt / WastegateLag);

    // --- results to the engine
    const double outletTemperature =
        AmbientTemperature + compressorTemperatureRise * (1.0 - m_settings.intercooler);
    for (int i = 0; i < engine->getIntakeCount(); ++i) {
        engine->getIntake(i)->setAmbient(AmbientPressure * m_pressureRatio, outletTemperature);
    }
    for (int i = 0; i < exhaustCount; ++i) {
        engine->getExhaustSystem(i)->setOutletScale(std::min(1.0, 0.10 + 0.32 * m_settings.turbineSize + m_wastegate * WastegateConductance));
    }
}

void Turbo::mixAudio(int16_t *buffer, int samples, double sampleRate) {
    if (!m_settings.enabled || m_settings.soundVolume <= 0.0) {
        m_bovLevel = 0.0;
        return;
    }

    constexpr double TwoPi = 6.283185307179586;
    constexpr int BladeCount = 5;                 // blade pass frequency = shaft frequency x blades
    constexpr double WhistleGain = 2200.0;
    constexpr double HumGain = 700.0;
    constexpr double RushGain = 2600.0;
    constexpr double BovGain = 9000.0;

    const double shaftHz = m_omega / TwoPi;
    const double whistleHz = std::min(shaftHz * BladeCount, 15000.0);
    const double load = std::clamp(m_airFlow / 6.0, 0.0, 1.5) * (m_blowOff ? 0.3 : 1.0);

    // Targets, the audible levels follow them with a short lag so that nothing clicks
    const double spin = std::clamp((m_omega - 2000.0) / 8000.0, 0.0, 1.0);
    const double toneTarget = spin * (0.15 + 0.85 * std::clamp(load, 0.0, 1.0)) * std::min(1.0, 0.3 + (m_pressureRatio - 1.0));
    const double humTarget = spin * 0.6;
    const double rushTarget = std::clamp(load, 0.0, 1.5) * std::sqrt(m_pressureRatio);
    const double smooth = 1.0 - std::exp(-1.0 / (0.03 * sampleRate));
    const double bovDecay = std::exp(-1.0 / (0.30 * sampleRate));

    for (int i = 0; i < samples; ++i) {
        m_toneSmooth += (toneTarget - m_toneSmooth) * smooth;
        m_humSmooth += (humTarget - m_humSmooth) * smooth;
        m_rushSmooth += (rushTarget - m_rushSmooth) * smooth;

        m_noiseState = m_noiseState * 1664525u + 1013904223u;
        const double noise = ((m_noiseState >> 8) & 0xFFFF) / 32768.0 - 1.0;

        m_whistlePhase += TwoPi * whistleHz / sampleRate;
        m_humPhase += TwoPi * shaftHz / sampleRate;
        if (m_whistlePhase > TwoPi) m_whistlePhase -= TwoPi;
        if (m_humPhase > TwoPi) m_humPhase -= TwoPi;

        const double whistle = std::sin(m_whistlePhase) + 0.25 * std::sin(2.0 * m_whistlePhase);
        const double hum = std::sin(m_humPhase) + 0.5 * std::sin(2.0 * m_humPhase);

        // band limited noise for the air rush (difference of two one-pole filters)
        m_rushLow += (noise - m_rushLow) * 0.35;
        m_rushBand += (m_rushLow - m_rushBand) * 0.05;
        const double rush = m_rushLow - m_rushBand;

        // blow-off: hissing burst, brighter than the rush
        const double bov = noise * m_bovLevel * m_bovLevel;
        m_bovLevel *= bovDecay;

        const double v =
            m_settings.soundVolume * (
                WhistleGain * m_toneSmooth * whistle
                + HumGain * m_humSmooth * hum
                + RushGain * m_rushSmooth * rush
                + BovGain * bov);

        buffer[i] = (int16_t)std::clamp((double)buffer[i] + v, -32768.0, 32767.0);
    }
}
