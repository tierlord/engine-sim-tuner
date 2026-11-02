#ifndef ATG_ENGINE_SIM_VALVETRAIN_H
#define ATG_ENGINE_SIM_VALVETRAIN_H

#include <vector>

class Camshaft;
class Function;
class Valvetrain {
public:
    Valvetrain();
    virtual ~Valvetrain();

    virtual double intakeValveLift(int cylinder) = 0;
    virtual double exhaustValveLift(int cylinder) = 0;

    virtual Camshaft *getActiveIntakeCamshaft() = 0;
    virtual Camshaft *getActiveExhaustCamshaft() = 0;

    // --- cam profile switching (VTEC): defaults for valvetrains with a single profile ---
    virtual bool hasHighLift() const { return false; }
    virtual bool isHighLiftActive() const { return false; }

    // switching point of the high-lift profile; safe to change while the simulation runs
    virtual void setSwitching(double minRpm, double hysteresis, double minThrottle, double minManifoldPressure) {
        (void)minRpm; (void)hysteresis; (void)minThrottle; (void)minManifoldPressure;
    }

    // replace the lobe profiles of the camshafts (nullptr: leave as is); the "high" ones only exist with VTEC
    virtual void setLobeProfiles(Function *intakeLow, Function *exhaustLow, Function *intakeHigh, Function *exhaustHigh) = 0;

    // all camshafts of this valvetrain (the VTEC ones too), for the centreline controls
    virtual void getCamshafts(std::vector<Camshaft *> *intake, std::vector<Camshaft *> *exhaust) = 0;
};

#endif /* ATG_ENGINE_SIM_VALVETRAIN_H */
