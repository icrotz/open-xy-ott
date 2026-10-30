#include "core.h"
#include "ott_tables.h"
#include <opxy/q31.hpp>

namespace ott {
namespace {
const opxy::i32 kMaximum = opxy::q31::maximum, kMinimum = -kMaximum - 1;
const opxy::i32 kGainApplyShift = 31 - kGainQ;   // Q26 gain -> sample scale: <<5

// Attack/release one-pole coefficients (Q31), per sample.
const opxy::i32 kAttack = 42949673;              // ~0.02  (fast attack)
const opxy::i32 kRelFast = 8589935;              // ~0.004 (time = 0)
const opxy::i32 kRelSpan = 7945620;              // down to ~0.0003 (time = 1)

opxy::i32 clamp32(opxy::i64 v) {
    if (v > kMaximum) return kMaximum;
    if (v < kMinimum) return kMinimum;
    return static_cast<opxy::i32>(v);
}

opxy::i32 absq(opxy::i32 x) { return x < 0 ? (x == kMinimum ? kMaximum : -x) : x; }

unsigned msb(opxy::u32 v) { unsigned r = 0; while (v >>= 1) ++r; return r; }  // 0..31 (v>0)

// One Direct Form I biquad sample; coeffs are stored /SCALE, so the result is
// shifted back by kScaleShift. Updates the four history words.
inline opxy::i32 biquad(const opxy::i32 c[5], opxy::i32 x,
                        opxy::i32& x1, opxy::i32& x2, opxy::i32& y1, opxy::i32& y2) {
    opxy::i64 acc = opxy::q31::multiply(c[0], x);
    acc += opxy::q31::multiply(c[1], x1);
    acc += opxy::q31::multiply(c[2], x2);
    acc -= opxy::q31::multiply(c[3], y1);
    acc -= opxy::q31::multiply(c[4], y2);
    const opxy::i32 y = clamp32(acc << kScaleShift);
    x2 = x1; x1 = x; y2 = y1; y1 = y;
    return y;
}

// Baked compression gain (Q26) for an envelope level, blended toward unity by
// effDepth (Q31): gain = unity + effDepth * (base - unity).
opxy::i32 band_gain(opxy::i32 env, opxy::i32 eff_depth) {
    opxy::u32 e = env < 1 ? 1u : static_cast<opxy::u32>(env);
    unsigned ex = msb(e);                        // 0..30
    unsigned mant = ex >= 3 ? (e >> (ex - 3)) & (kMant - 1) : 0;
    unsigned idx = ex * kMant + mant;
    const opxy::i32 base = kGainLut[idx];
    return kGainUnity + opxy::q31::multiply(eff_depth, base - kGainUnity);
}

// In/Out gain: param (Q31, 0.5 = unity) applied as a 0..2x linear gain.
inline opxy::i32 apply_gain(opxy::i32 x, opxy::i32 p) {
    return clamp32(static_cast<opxy::i64>(opxy::q31::multiply(x, p)) << 1);
}
}

void initialize(State& state) {
    for (int ch = 0; ch < 2; ++ch)
        for (int f = 0; f < 4; ++f)
            state.x1[ch][f] = state.x2[ch][f] = state.y1[ch][f] = state.y2[ch][f] = 0;
    for (int b = 0; b < 3; ++b) { state.env[b] = 0; state.gain[b] = kGainUnity; }
}

void process(State& state, opxy::Channel left, opxy::Channel right, opxy::Parameters parameters) {
    const opxy::i32 depth = parameters[0] < 0 ? 0 : parameters[0];
    const opxy::i32 time  = parameters[1] < 0 ? 0 : parameters[1];
    const opxy::i32 pin = parameters[2], pout = parameters[3];
    const opxy::i32 bandp[3] = {parameters[4], parameters[5], parameters[6]};
    const opxy::i32 release = kRelFast - opxy::q31::multiply(time, kRelSpan);

    // Per-block gain target and ramp for each band, from the current envelope.
    opxy::i32 g[3], gstep[3];
    for (int b = 0; b < 3; ++b) {
        opxy::i32 bd = bandp[b] < 0 ? 0 : bandp[b];
        opxy::i32 eff = opxy::q31::multiply(depth, bd);
        opxy::i32 target = band_gain(state.env[b], eff);
        g[b] = state.gain[b];
        gstep[b] = (target - state.gain[b]) / static_cast<opxy::i32>(opxy::block_frames);
        state.gain[b] = target;
    }

    const opxy::i32 (&c0)[5] = kLP1; const opxy::i32 (&c1)[5] = kHP1;
    const opxy::i32 (&c2)[5] = kLP2; const opxy::i32 (&c3)[5] = kHP2;

    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        const opxy::i32 xl = apply_gain(left[i], pin);
        const opxy::i32 xr = apply_gain(right[i], pin);

        // Split both channels into low / mid / high.
        opxy::i32 bl[3], br[3];
        bl[0] = biquad(c0, xl, state.x1[0][0], state.x2[0][0], state.y1[0][0], state.y2[0][0]);
        br[0] = biquad(c0, xr, state.x1[1][0], state.x2[1][0], state.y1[1][0], state.y2[1][0]);
        {
            opxy::i32 ml = biquad(c1, xl, state.x1[0][1], state.x2[0][1], state.y1[0][1], state.y2[0][1]);
            opxy::i32 mr = biquad(c1, xr, state.x1[1][1], state.x2[1][1], state.y1[1][1], state.y2[1][1]);
            bl[1] = biquad(c2, ml, state.x1[0][2], state.x2[0][2], state.y1[0][2], state.y2[0][2]);
            br[1] = biquad(c2, mr, state.x1[1][2], state.x2[1][2], state.y1[1][2], state.y2[1][2]);
        }
        bl[2] = biquad(c3, xl, state.x1[0][3], state.x2[0][3], state.y1[0][3], state.y2[0][3]);
        br[2] = biquad(c3, xr, state.x1[1][3], state.x2[1][3], state.y1[1][3], state.y2[1][3]);

        opxy::i64 outl = 0, outr = 0;
        for (int b = 0; b < 3; ++b) {
            // Stereo-linked peak detector feeds the envelope for next block.
            opxy::i32 d = absq(bl[b]); opxy::i32 dr = absq(br[b]); if (dr > d) d = dr;
            opxy::i32 diff = d - state.env[b];
            state.env[b] += opxy::q31::multiply(diff, diff > 0 ? kAttack : release);

            g[b] += gstep[b];
            outl += static_cast<opxy::i64>(opxy::q31::multiply(bl[b], g[b])) << kGainApplyShift;
            outr += static_cast<opxy::i64>(opxy::q31::multiply(br[b], g[b])) << kGainApplyShift;
        }
        left[i]  = apply_gain(clamp32(outl), pout);
        right[i] = apply_gain(clamp32(outr), pout);
    }
}
}
