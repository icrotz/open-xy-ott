#pragma once
#include <opxy/effect.hpp>

namespace ott {
// 3-band multiband compressor (OTT-style). Per channel, four crossover biquads
// split the signal (filter index: 0=LP@120, 1=HP@120, 2=LP@2500, 3=HP@2500;
// low=LP1, mid=LP2(HP1(x)), high=HP2). Per band a stereo-linked envelope drives
// a baked compression-gain table. State ~152 bytes, within the 476 limit.
struct State {
    opxy::i32 x1[2][4], x2[2][4], y1[2][4], y2[2][4];  // crossover histories [ch][filter]
    opxy::i32 env[3];   // per-band peak envelope (stereo-linked), Q31
    opxy::i32 gain[3];  // per-band smoothed gain, Q26
};
void initialize(State& state);
void process(State& state, opxy::Channel left, opxy::Channel right, opxy::Parameters parameters);
}
