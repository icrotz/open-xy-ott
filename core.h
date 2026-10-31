#pragma once
#include <opxy/effect.hpp>

namespace ott {
// 3-band multiband compressor (OTT-style). Per channel, nine crossover biquad
// stages split the signal with 4th-order Linkwitz-Riley crossovers plus an
// allpass so the bands sum flat (no notches):
//   low  = allpass_F2( LP_F1( LP_F1(x) ) )
//   mid  = LP_F2( LP_F2( HP_F1( HP_F1(x) ) ) )
//   high = HP_F2( HP_F2( HP_F1( HP_F1(x) ) ) )
// Stage index [ch][s]: 0,1 = low LP_F1 x2; 2 = low allpass; 3,4 = HP_F1 x2
// (shared mid/high upper path); 5,6 = mid LP_F2 x2; 7,8 = high HP_F2 x2.
// Per band a stereo-linked peak envelope drives a baked per-band compression
// gain LUT. State ~312 bytes, within the 476 limit.
struct State {
    opxy::i32 x1[2][9], x2[2][9], y1[2][9], y2[2][9];  // crossover histories [ch][stage]
    opxy::i32 env[3];   // per-band envelope (stereo-linked), Q31: amplitude in
                        // Peak mode, mean-square power in RMS mode (Detect menu)
    opxy::i32 gain[3];  // per-band smoothed gain, Q26
};
void initialize(State& state);
void process(State& state, opxy::Channel left, opxy::Channel right, opxy::Parameters parameters);
}
