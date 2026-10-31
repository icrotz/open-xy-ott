#include "core.h"
#include "ott_tables.h"
#include <opxy/q31.hpp>

namespace ott {
namespace {
const opxy::i32 kMaximum = opxy::q31::maximum, kMinimum = -kMaximum - 1;
const opxy::i32 kGainApplyShift = 31 - kGainQ;   // Q26 gain -> sample scale: <<5

opxy::i32 clamp32(opxy::i64 v) {
    if (v > kMaximum) return kMaximum;
    if (v < kMinimum) return kMinimum;
    return static_cast<opxy::i32>(v);
}

opxy::i32 absq(opxy::i32 x) { return x < 0 ? (x == kMinimum ? kMaximum : -x) : x; }

unsigned msb(opxy::u32 v) { unsigned r = 0; while (v >>= 1) ++r; return r; }  // 0..31 (v>0)

// Amplitude (Q31) from a Q31 power value: sqrt(p/2^31)*2^31 = isqrt(p << 31).
// Integer bitwise sqrt of a <=62-bit value; only 64-bit add/shift-by-constant,
// no 64-bit multiply or divide. Called a few times per block, never per sample.
opxy::i32 amp_from_power(opxy::i32 power) {
    opxy::i64 n = static_cast<opxy::i64>(power < 0 ? 0 : power) << 31;
    opxy::i64 res = 0, bit = static_cast<opxy::i64>(1) << 60;  // highest even bit
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= res + bit) { n -= res + bit; res = (res >> 1) + bit; }
        else res >>= 1;
        bit >>= 2;
    }
    return static_cast<opxy::i32>(res);
}

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

// Baked compression gain (Q26) for a band envelope level, blended toward unity
// by effDepth (Q31): gain = unity + effDepth * (base - unity).
opxy::i32 band_gain(int b, opxy::i32 env, opxy::i32 eff_depth) {
    opxy::u32 e = env < 1 ? 1u : static_cast<opxy::u32>(env);
    unsigned ex = msb(e);                        // 0..30
    unsigned mant = ex >= 3 ? (e >> (ex - 3)) & (kMant - 1) : 0;
    unsigned idx = ex * kMant + mant;
    const opxy::i32 base = kGainLut[b][idx];
    return kGainUnity + opxy::q31::multiply(eff_depth, base - kGainUnity);
}

// In/Out gain: param (Q31, 0.5 = unity) applied as a 0..2x linear gain.
inline opxy::i32 apply_gain(opxy::i32 x, opxy::i32 p) {
    return clamp32(static_cast<opxy::i64>(opxy::q31::multiply(x, p)) << 1);
}
}

void initialize(State& state) {
    for (int ch = 0; ch < 2; ++ch)
        for (int s = 0; s < 9; ++s)
            state.x1[ch][s] = state.x2[ch][s] = state.y1[ch][s] = state.y2[ch][s] = 0;
    for (int b = 0; b < 3; ++b) { state.env[b] = 0; state.gain[b] = kGainUnity; }
}

void process(State& state, opxy::Channel left, opxy::Channel right, opxy::Parameters parameters) {
    const opxy::i32 depth = parameters[0] < 0 ? 0 : parameters[0];
    const opxy::i32 time  = parameters[1] < 0 ? 0 : parameters[1];
    const opxy::i32 pin = parameters[2], pout = parameters[3];
    const opxy::i32 bandp[3] = {parameters[4], parameters[5], parameters[6]};
    // Detect menu (enum): 0 = Peak (amplitude), 1 = RMS (mean-square/power).
    const bool rms = (parameters[7] >> 30) != 0;

    // Per-band attack/release coefficients, interpolated by the Time macro, and
    // the per-block gain target + ramp from the current envelope. In RMS mode
    // the envelope holds power, so convert it to amplitude for the gain LUT.
    opxy::i32 atk[3], rel[3], g[3], gstep[3];
    for (int b = 0; b < 3; ++b) {
        atk[b] = kAtkFast[b] + opxy::q31::multiply(time, kAtkSlow[b] - kAtkFast[b]);
        rel[b] = kRelFast[b] + opxy::q31::multiply(time, kRelSlow[b] - kRelFast[b]);
        opxy::i32 bd = bandp[b] < 0 ? 0 : bandp[b];
        opxy::i32 eff = opxy::q31::multiply(depth, bd);
        opxy::i32 level = rms ? amp_from_power(state.env[b]) : state.env[b];
        opxy::i32 target = band_gain(b, level, eff);
        g[b] = state.gain[b];
        gstep[b] = (target - state.gain[b]) / static_cast<opxy::i32>(opxy::block_frames);
        state.gain[b] = target;
    }

    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        const opxy::i32 xl = apply_gain(left[i], pin);
        const opxy::i32 xr = apply_gain(right[i], pin);

        // Split each channel into low / mid / high (LR4 + allpass, sums flat).
        opxy::i32 bl[3], br[3];
        for (int ch = 0; ch < 2; ++ch) {
            const opxy::i32 x = ch ? xr : xl;
            opxy::i32* x1 = state.x1[ch]; opxy::i32* x2 = state.x2[ch];
            opxy::i32* y1 = state.y1[ch]; opxy::i32* y2 = state.y2[ch];
            opxy::i32 lo = biquad(kLP_F1, x,  x1[0], x2[0], y1[0], y2[0]);
            lo           = biquad(kLP_F1, lo, x1[1], x2[1], y1[1], y2[1]);
            lo           = biquad(kAP_F2, lo, x1[2], x2[2], y1[2], y2[2]);
            opxy::i32 up = biquad(kHP_F1, x,  x1[3], x2[3], y1[3], y2[3]);
            up           = biquad(kHP_F1, up, x1[4], x2[4], y1[4], y2[4]);
            opxy::i32 mid = biquad(kLP_F2, up,  x1[5], x2[5], y1[5], y2[5]);
            mid           = biquad(kLP_F2, mid, x1[6], x2[6], y1[6], y2[6]);
            opxy::i32 hi  = biquad(kHP_F2, up,  x1[7], x2[7], y1[7], y2[7]);
            hi            = biquad(kHP_F2, hi,  x1[8], x2[8], y1[8], y2[8]);
            opxy::i32* dst = ch ? br : bl;
            dst[0] = lo; dst[1] = mid; dst[2] = hi;
        }

        opxy::i64 outl = 0, outr = 0;
        for (int b = 0; b < 3; ++b) {
            // Stereo-linked detector feeds the envelope for next block: peak
            // amplitude (max |x|) or RMS power (max x^2), per the Detect menu.
            opxy::i32 d, dr;
            if (rms) { d = opxy::q31::multiply(bl[b], bl[b]); dr = opxy::q31::multiply(br[b], br[b]); }
            else     { d = absq(bl[b]); dr = absq(br[b]); }
            if (dr > d) d = dr;
            opxy::i32 diff = d - state.env[b];
            state.env[b] += opxy::q31::multiply(diff, diff > 0 ? atk[b] : rel[b]);

            g[b] += gstep[b];
            outl += static_cast<opxy::i64>(opxy::q31::multiply(bl[b], g[b])) << kGainApplyShift;
            outr += static_cast<opxy::i64>(opxy::q31::multiply(br[b], g[b])) << kGainApplyShift;
        }
        left[i]  = apply_gain(clamp32(outl), pout);
        right[i] = apply_gain(clamp32(outr), pout);
    }
}
}
