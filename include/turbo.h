#ifndef ATG_ENGINE_SIM_TURBO_H
#define ATG_ENGINE_SIM_TURBO_H

class Engine;

// Exhaust gas turbocharger: the turbine sits in the exhaust outlet (restriction -> back pressure, the
// pulses drive it), the compressor raises the pressure and temperature of the air in front of the intakes.
// Shaft energy balance: d(KE) = E_turbine - E_compressor - E_friction.
class Turbo {
    public:
        struct Settings {
            bool enabled = false;
            double targetBoost = 0.8;      // [bar] gauge pressure at which the wastegate opens
            double turbineSize = 0.35;     // outlet conductance through the turbine relative to the open exhaust
            double inertiaScale = 1.0;     // 1 = medium sized turbo, larger = slower spool
            double intercooler = 0.7;      // share of the compression heat that is removed
        };

        Turbo();

        void setSettings(const Settings &s);
        inline const Settings &settings() const { return m_settings; }

        // Called once per fluid step after the exhaust systems and intakes were processed
        void step(Engine *engine, double dt);

        inline double shaftRpm() const { return m_omega * 9.5492965855; }
        inline double pressureRatio() const { return m_pressureRatio; }
        inline double boostBar() const { return (m_pressureRatio - 1.0) * 1.01325; }
        inline double wastegate() const { return m_wastegate; }
        inline bool blowOff() const { return m_blowOff; }
        inline double backPressureRatio() const { return m_backPressureRatio; }

    private:
        Settings m_settings;
        double m_kineticEnergy;
        double m_omega;
        double m_pressureRatio;
        double m_backPressureRatio;
        double m_wastegate;
        bool m_blowOff;
        bool m_applied;
};

#endif /* ATG_ENGINE_SIM_TURBO_H */
