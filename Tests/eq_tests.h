#pragma once
#include <cstdio>
#include <vector>
#include <cmath>
#include <algorithm>
#include "../Source/Dsp/LinearPhaseEq.h"

namespace eqt {
using namespace oc;

inline int fails = 0;
inline void check(const char* name, bool ok, const char* fmt, double a = 0, double b = 0) {
    std::printf("[%s] ", name); std::printf(fmt, a, b); std::printf("  %s\n", ok ? "OK" : "FAIL"); fails += !ok;
}
// run an input through the EQ in irregular host-sized chunks
inline void run(LinearPhaseEq& eq, std::vector<float>& l, std::vector<float>& r, const EqParams& p) {
    int N = (int)l.size(), i = 0, k = 0; const int sizes[5] = { 64, 100, 256, 37, 512 };
    while (i < N) { int m = std::min(sizes[k++ % 5], N - i); eq.process(&l[(size_t)i], &r[(size_t)i], m, p); i += m; }
}
inline float tailDb(const std::vector<float>& x, int tail) {
    double s = 0; for (size_t i = x.size() - (size_t)tail; i < x.size(); ++i) s += (double)x[i] * x[i];
    return 20.f * std::log10((float)std::sqrt(s / tail) + 1e-12f);
}
// gain of a sine through the EQ (dB), measured after settling
inline float gainAt(float sr, float f, const EqParams& p, int chan = 0, float rightScale = 1.f, float rightPhase = 0.f) {
    LinearPhaseEq eq; eq.prepare(sr);
    int N = (int)(1.2f * sr);
    std::vector<float> l((size_t)N), r((size_t)N), ref((size_t)N);
    for (int i = 0; i < N; ++i) {
        const float v = 0.3f * std::sin(2.f * kPi * f * (float)i / sr);
        ref[(size_t)i] = v; l[(size_t)i] = v;
        r[(size_t)i] = rightScale * 0.3f * std::sin(2.f * kPi * f * (float)i / sr + rightPhase);
    }
    run(eq, l, r, p);
    const std::vector<float>& o = (chan == 0) ? l : r;
    const float in = (chan == 0) ? tailDb(ref, 9600) : tailDb(ref, 9600) + 20.f * std::log10(std::max(rightScale, 1e-6f));
    return tailDb(o, 9600) - in;
}

inline int runAll() {
    const float sr = 48000.f;
    LinearPhaseEq probe; probe.prepare(sr);
    std::printf("[eq] latency %d samples (%.1f ms) at 48 kHz\n", probe.latency(), 1000.f * probe.latency() / sr);

    { // 1) flat EQ = exact delay by 'latency'
        LinearPhaseEq eq; eq.prepare(sr); int N = 12000; std::vector<float> l((size_t)N, 0.f), r((size_t)N, 0.f);
        l[0] = 1.f; r[5] = 0.5f; EqParams p; run(eq, l, r, p);
        int lat = eq.latency(); double err = 0;
        for (int i = 0; i < N; ++i) { err = std::max(err, (double)std::fabs(l[(size_t)i] - (i == lat ? 1.f : 0.f))); err = std::max(err, (double)std::fabs(r[(size_t)i] - (i == lat + 5 ? 0.5f : 0.f))); }
        check("eq", err < 2e-3, "flat = pure delay of %.0f samples, max error %.5f", lat, err);
    }
    EqParams bell; bell.band[0].on = true; bell.band[0].shape = EqBell; bell.band[0].freq = 1000.f; bell.band[0].gainDb = 6.f; bell.band[0].q = 1.f;
    { // 2) bell gains
        float g1k = gainAt(sr, 1000.f, bell), g100 = gainAt(sr, 100.f, bell), g10k = gainAt(sr, 10000.f, bell);
        check("eq", std::fabs(g1k - 6.f) < 0.4f, "bell +6 dB @1k: %+.2f dB", g1k);
        check("eq", std::fabs(g100) < 0.5f && std::fabs(g10k) < 0.5f, "far from the band: 100 Hz %+.2f dB, 10 kHz %+.2f dB", g100, g10k);
    }
    { // 3) linear phase: impulse response symmetric around the latency
        LinearPhaseEq eq; eq.prepare(sr); int N = 9000; std::vector<float> l((size_t)N, 0.f), r((size_t)N, 0.f); l[0] = 1.f;
        EqParams p = bell; p.band[1].on = true; p.band[1].shape = EqLowShelf; p.band[1].freq = 200.f; p.band[1].gainDb = -4.f; p.band[1].q = 0.7f;
        run(eq, l, r, p);
        int c = eq.latency(); double asym = 0, peak = 0;
        for (int i = 0; i < N; ++i) peak = std::max(peak, (double)std::fabs(l[(size_t)i]));
        for (int k = 1; k < 2000; ++k) asym = std::max(asym, (double)std::fabs(l[(size_t)(c + k)] - l[(size_t)(c - k)]));
        check("eq", asym < 0.01 * peak, "impulse response symmetric (linear phase): asymmetry %.2e of peak %.3f", asym, peak);
    }
    { // 4) steep low cut
        EqParams p; p.band[0].on = true; p.band[0].shape = EqLowCut; p.band[0].freq = 1000.f; p.band[0].slope = 5; p.band[0].q = 0.7071f; // 48 dB/oct
        float g250 = gainAt(sr, 250.f, p), g4k = gainAt(sr, 4000.f, p), g1k = gainAt(sr, 1000.f, p);
        check("eq", g250 < -40.f && std::fabs(g4k) < 0.4f, "low cut 1k/48dB: 250 Hz %.1f dB, 4 kHz %+.2f dB", g250, g4k);
        check("eq", std::fabs(g1k + 3.f) < 1.2f, "cut frequency is about -3 dB: %+.2f dB", g1k);
    }
    { // 5) shelves, notch
        EqParams p; p.band[0].on = true; p.band[0].shape = EqHighShelf; p.band[0].freq = 5000.f; p.band[0].gainDb = 6.f; p.band[0].q = 0.7071f;
        float hi = gainAt(sr, 15000.f, p), lo = gainAt(sr, 300.f, p);
        check("eq", std::fabs(hi - 6.f) < 0.8f && std::fabs(lo) < 0.4f, "high shelf +6: 15 kHz %+.2f dB, 300 Hz %+.2f dB", hi, lo);
        EqParams n; n.band[0].on = true; n.band[0].shape = EqNotch; n.band[0].freq = 2000.f; n.band[0].q = 4.f;
        float g = gainAt(sr, 2000.f, n);
        check("eq", g < -25.f, "notch 2k: %.1f dB at the notch", g);
    }
    { // 6) placement: Side band only affects the side signal; Left band only the left channel
        EqParams p = bell; p.band[0].place = EqSide;
        float mid  = gainAt(sr, 1000.f, p, 0, 1.f, 0.f);          // L = R -> pure mid
        float side = gainAt(sr, 1000.f, p, 0, 1.f, 3.14159265f);  // L = -R -> pure side
        check("eq", std::fabs(mid) < 0.4f && std::fabs(side - 6.f) < 0.5f, "Side band: mid %+.2f dB, side %+.2f dB", mid, side);
        EqParams q = bell; q.band[0].place = EqLeft;
        float gl = gainAt(sr, 1000.f, q, 0), gr = gainAt(sr, 1000.f, q, 1);
        check("eq", std::fabs(gl - 6.f) < 0.5f && std::fabs(gr) < 0.4f, "Left band: L %+.2f dB, R %+.2f dB", gl, gr);
    }
    { // 7) no clicks while moving a band
        LinearPhaseEq eq; eq.prepare(sr); int N = (int)(1.5f * sr);
        std::vector<float> l((size_t)N), r((size_t)N);
        for (int i = 0; i < N; ++i) l[(size_t)i] = r[(size_t)i] = 0.3f * std::sin(2.f * kPi * 1000.f * (float)i / sr);
        EqParams p; int i = 0; float maxStep = 0;
        std::vector<float> out((size_t)N);
        while (i < N) {
            int m = std::min(128, N - i);
            p.band[0].on = true; p.band[0].freq = 1000.f; p.band[0].q = 1.f; p.band[0].gainDb = (i > (int)sr / 2) ? 12.f : 0.f;   // switch at 0.5 s
            std::vector<float> a(l.begin() + i, l.begin() + i + m), b(r.begin() + i, r.begin() + i + m);
            eq.process(a.data(), b.data(), m, p);
            std::copy(a.begin(), a.end(), out.begin() + i); i += m;
        }
        for (int k = eq.latency() + 2; k < N; ++k) maxStep = std::max(maxStep, std::fabs(out[(size_t)k] - out[(size_t)k - 1]));
        check("eq", maxStep < 0.30f, "+12 dB step while playing: max sample jump %.3f (a click would be > 0.4)", maxStep);
    }
    { // 8) other sample rates
        LinearPhaseEq e96; e96.prepare(96000.f); LinearPhaseEq e44; e44.prepare(44100.f);
        float g = gainAt(96000.f, 1000.f, bell);
        check("eq", std::fabs(g - 6.f) < 0.5f, "96 kHz bell +6 @1k: %+.2f dB (latency %.0f samples)", g, (double)e96.latency());
        std::printf("          44.1 kHz latency %d samples (%.1f ms)\n", e44.latency(), 1000.f * e44.latency() / 44100.f);
    }
    return fails;
}
} // namespace eqt
