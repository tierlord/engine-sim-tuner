#ifndef ATG_ENGINE_SIM_COMBUSTION_CHAMBER_H
#define ATG_ENGINE_SIM_COMBUSTION_CHAMBER_H

#include "scs.h"

#include "piston.h"
#include "gas_system.h"
#include "cylinder_head.h"
#include "units.h"
#include "fuel.h"

class Engine;
class CombustionChamber : public atg_scs::ForceGenerator {
    public:
        struct Parameters {
            Piston *Piston;
            CylinderHead *Head;
            Fuel *Fuel;
            Function *MeanPistonSpeedToTurbulence;

            double StartingPressure;
            double StartingTemperature;
            double CrankcasePressure;
        };

        struct FlameEvent {
            double lit_n = 0;
            double total_n = 0;
            double percentageLit = 0;
            double efficiency = 1.0;
            double flameSpeed = 0.0;

            double lastVolume = 0.0;
            double travel_x = 0.0;
            double travel_y = 0.0;
            GasSystem::Mix globalMix;
        };

        struct FrictionModelParams {
            double frictionCoeff = 0.06;
            double breakawayFriction = units::force(50, units::N);
            double breakawayFrictionVelocity = units::distance(0.1, units::m);
            double viscousFrictionCoefficient = units::force(20, units::N);
        };

    public:
        CombustionChamber();
        virtual ~CombustionChamber();

        void initialize(const Parameters &params);
        void destroy();
        void setEngine(Engine *engine) { m_engine = engine; }
        virtual void apply(atg_scs::SystemState *system);

        CylinderHead *getCylinderHead() const { return m_head; }
        Piston *getPiston() const { return m_piston; }

        double getFrictionForce() const;
        double getVolume() const;
        double pistonSpeed() const;
        double calculateMeanPistonSpeed() const;
        double calculateFiringPressure() const;

        bool isLit() const { return m_lit; }
        bool popLitLastFrame();

        void ignite();
        void update(double dt);
        void flow(double dt);

        double lastEventAfr() const;

        double getLastIterationExhaustFlow() const { return m_exhaustFlow; }

        // --- knock: self-ignition of the unburned gas ahead of the flame (needs a fuel with an octane number) ---
        double getKnockLevel() const { return m_knockLevel; }              // strongest recent knock, fades out
        long getKnockEventCount() const { return m_knockEvents; }
        double getKnockIntegral() const { return m_knockIntegral; }        // Livengood-Wu integral of this cycle, 1.0 = knock
        double popKnockRing(double dt, double sampleRate);                 // next audio sample of the knock "ping" [Pa]

        void resetLastTimestepExhaustFlow() { m_lastTimestepTotalExhaustFlow = 0; }
        double getLastTimestepExhaustFlow() const { return m_lastTimestepTotalExhaustFlow; }

        void resetLastTimestepIntakeFlow() { m_lastTimestepTotalIntakeFlow = 0; }
        double getLastTimestepIntakeFlow() const { return m_lastTimestepTotalIntakeFlow; }

        Function *m_meanPistonSpeedToTurbulence;
        GasSystem m_system;
        GasSystem m_intakeRunnerAndManifold;
        GasSystem m_exhaustRunnerAndPrimary;
        FlameEvent m_flameEvent;
        bool m_lit;

        FrictionModelParams m_frictionModel;

        double m_peakTemperature;
        double m_nBurntFuel;

    protected:
        double calculateFrictionForce(double v) const;
        void updateCycleStates();
        void updateKnock(double dt);

        double m_intakeFlowRate;
        double m_exhaustFlowRate;

        double m_manifoldToRunnerFlowRate;
        double m_primaryToCollectorFlowRate;
        double m_cylinderCrossSectionSurfaceArea;
        double m_cylinderWidthApproximation;

        double m_lastTimestepTotalExhaustFlow;
        double m_lastTimestepTotalIntakeFlow;
        double m_exhaustFlow;

        double m_crankcasePressure;

        double *m_pressure;
        double *m_pistonSpeed;
        static constexpr int StateSamples = 256;

        bool m_litLastFrame;

        // knock model state
        double m_knockIntegral;
        double m_knockCycleFactor;       // cycle-to-cycle scatter of the ignition delay
        double m_knockPIvc;
        double m_knockTIvc;
        bool m_knockActive;
        bool m_knockFlameStarted;
        bool m_knockFlameDone;
        bool m_knockedThisCycle;
        double m_knockLevel;
        long m_knockEvents;
        double m_knockRingAmp;
        double m_knockRingPhase;

        Piston *m_piston;
        CylinderHead *m_head;
        Engine *m_engine;
        Fuel *m_fuel;
};

#endif /* ATG_ENGINE_SIM_COMBUSTION_CHAMBER_H */
