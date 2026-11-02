#include "../include/tuning_cluster.h"

#include "../include/units.h"
#include "../include/constants.h"
#include "../include/engine_sim_application.h"

#include <cmath>

TuningCluster::TuningCluster() {
    m_advanceGauge = nullptr;
    m_throttleGauge = nullptr;
    m_mapGauge = nullptr;
    m_knockGauge = nullptr;
    m_damageGauge = nullptr;
    m_octaneGauge = nullptr;
    m_engine = nullptr;
}

TuningCluster::~TuningCluster() {
    /* void */
}

void TuningCluster::setupGauge(
    LabeledGauge *gauge,
    const char *title,
    const char *unit,
    int precision,
    float min,
    float max,
    float minorStep,
    float majorStep) const
{
    gauge->m_title = title;
    gauge->m_unit = unit;
    gauge->m_precision = precision;
    gauge->setLocalPosition({ 0, 0 });
    gauge->m_gauge->m_min = min;
    gauge->m_gauge->m_max = max;
    gauge->m_gauge->m_minorStep = minorStep;
    gauge->m_gauge->m_majorStep = majorStep;
    gauge->m_gauge->m_maxMinorTick = 1000000;
    gauge->m_gauge->m_thetaMin = (float)constants::pi * 1.2f;
    gauge->m_gauge->m_thetaMax = -(float)constants::pi * 0.2f;
    gauge->m_gauge->m_needleWidth = 4.0f;
    gauge->m_gauge->m_gamma = 1.0f;
    gauge->m_gauge->m_needleKs = 1000.0f;
    gauge->m_gauge->m_needleKd = 20.0f;
    gauge->m_gauge->setBandCount(0);
}

void TuningCluster::initialize(EngineSimApplication *app) {
    UiElement::initialize(app);

    constexpr float shortenAngle = (float)units::angle(1.0, units::deg);

    m_advanceGauge = addElement<LabeledGauge>();
    setupGauge(m_advanceGauge, "IGNITION ADV.", "deg", 1, 0.0f, 60.0f, 2.0f, 10.0f);

    m_throttleGauge = addElement<LabeledGauge>();
    setupGauge(m_throttleGauge, "THROTTLE", "%", 0, 0.0f, 100.0f, 5.0f, 10.0f);
    m_throttleGauge->m_spaceBeforeUnit = false;

    m_mapGauge = addElement<LabeledGauge>();
    setupGauge(m_mapGauge, "MAP ABS.", "kPa", 0, 0.0f, 250.0f, 10.0f, 50.0f);

    // Knock: the strongest recent knock of all cylinders. Red from a clearly audible level on.
    m_knockGauge = addElement<LabeledGauge>();
    setupGauge(m_knockGauge, "KNOCK", "%", 0, 0.0f, 100.0f, 5.0f, 10.0f);
    m_knockGauge->m_spaceBeforeUnit = false;
    m_knockGauge->m_gauge->setBandCount(2);
    m_knockGauge->m_gauge->setBand(
        { m_app->getOrange(), 10.0f, 30.0f, 3.0f, 6.0f, shortenAngle, shortenAngle }, 0);
    m_knockGauge->m_gauge->setBand(
        { m_app->getRed(), 30.0f, 100.0f, 3.0f, 6.0f, shortenAngle, -shortenAngle }, 1);

    m_damageGauge = addElement<LabeledGauge>();
    setupGauge(m_damageGauge, "ENGINE DAMAGE", "%", 0, 0.0f, 100.0f, 5.0f, 10.0f);
    m_damageGauge->m_spaceBeforeUnit = false;
    m_damageGauge->m_gauge->setBandCount(2);
    m_damageGauge->m_gauge->setBand(
        { m_app->getOrange(), 25.0f, 60.0f, 3.0f, 6.0f, shortenAngle, shortenAngle }, 0);
    m_damageGauge->m_gauge->setBand(
        { m_app->getRed(), 60.0f, 100.0f, 3.0f, 6.0f, shortenAngle, -shortenAngle }, 1);

    // Octane number of the fuel (0 = no knock model)
    m_octaneGauge = addElement<LabeledGauge>();
    setupGauge(m_octaneGauge, "FUEL RON", "", 0, 80.0f, 110.0f, 1.0f, 5.0f);
}

void TuningCluster::destroy() {
    UiElement::destroy();
}

void TuningCluster::update(float dt) {
    UiElement::update(dt);
}

void TuningCluster::render() {
    Grid grid;
    grid.h_cells = 3;
    grid.v_cells = 2;

    float advance = 0.0f, throttle = 0.0f, map = 0.0f, knock = 0.0f, damage = 0.0f, octane = 0.0f;
    if (m_engine != nullptr) {
        // Ignition advance as currently looked up for the engine speed, in degrees before TDC
        advance = (float)(m_engine->getIgnitionModule()->getTimingAdvance() / units::deg);

        // The plate position is 1 when closed; the opening is the flow factor the intake uses
        const double plate = m_engine->getIntake(0)->getThrottlePlatePosition();
        throttle = (float)(100.0 * std::cos(plate * constants::pi / 2));

        map = (float)(m_engine->getManifoldPressure() / 1000.0);

        knock = (float)(100.0 * m_engine->getKnockLevel());
        damage = (float)(100.0 * m_engine->getKnockDamage());
        octane = (float)m_engine->getFuel()->getOctane();
    }

    m_advanceGauge->m_bounds = grid.get(m_bounds, 0, 0);
    m_advanceGauge->m_gauge->m_value = advance;

    m_throttleGauge->m_bounds = grid.get(m_bounds, 1, 0);
    m_throttleGauge->m_gauge->m_value = throttle;

    m_mapGauge->m_bounds = grid.get(m_bounds, 2, 0);
    m_mapGauge->m_gauge->m_value = map;

    m_knockGauge->m_bounds = grid.get(m_bounds, 0, 1);
    m_knockGauge->m_gauge->m_value = knock;

    m_damageGauge->m_bounds = grid.get(m_bounds, 1, 1);
    m_damageGauge->m_gauge->m_value = damage;

    m_octaneGauge->m_bounds = grid.get(m_bounds, 2, 1);
    m_octaneGauge->m_gauge->m_value = octane;

    UiElement::render();
}
