#ifndef ATG_ENGINE_SIM_TUNING_CLUSTER_H
#define ATG_ENGINE_SIM_TUNING_CLUSTER_H

#include "ui_element.h"

#include "engine.h"
#include "gauge.h"
#include "labeled_gauge.h"

// Instruments for tuning: ignition advance, throttle opening, absolute manifold pressure,
// knock, engine damage and the octane number of the fuel.
class TuningCluster : public UiElement {
    public:
        TuningCluster();
        virtual ~TuningCluster();

        virtual void initialize(EngineSimApplication *app);
        virtual void destroy();

        virtual void update(float dt);
        virtual void render();

        void setEngine(Engine *engine) { m_engine = engine; }

        LabeledGauge *m_advanceGauge;
        LabeledGauge *m_throttleGauge;
        LabeledGauge *m_mapGauge;
        LabeledGauge *m_knockGauge;
        LabeledGauge *m_damageGauge;
        LabeledGauge *m_octaneGauge;

    protected:
        void setupGauge(LabeledGauge *gauge, const char *title, const char *unit, int precision,
            float min, float max, float minorStep, float majorStep) const;

        Engine *m_engine;
};

#endif /* ATG_ENGINE_SIM_TUNING_CLUSTER_H */
