#ifndef ATG_ENGINE_SIM_TUNING_MAP_H
#define ATG_ENGINE_SIM_TUNING_MAP_H

#include <vector>

// Table over engine speed [rpm] and load [Pa] with bilinear interpolation; outside the axes the edge value is used.
// values[speedIndex * loads.size() + loadIndex]. A single load entry makes it a plain curve over the speed.
class TuningMap {
    public:
        bool set(const std::vector<double> &speeds, const std::vector<double> &loads, const std::vector<double> &values) {
            bool valid = !speeds.empty() && !loads.empty() && values.size() == speeds.size() * loads.size();
            for (size_t i = 1; i < speeds.size() && valid; ++i) valid = speeds[i] > speeds[i - 1];
            for (size_t i = 1; i < loads.size() && valid; ++i) valid = loads[i] > loads[i - 1];
            if (!valid) { clear(); return false; }

            m_speeds = speeds; m_loads = loads; m_values = values;
            return true;
        }

        void clear() { m_speeds.clear(); m_loads.clear(); m_values.clear(); }
        bool valid() const { return !m_values.empty(); }

        double sample(double rpm, double load) const {
            size_t r0, r1, c0, c1;
            double tr, tc;
            locate(m_speeds, rpm, &r0, &r1, &tr);
            locate(m_loads, load, &c0, &c1, &tc);

            const size_t columns = m_loads.size();
            const double a = m_values[r0 * columns + c0] * (1 - tc) + m_values[r0 * columns + c1] * tc;
            const double b = m_values[r1 * columns + c0] * (1 - tc) + m_values[r1 * columns + c1] * tc;
            return a * (1 - tr) + b * tr;
        }

    private:
        static void locate(const std::vector<double> &axis, double v, size_t *lower, size_t *upper, double *t) {
            if (axis.size() == 1 || v <= axis.front()) { *lower = *upper = 0; *t = 0; return; }
            if (v >= axis.back()) { *lower = *upper = axis.size() - 1; *t = 0; return; }

            size_t i = 1;
            while (axis[i] < v) ++i;
            *lower = i - 1;
            *upper = i;
            *t = (v - axis[i - 1]) / (axis[i] - axis[i - 1]);
        }

        std::vector<double> m_speeds, m_loads, m_values;
};

#endif /* ATG_ENGINE_SIM_TUNING_MAP_H */
