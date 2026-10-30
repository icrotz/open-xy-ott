// Sound checks for the OTT multiband compressor, run by
// python sdk.py test --machine <folder>.
#include "../fx.cpp"
#include <cstdio>
#include <cmath>
#include <cstdlib>

namespace {
const opxy::i32 kUnity = 1 << 30;        // param 0.5 -> unity in/out gain
const opxy::i32 kFull  = opxy::q31::maximum;

void params(opxy::i32 p[8], opxy::i32 depth) {
    p[0] = depth; p[1] = kUnity;         // depth, time
    p[2] = kUnity; p[3] = kUnity;        // in, out gain (unity)
    p[4] = kFull; p[5] = kFull; p[6] = kFull; p[7] = 0;  // low/mid/high full
}
bool fail(const char* m) { std::puts(m); return false; }
opxy::i32 tone(unsigned n, double f, double a) {
    return (opxy::i32)(a * std::sin(2.0 * M_PI * f * n / 44100.0));
}

// Run a 1 kHz tone of amplitude `amp` for `blocks`, return peak |out| over the
// last 8 blocks (after the envelope/gain settle).
double measure(double amp, opxy::i32 depth, unsigned blocks) {
    static ott::State st; ott::initialize(st);
    opxy::i32 p[8]; params(p, depth);
    unsigned n = 0; double peak = 0;
    for (unsigned b = 0; b < blocks; ++b) {
        opxy::i32 l[64], r[64];
        for (unsigned i = 0; i < 64; ++i) l[i] = r[i] = tone(n + i, 1000.0, amp);
        ott::process(st, l, r, p);
        if (b >= blocks - 8)
            for (unsigned i = 0; i < 64; ++i) { double a = std::fabs((double)l[i]); if (a > peak) peak = a; }
        n += 64;
    }
    return peak;
}

bool test_silence() {
    static ott::State st; ott::initialize(st);
    opxy::i32 p[8]; params(p, kFull);
    for (unsigned b = 0; b < 64; ++b) {
        opxy::i32 l[64] = {0}, r[64] = {0};
        ott::process(st, l, r, p);
        for (unsigned i = 0; i < 64; ++i) if (l[i] || r[i]) return fail("FAIL silence in -> sound out");
    }
    return true;
}

bool test_upward() {   // a quiet tone should come out louder at full depth
    const double amp = 0x02000000;   // ~ -24 dBFS
    double in_peak = amp;            // ~sine peak
    double out = measure(amp, kFull, 96);
    if (out < in_peak * 2.0) return fail("FAIL upward: quiet signal not boosted");
    return true;
}

bool test_downward() { // a loud tone should come out quieter at full depth
    const double amp = 0x60000000;   // ~ -1.3 dBFS
    double out = measure(amp, kFull, 96);
    if (out > amp * 0.9) return fail("FAIL downward: loud signal not attenuated");
    return true;
}

bool test_depth_off() { // depth 0 should pass the signal ~unchanged (unity)
    const double amp = 0x20000000;
    double out = measure(amp, 0, 64);
    if (out < amp * 0.6 || out > amp * 1.4) return fail("FAIL depth 0 not ~unity");
    return true;
}

bool test_tail() {     // after input stops the output must not run away, and
                       // must decay well below the drive (boosted floor is OK)
    const double drive = 0x30000000;
    static ott::State st; ott::initialize(st);
    opxy::i32 p[8]; params(p, kFull);
    unsigned n = 0; double peak = 0;
    for (unsigned b = 0; b < 300; ++b) {
        opxy::i32 l[64], r[64];
        for (unsigned i = 0; i < 64; ++i) l[i] = r[i] = b < 64 ? tone(n + i, 1000.0, drive) : 0;
        ott::process(st, l, r, p);
        if (b >= 292) for (unsigned i = 0; i < 64; ++i) { double a = std::fabs((double)l[i]); if (a > peak) peak = a; }
        n += 64;
    }
    std::printf("tail peak (last blocks) = %.0f (%.1f dB below drive)\n", peak, 20.0 * std::log10(peak / drive + 1e-12));
    if (peak > drive * 0.25) return fail("FAIL tail runs away / not decaying");
    return true;
}
}

int main() {
    if (!test_silence()) return 1;
    if (!test_depth_off()) return 1;
    if (!test_upward()) return 1;
    if (!test_downward()) return 1;
    if (!test_tail()) return 1;
    return 0;
}
