#include "../include/cam_lobe.h"

#include "../include/constants.h"
#include "../include/units.h"

#include <cmath>

Function *generateHarmonicCamLobe(double durationAt50Thou, double gamma, double lift, int steps) {
    if (!(durationAt50Thou > 0.0) || !(lift > 0.0) || !(gamma > 0.0) || steps < 10) return nullptr;

    const double angle = durationAt50Thou / 4;
    const double s = std::pow(2 * units::distance(50, units::thou) / lift, 1 / gamma) - 1;
    if (!(std::fabs(s) < 1.0)) return nullptr;                 // lift below 0.050 in: the duration is undefined

    const double k = std::acos(s) / angle;
    const double extents = constants::pi / k;
    const double step = extents / (steps - 5.0);

    Function *function = new Function;
    function->initialize(1 + 2 * (steps - 1), step);

    for (int i = 0; i < steps; ++i) {
        if (i == 0) {
            function->addSample(0.0, lift);
        }
        else {
            const double x = i * step;
            const double value = (x >= extents)
                ? 0.0
                : lift * std::pow(0.5 + 0.5 * std::cos(k * x), gamma);
            function->addSample(x, value);
            function->addSample(-x, value);
        }
    }

    return function;
}
