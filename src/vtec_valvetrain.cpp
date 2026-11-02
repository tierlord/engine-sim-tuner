#include "../include/vtec_valvetrain.h"

#include "../include/engine.h"
#include "../include/camshaft.h"

VtecValvetrain::VtecValvetrain() {
    m_intakeCamshaft = nullptr;
    m_exhaustCamshaft = nullptr;

    m_vtecIntakeCamshaft = nullptr;
    m_vtecExhaustCamshaft = nullptr;

    m_engine = nullptr;

    m_minRpm = 0.0;
    m_minSpeed = 0.0;
    m_minThrottlePosition = 0.0;
    m_manifoldVacuum = 0.0;
    m_hysteresis = 0.0;
    m_active = false;
}

VtecValvetrain::~VtecValvetrain() {
    /* void */
}

void VtecValvetrain::initialize(const Parameters &parameters) {
    m_intakeCamshaft = parameters.intakeCamshaft;
    m_exhaustCamshaft = parameters.exhaustCamshaft;
    m_vtecIntakeCamshaft = parameters.vtecIntakeCamshaft;
    m_vtecExhaustCamshaft = parameters.vtexExhaustCamshaft;

    m_minRpm = parameters.minRpm;
    m_minSpeed = parameters.minSpeed;
    m_minThrottlePosition = parameters.minThrottlePosition;
    m_manifoldVacuum = parameters.manifoldVacuum;
    m_hysteresis = parameters.hysteresis;
    m_engine = parameters.engine;
}

double VtecValvetrain::intakeValveLift(int cylinder) {
    return isVtecEnabled()
        ? m_vtecIntakeCamshaft->valveLift(cylinder)
        : m_intakeCamshaft->valveLift(cylinder);
}

double VtecValvetrain::exhaustValveLift(int cylinder) {
    return isVtecEnabled()
        ? m_vtecExhaustCamshaft->valveLift(cylinder)
        : m_exhaustCamshaft->valveLift(cylinder);
}

Camshaft *VtecValvetrain::getActiveIntakeCamshaft() {
    return isVtecEnabled()
        ? m_vtecIntakeCamshaft
        : m_intakeCamshaft;
}

Camshaft *VtecValvetrain::getActiveExhaustCamshaft() {
    return isVtecEnabled()
        ? m_vtecExhaustCamshaft
        : m_exhaustCamshaft;
}

void VtecValvetrain::setSwitching(double minRpm, double hysteresis, double minThrottle, double minManifoldPressure) {
    m_minRpm = minRpm;
    m_hysteresis = hysteresis;
    m_minThrottlePosition = minThrottle;
    m_manifoldVacuum = minManifoldPressure;
}

void VtecValvetrain::setLobeProfiles(Function *intakeLow, Function *exhaustLow, Function *intakeHigh, Function *exhaustHigh) {
    if (intakeLow != nullptr) m_intakeCamshaft->setLobeProfile(intakeLow);
    if (exhaustLow != nullptr) m_exhaustCamshaft->setLobeProfile(exhaustLow);
    if (intakeHigh != nullptr) m_vtecIntakeCamshaft->setLobeProfile(intakeHigh);
    if (exhaustHigh != nullptr) m_vtecExhaustCamshaft->setLobeProfile(exhaustHigh);
}

// The high-lift profile engages above minRpm (with the other conditions met) and is released again
// once the speed has dropped by the hysteresis, so it does not flutter around the switching point.
bool VtecValvetrain::isVtecEnabled() const {
    const double speed = m_engine->getSpeed();
    const bool conditions =
        m_engine->getManifoldPressure() > m_manifoldVacuum
        && (1 - m_engine->getThrottle()) > m_minThrottlePosition;

    if (m_active) {
        if (speed < m_minRpm - m_hysteresis || !conditions) m_active = false;
    }
    else if (speed > m_minRpm && conditions) {
        m_active = true;
    }

    return m_active;
}
