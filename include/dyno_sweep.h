#ifndef ATG_ENGINE_SIM_DYNO_SWEEP_H
#define ATG_ENGINE_SIM_DYNO_SWEEP_H

#include <string>
#include <vector>

class Engine;
class Simulator;

// Automatic dyno run on the *running* simulator, so the engine can be heard while it is measured.
//
// Three modes:
//  - steps: the engine is held at one speed after the other and the torque is averaged (steady state)
//  - hold:  the engine is started and then kept at one speed (start = speed) until the run is stopped, e.g. to
//           tune the ignition at full load. Speed and throttle can be changed while it runs (setHold).
//  - ramp:  full-throttle pull from a low speed up to the rev limiter. The dynamometer lets the engine
//           accelerate at a constant rate; the torque follows from the load plus the acceleration of
//           the rotating mass (like an inertia dyno). No fixed steps, the curve is continuous.
//
// Call order each frame: update() before the simulation steps, sample() for every simulation step.
class DynoSweep {
    public:
        struct Parameters {
            double rpmStart = 1000.0;
            double rpmEnd = 6500.0;      // <= 0 in ramp mode: up to the rev limiter
            double rpmStep = 500.0;      // steps mode only
            int settleCycles = 10;       // steps mode: 720 degree cycles to wait before measuring
            int measureCycles = 10;      // steps mode: 720 degree cycles to average over
            double throttle = 1.0;       // speed control: 1.0 = wide open
            bool ramp = false;
            bool hold = false;           // hold mode: keep rpmStart until stopped
            double rampRate = 300.0;     // ramp mode: acceleration in rpm per second
        };

        struct Point {
            double rpm = 0;
            double torqueNm = 0;
            double powerKw = 0;
            double manifoldKpa = 0;
            double advanceDeg = 0;
            double knockShare = 0;       // share of the cylinder cycles that knocked (0..1)
            bool held = true;            // false if the dyno did not reach the speed in time
        };

        enum class State { Idle, Starting, Running, Done, Aborted };

        DynoSweep();

        bool start(const Parameters &params, Engine *engine, Simulator *simulator, std::string *error);
        void abort(const char *reason, Engine *engine, Simulator *simulator);
        // hold mode only: new speed [rpm] and throttle while the run is active
        bool setHold(double rpm, double throttle, Engine *engine, std::string *error);
        void update(float dt, Engine *engine, Simulator *simulator);
        void sample(Simulator *simulator, Engine *engine);

        bool isActive() const { return m_state == State::Starting || m_state == State::Running; }
        State getState() const { return m_state; }
        const char *stateName() const;
        const char *modeName() const { return m_params.hold ? "hold" : (m_params.ramp ? "ramp" : "steps"); }
        const std::vector<Point> &getPoints() const { return m_points; }

        // {"state":"running","progress":0.4,"rpm":3000,"points":[...]}
        std::string statusJson() const;

        static bool parseParameters(const std::string &text, Parameters *params);

    private:
        struct Bin {
            double torqueSum = 0;
            double speedSum = 0;
            long samples = 0;
            double manifoldSum = 0;
            double advanceSum = 0;
            int frames = 0;
            long knockEvents = 0;
        };

        void beginPoint();
        void finishPoint(Engine *engine);
        void finish(State state, const char *message, Simulator *simulator);
        void updateSteps(Engine *engine, Simulator *simulator);
        void updateRamp(Engine *engine, Simulator *simulator);
        void updateHold(Engine *engine, Simulator *simulator);
        std::vector<Point> rampPoints(bool onlyFinished) const;

        State m_state;
        Parameters m_params;
        std::string m_message;

        double m_rpm;                    // current target speed
        double m_simTime;                // simulated seconds since the start of the run
        double m_phaseStart;
        double m_settleTime;
        double m_measureTime;
        double m_extraSettle;
        bool m_measuring;
        double m_startingTime;

        double m_torqueSum;
        double m_speedSum;
        long m_samples;
        double m_manifoldSum;
        double m_advanceSum;
        int m_frames;

        // ramp mode
        bool m_rampActive;
        double m_rampStartTime;
        double m_inertia;                // moment of inertia of the rotating assembly [kg m^2]
        std::vector<Bin> m_bins;

        double m_timestep;               // simulation time step [s]
        int m_cylinders;
        long m_knockStart;               // knock events at the start of a measuring window
        long m_lastKnockEvents;          // knock events at the last frame (ramp mode)
        double m_damage;                 // engine damage at the last frame (0..1)

        // user state that is restored when the run ends
        bool m_savedDynoEnabled;
        bool m_savedDynoHold;

        std::vector<Point> m_points;
};

#endif /* ATG_ENGINE_SIM_DYNO_SWEEP_H */
