#ifndef ATG_ENGINE_SIM_VTEC_STANDARD_VALVETRAIN_H
#define ATG_ENGINE_SIM_VTEC_STANDARD_VALVETRAIN_H

#include "valvetrain.h"

class Engine;
class VtecValvetrain : public Valvetrain {
public:
    struct Parameters {
        double minRpm;
        double minSpeed;
        double manifoldVacuum;
        double minThrottlePosition;
        double hysteresis = 0.0;      // [rad/s] the profile switches back this far below minRpm

        Camshaft *intakeCamshaft;
        Camshaft *exhaustCamshaft;

        Camshaft *vtecIntakeCamshaft;
        Camshaft *vtexExhaustCamshaft;

        Engine *engine;
    };

public:
    VtecValvetrain();
    virtual ~VtecValvetrain();

    void initialize(const Parameters &parameters);

    virtual double intakeValveLift(int cylinder) override;
    virtual double exhaustValveLift(int cylinder) override;

    virtual Camshaft *getActiveIntakeCamshaft() override;
    virtual Camshaft *getActiveExhaustCamshaft() override;

    virtual bool hasHighLift() const override { return true; }
    virtual bool isHighLiftActive() const override { return isVtecEnabled(); }
    virtual void setSwitching(double minRpm, double hysteresis, double minThrottle, double minManifoldPressure) override;
    virtual void setLobeProfiles(Function *intakeLow, Function *exhaustLow, Function *intakeHigh, Function *exhaustHigh) override;
    virtual void getCamshafts(std::vector<Camshaft *> *intake, std::vector<Camshaft *> *exhaust) override;

private:
    bool isVtecEnabled() const;

    Camshaft *m_intakeCamshaft;
    Camshaft *m_exhaustCamshaft;

    Camshaft *m_vtecIntakeCamshaft;
    Camshaft *m_vtecExhaustCamshaft;

    Engine *m_engine;

    double m_minRpm;
    double m_minSpeed;
    double m_manifoldVacuum;
    double m_minThrottlePosition;
    double m_hysteresis;
    mutable bool m_active;           // profile in use; switching back needs the hysteresis
};

#endif /* ATG_ENGINE_SIM_VTEC_STANDARD_VALVETRAIN_H */
