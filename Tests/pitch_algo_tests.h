#pragma once
#include <cstdio>
#include <vector>
#include <cmath>
#include <algorithm>
#include "../Source/Dsp/PrecisePitch.h"
#include "../Source/Dsp/WavesPitch.h"

namespace pat {
using namespace oc;

inline int fails = 0;
inline void check(const char* name, bool ok, const char* fmt, double a = 0, double b = 0, double c = 0) {
    std::printf("[%s] ", name); std::printf(fmt, a, b, c); std::printf("  %s\n", ok ? "OK" : "FAIL"); fails += !ok;
}
inline float zcFreq(const std::vector<float>& x, int from, float sr) {
    int cross = 0, first = -1, last = -1;
    for (int i = from + 1; i < (int)x.size(); ++i) if (x[(size_t)i - 1] < 0 && x[(size_t)i] >= 0) { if (first < 0) first = i; last = i; ++cross; }
    return cross > 1 ? (float)(cross - 1) * sr / (float)(last - first) : 0.f;
}
inline std::vector<float> sine(float f, float sr, int N, float a = 0.4f) {
    std::vector<float> x((size_t)N); for (int i = 0; i < N; ++i) x[(size_t)i] = a * std::sin(2.f * kPi * f * (float)i / sr); return x;
}
// f0 track (in cents re 440 Hz) of a signal, using the same detector as the engines
inline std::vector<float> trackCents(const std::vector<float>& x, float sr, float fromSec) {
    YinDetector d; d.prepare(sr, 70.f); std::vector<float> out; const int hop = (int)(sr * 0.004f);
    for (int i = 0; i < (int)x.size(); ++i) {
        d.push(x[(size_t)i]);
        if ((i + 1) % hop == 0 && (float)i / sr >= fromSec) { float c; const float f = d.detect(c); if (f > 0.f && c > 0.85f) out.push_back(1200.f * std::log2(f / 440.f)); }
    }
    return out;
}
inline void meanStd(const std::vector<float>& v, float& mean, float& sd) {
    double s = 0; for (float x : v) s += x; mean = v.empty() ? 0.f : (float)(s / (double)v.size());
    double q = 0; for (float x : v) q += (x - mean) * (x - mean); sd = v.empty() ? 0.f : (float)std::sqrt(q / (double)v.size());
}
// vibrato tone: centre 'centreCents' re 440 Hz, +-depthCents at rateHz
inline std::vector<float> vibratoTone(float sr, int N, float centreCents, float depthCents, float rateHz) {
    std::vector<float> x((size_t)N); double ph = 0;
    for (int i = 0; i < N; ++i) {
        const float cents = centreCents + depthCents * std::sin(2.f * kPi * rateHz * (float)i / sr);
        ph += 2.0 * 3.14159265358979 * 440.0 * std::pow(2.0, cents / 1200.0) / sr;
        x[(size_t)i] = 0.35f * (float)(std::sin(ph) + 0.4 * std::sin(2 * ph) + 0.2 * std::sin(3 * ph));
    }
    return x;
}
template <class F> inline void chunks(std::vector<float>& l, std::vector<float>& r, F fn, int chunk = 128) {
    const int N = (int)l.size(); for (int i = 0; i < N; i += chunk) fn(&l[(size_t)i], &r[(size_t)i], std::min(chunk, N - i));
}

inline int runAll() {
    const float sr = 48000.f;
    std::printf("\n--- Precise (PSOLA) ---\n");
    { // exact tuning
        PrecisePitch pp; pp.prepare(sr); int N = (int)(3 * sr); auto l = sine(224.f, sr, N); auto r = l;
        PitchParams p; p.speedMs = 5; p.scale = 0; chunks(l, r, [&](float* a, float* b, int n) { pp.process(a, b, n, p); });
        const float f = zcFreq(l, (int)(2 * sr), sr); const double cents = 1200.0 * std::log2(f / 220.f);
        check("precise", std::fabs(cents) < 1.0, "224 Hz -> 220 Hz: error %+.3f cents", cents);
        double worst = 0; const float ins[] = { 110.f, 147.f, 261.f, 330.f, 392.f, 523.f }, tg[] = { 110.f, 146.83f, 261.63f, 329.63f, 392.f, 523.25f };
        for (int k = 0; k < 6; ++k) {
            PrecisePitch q; q.prepare(sr); auto a = sine(ins[k], sr, N); auto b = a; PitchParams pr; pr.speedMs = 5; pr.scale = 1;
            chunks(a, b, [&](float* x, float* y, int n) { q.process(x, y, n, pr); });
            worst = std::max(worst, (double)std::fabs(1200.f * std::log2(zcFreq(a, (int)(2 * sr), sr) / tg[k])));
        }
        check("precise", worst < 2.0, "6 notes in C major: worst error %.3f cents", worst);
    }
    for (int range = 0; range < 3; ++range) { // latency measured vs reported (pitch off = delayed dry)
        static const float mh[3] = { 80.f, 100.f, 150.f };
        PrecisePitch pp; pp.prepare(sr); pp.setMinHz(mh[range]); int N = (int)(0.6f * sr), pos = 300;
        std::vector<float> l((size_t)N, 0.f), r((size_t)N, 0.f); l[(size_t)pos] = r[(size_t)pos] = 0.3f;
        PitchParams p; p.on = false; chunks(l, r, [&](float* a, float* b, int n) { pp.process(a, b, n, p); });
        int idx = 0; float mx = 0; for (int i = 0; i < N; ++i) if (std::fabs(l[(size_t)i]) > mx) { mx = std::fabs(l[(size_t)i]); idx = i; }
        check("precise", std::abs((idx - pos) - pp.latency()) <= 1, "range min %3.0f Hz: latency reported %.0f smp, measured %.0f", mh[range], (double)pp.latency(), (double)(idx - pos));
        std::printf("          = %.1f ms\n", 1000.0 * pp.latency() / sr);
    }
    { // switching the tuner on in the middle of a tone must not leave a silent gap
        PrecisePitch pp; pp.prepare(sr); int N = (int)(3 * sr); auto l = sine(300.f, sr, N); auto r = l; PitchParams p; p.speedMs = 5; p.scale = 0;
        int i = 0; while (i < N) { int n = std::min(128, N - i); p.on = (i >= (int)(1.0f * sr)); pp.process(&l[(size_t)i], &r[(size_t)i], n, p); i += n; }
        const int w = (int)(0.002f * sr); float mn = 1e9f; for (int k = (int)(0.6f * sr); k + w < N; k += w) { double s = 0; for (int j = 0; j < w; ++j) s += l[(size_t)(k + j)] * l[(size_t)(k + j)]; mn = std::min(mn, (float)std::sqrt(s / w)); }
        const float nominal = 0.4f / std::sqrt(2.f);
        check("precise", mn > 0.6f * nominal, "tuner switched on mid-tone: quietest 2 ms window %.2f of nominal (no gap)", mn / nominal);
    }
    { // PSOLA keeps formants: vowel centroid after a big shift vs the shift of a naive shifter is checked in Experimental; here sanity of voiced/unvoiced
        PrecisePitch pp; pp.prepare(sr); int N = (int)(2 * sr); std::vector<float> l((size_t)N), r; unsigned s = 5;
        for (auto& v : l) { s = s * 1664525u + 1013904223u; v = 0.2f * ((float)(s >> 8) / 8388608.f - 1.f); } r = l;
        PitchParams p; chunks(l, r, [&](float* a, float* b, int n) { pp.process(a, b, n, p); });
        double e0 = 0, e1 = 0; { unsigned t = 5; for (int i = (int)sr; i < N; ++i) { t = t * 1664525u + 1013904223u; float v = 0.2f * ((float)(t >> 8) / 8388608.f - 1.f); e0 += (double)v * v; } }
        for (int i = (int)sr; i < N; ++i) e1 += (double)l[(size_t)i] * l[(size_t)i];
        check("precise", std::fabs(10 * std::log10(e1 / e0)) < 1.5, "noise stays noise (level change %+.2f dB)", 10 * std::log10(e1 / e0));
    }

    std::printf("\n--- Waves style ---\n");
    for (int range = 0; range < 3; ++range) {
        WavesPitch wp; wp.prepare(sr); wp.setRange(range); int N = (int)(0.5f * sr), pos = 300;
        std::vector<float> l((size_t)N, 0.f), r((size_t)N, 0.f); l[(size_t)pos] = r[(size_t)pos] = 0.3f;
        PitchParams p; WavesParams w; chunks(l, r, [&](float* a, float* b, int n) { wp.process(a, b, n, p, w); });
        int idx = 0; float mx = 0; for (int i = 0; i < N; ++i) if (std::fabs(l[(size_t)i]) > mx) { mx = std::fabs(l[(size_t)i]); idx = i; }
        static const char* nm[3] = { "Low   (70 Hz+)", "Medium (100 Hz+)", "High (150 Hz+)" };
        char lbl[64]; std::snprintf(lbl, sizeof lbl, "waves %-16s", nm[range]);
        check(lbl, std::abs((idx - pos) - wp.latency()) <= 1, "latency reported %.0f smp, measured %.0f", (double)wp.latency(), (double)idx - pos);
        std::printf("          = %.1f ms\n", 1000.0 * wp.latency() / sr);
    }
    { // vibrato: centre 20 cents flat of A4, +-40 cents vibrato at 5.5 Hz
        const int N = (int)(4 * sr); auto in = vibratoTone(sr, N, -20.f, 40.f, 5.5f);
        auto run = [&](float vib, float tol, float speed, std::vector<float>& out) {
            WavesPitch wp; wp.prepare(sr); wp.setRange(1); out = in; auto r = in; PitchParams p; p.scale = 0; p.speedMs = speed; WavesParams w; w.vibrato = vib; w.tolCents = tol; w.transMs = 60;
            chunks(out, r, [&](float* a, float* b, int n) { wp.process(a, b, n, p, w); });
        };
        float mi, si; { auto t = trackCents(in, sr, 1.5f); meanStd(t, mi, si); }
        std::vector<float> o1, o2; run(1.f, 0.f, 30.f, o1); run(0.f, 0.f, 10.f, o2);
        float m1, s1, m2, s2; { auto t = trackCents(o1, sr, 2.0f); meanStd(t, m1, s1); } { auto t = trackCents(o2, sr, 2.0f); meanStd(t, m2, s2); }
        std::printf("          input: mean %+.1f cents, vibrato std %.1f cents\n", mi, si);
        check("waves", std::fabs(m1) < 5.f && s1 > 0.7f * si && s1 < 1.3f * si, "vibrato 100%%: note centre %+.1f cents, vibrato std %.1f", m1, s1);
        check("waves", std::fabs(m2) < 5.f && s2 < 0.45f * si, "vibrato   0%%: note centre %+.1f cents, vibrato std %.1f (ironed)", m2, s2);
    }
    { // tolerance: a steady tone 12 cents flat
        const int N = (int)(3 * sr); auto in = vibratoTone(sr, N, -12.f, 0.f, 5.f);
        auto run = [&](float tol) { WavesPitch wp; wp.prepare(sr); wp.setRange(1); auto l = in, r = in; PitchParams p; p.scale = 0; p.speedMs = 20; WavesParams w; w.tolCents = tol;
            chunks(l, r, [&](float* a, float* b, int n) { wp.process(a, b, n, p, w); }); auto t = trackCents(l, sr, 1.5f); float m, s; meanStd(t, m, s); return m; };
        const float left = run(20.f), fixed = run(3.f);
        check("waves", std::fabs(left + 12.f) < 4.f && std::fabs(fixed) < 4.f, "tolerance 20c: stays %+.1f cents; tolerance 3c: corrected to %+.1f", left, fixed);
    }
    { // speed: how far the correction has got 120 ms after the note starts (35 cents flat of A4)
        const int N = (int)(2 * sr); std::vector<float> in((size_t)N, 0.f); { auto t = vibratoTone(sr, N, -35.f, 0.f, 5.f); for (int i = (int)(0.5f * sr); i < N; ++i) in[(size_t)i] = t[(size_t)i]; }
        auto errAt = [&](float speed) { WavesPitch wp; wp.prepare(sr); wp.setRange(1); auto l = in, r = in; PitchParams p; p.scale = 0; p.speedMs = speed; WavesParams w;
            chunks(l, r, [&](float* a, float* b, int n) { wp.process(a, b, n, p, w); });
            std::vector<float> seg(l.begin() + (int)(0.5f * sr + 0.12f * sr), l.begin() + (int)(0.5f * sr + 0.20f * sr));
            YinDetector d; d.prepare(sr, 70.f); std::vector<float> c; const int hop = (int)(sr * 0.004f);
            for (int i = 0; i < (int)seg.size(); ++i) { d.push(seg[(size_t)i]); if ((i + 1) % hop == 0 && i > (int)(0.03f * sr)) { float cf; float f = d.detect(cf); if (f > 0.f && cf > 0.85f) c.push_back(1200.f * std::log2(f / 440.f)); } }
            float m, s; meanStd(c, m, s); return std::fabs(m); };
        const float fast = errAt(1.f), slow = errAt(250.f);
        check("waves", fast < 8.f && slow > fast + 8.f, "speed 1 ms: %.1f cents off at 120-200 ms; speed 250 ms: %.1f cents off", fast, slow);
    }
    return fails;
}
} // namespace pat
