#ifndef ATG_ENGINE_SIM_CAM_LOBE_H
#define ATG_ENGINE_SIM_CAM_LOBE_H

#include "function.h"

// Harmonic cam lobe, the same profile the script function harmonic_cam_lobe() builds
// (see GenerateHarmonicCamLobeNode in scripting/include/actions.h), so a lobe can be rebuilt while the
// engine runs. Angles are in radians of the camshaft, the lift in metres.
//   durationAt50Thou: duration at 0.050 in valve lift, in crankshaft radians
// Returns nullptr if the lift is too small for the given duration to make sense.
Function *generateHarmonicCamLobe(double durationAt50Thou, double gamma, double lift, int steps);

#endif /* ATG_ENGINE_SIM_CAM_LOBE_H */
