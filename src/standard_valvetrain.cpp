#include "../include/standard_valvetrain.h"

#include "../include/camshaft.h"

StandardValvetrain::StandardValvetrain() {
    m_intakeCamshaft = nullptr;
    m_exhaustCamshaft = nullptr;
}

StandardValvetrain::~StandardValvetrain() {
    /* void */
}

void StandardValvetrain::initialize(const Parameters &params) {
    m_intakeCamshaft = params.intakeCamshaft;
    m_exhaustCamshaft = params.exhaustCamshaft;
}

double StandardValvetrain::intakeValveLift(int cylinder) {
    return m_intakeCamshaft->valveLift(cylinder);
}

double StandardValvetrain::exhaustValveLift(int cylinder) {
    return m_exhaustCamshaft->valveLift(cylinder);
}

Camshaft *StandardValvetrain::getActiveIntakeCamshaft() {
    return m_intakeCamshaft;
}

Camshaft *StandardValvetrain::getActiveExhaustCamshaft() {
    return m_exhaustCamshaft;
}

void StandardValvetrain::getCamshafts(std::vector<Camshaft *> *intake, std::vector<Camshaft *> *exhaust) {
    intake->push_back(m_intakeCamshaft);
    exhaust->push_back(m_exhaustCamshaft);
}

void StandardValvetrain::setLobeProfiles(Function *intakeLow, Function *exhaustLow, Function *intakeHigh, Function *exhaustHigh) {
    (void)intakeHigh;
    (void)exhaustHigh;

    if (intakeLow != nullptr) m_intakeCamshaft->setLobeProfile(intakeLow);
    if (exhaustLow != nullptr) m_exhaustCamshaft->setLobeProfile(exhaustLow);
}
