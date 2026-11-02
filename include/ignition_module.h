#ifndef ATG_ENGINE_SIM_IGNITION_MODULE_H
#define ATG_ENGINE_SIM_IGNITION_MODULE_H

#include "part.h"

#include "crankshaft.h"
#include "function.h"
#include "units.h"

#include <vector>

class IgnitionModule : public Part {
    public:
        struct Parameters {
            int cylinderCount;
            Crankshaft *crankshaft;
            Function *timingCurve;
            double revLimit = units::rpm(6000.0);
            double limiterDuration = 0.5 * units::sec;
        };

        struct SparkPlug {
            double angle = 0;
            bool ignitionEvent = false;
            bool enabled = false;
        };

    public:
        IgnitionModule();
        virtual ~IgnitionModule();

        virtual void destroy();

        void initialize(const Parameters &params);
        void setFiringOrder(int cylinderIndex, double angle);
        void reset();
        void update(double dt);

        bool getIgnitionEvent(int index) const;
        void resetIgnitionEvents();

        double getTimingAdvance();

        // Safe to change while the simulation runs (read every update())
        void setTimingCurve(Function *curve) { m_timingCurve = curve; }
        Function *getTimingCurve() const { return m_timingCurve; }
        void setRevLimit(double revLimit) { m_revLimit = revLimit; }
        double getRevLimit() const { return m_revLimit; }

        // Ignition map: advance [rad] over engine speed [rpm] and manifold pressure [Pa].
        // values is row-major, one row per speed: values[speed * loads.size() + load].
        // While a map is set it replaces the timing curve. Safe to change while the simulation runs.
        void setTimingMap(
            const std::vector<double> &speeds,
            const std::vector<double> &loads,
            const std::vector<double> &values);
        void clearTimingMap();
        bool hasTimingMap() const { return !m_mapSpeeds.empty(); }

        // Engine load for the map (manifold pressure in Pa), set before every update()
        void setLoadPressure(double pressure) { m_loadPressure = pressure; }
        double getFilteredLoadPressure() const { return m_filteredLoad; }

        bool m_enabled;

    protected:
        SparkPlug *getPlug(int i);
        double sampleTimingMap(double rpm, double pressure) const;

        Function *m_timingCurve;

        std::vector<double> m_mapSpeeds;
        std::vector<double> m_mapLoads;
        std::vector<double> m_mapValues;
        double m_loadPressure;
        double m_filteredLoad;
        SparkPlug *m_plugs;
        Crankshaft *m_crankshaft;
        int m_cylinderCount;

        double m_lastCrankshaftAngle;
        double m_revLimit;
        double m_revLimitTimer;
        double m_limiterDuration;
};

#endif /* ATG_ENGINE_SIM_IGNITION_MODULE_H */
