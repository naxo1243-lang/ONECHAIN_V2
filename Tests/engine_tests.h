#pragma once
#include <cstdio>
#include <vector>
#include <cmath>
#include <algorithm>
#include "../Source/Dsp/Engine.h"

namespace engt {
using namespace oc;

inline int fails = 0;
inline void check(const char* name, bool ok, const char* fmt, double a = 0, double b = 0, double c = 0) {
    std::printf("[%s] ", name); std::printf(fmt, a, b, c); std::printf("  %s\n", ok ? "OK" : "FAIL"); fails += !ok;
}
inline void runChunks(Engine& e, std::vector<float>& l, std::vector<float>& r, const EngineParams& p, int chunk = 128) {
    const int N = (int)l.size();
    for (int i = 0; i < N; i += chunk) {
        const int m = std::min(chunk, N - i);
        e.processFront(&l[(size_t)i], &r[(size_t)i], m, p);
        e.processBack(&l[(size_t)i], &r[(size_t)i], m, p);
    }
}

inline int runAll() {
    static const char* names[3] = { "Recording", "Original", "Mixing" };
    std::printf("\n--- whole chain: measured vs reported latency ---\n");
    for (float sr : { 44100.f, 48000.f, 96000.f }) {
        for (int mode = 0; mode < 3; ++mode) {
            Engine e; e.prepare(sr); e.setMode(mode);
            EngineParams ep; ep.mode = mode;
            const int N = (int)(0.5f * sr) + 4096, pos = 200;
            std::vector<float> l((size_t)N, 0.f), r((size_t)N, 0.f); l[(size_t)pos] = r[(size_t)pos] = 0.2f;
            runChunks(e, l, r, ep);
            int idx = 0; float mx = 0.f; for (int i = 0; i < N; ++i) if (std::fabs(l[(size_t)i]) > mx) { mx = std::fabs(l[(size_t)i]); idx = i; }
            const int measured = idx - pos, reported = e.latency();
            char label[64]; std::snprintf(label, sizeof label, "engine %5.0f Hz %-9s", sr, names[mode]);
            std::printf("[%s] reported %5d smp (%5.1f ms), measured %5d smp  %s\n", label, reported, 1000.f * reported / sr, measured,
                        std::abs(measured - reported) <= 1 ? "OK" : "FAIL");
            fails += std::abs(measured - reported) > 1;
        }
    }
    { // switching modes while audio runs must stay clean (no NaN, no explosion) and settle at unity gain
        const float sr = 48000.f; Engine e; e.prepare(sr);
        const int N = (int)(2.0f * sr); std::vector<float> l((size_t)N), r((size_t)N);
        for (int i = 0; i < N; ++i) l[(size_t)i] = r[(size_t)i] = 0.3f * std::sin(2.f * kPi * 440.f * (float)i / sr);
        EngineParams ep; ep.opto.on = false; int i = 0; bool bad = false; float mx = 0.f; int changes = 0;
        while (i < N) {
            const int m = std::min(128, N - i);
            ep.mode = (i / (int)(0.4f * sr)) % 3;
            if (e.processFront(&l[(size_t)i], &r[(size_t)i], m, ep)) ++changes;
            e.processBack(&l[(size_t)i], &r[(size_t)i], m, ep);
            for (int k = 0; k < m; ++k) { const float v = l[(size_t)(i + k)]; if (!std::isfinite(v)) bad = true; mx = std::max(mx, std::fabs(v)); }
            i += m;
        }
        double s = 0; for (int k = N - 9600; k < N; ++k) s += (double)l[(size_t)k] * l[(size_t)k];
        const float db = 20.f * std::log10((float)std::sqrt(s / 9600) / (0.3f / std::sqrt(2.f)));
        check("engine", !bad && mx < 1.0f && std::fabs(db) < 1.0f && changes >= 4, "%.0f mode switches mid-stream: finite output, peak %.2f, final level %+.2f dB", changes, mx, db);
    }
    { // algorithm x range: measured vs reported latency of the whole chain (Recording mode, pitch bypassed so an impulse shows the delay)
        static const char* an[3] = { "Original", "Precise", "Waves" };
        static const char* rn[3] = { "Low", "Medium", "High" };
        std::printf("\n--- whole chain per algorithm (Recording mode) ---\n");
        for (float sr : { 48000.f, 44100.f }) {
            for (int algo = 0; algo < 3; ++algo) {
                for (int range = 0; range < 3; ++range) {
                    Engine e; e.prepare(sr); e.setMode(ModeRecording);
                    EngineParams ep; ep.mode = ModeRecording; ep.algo = algo; ep.range = range; ep.pitch.on = false; ep.opto.on = false;
                    e.setPitch(algo, range);
                    const int N = (int)(0.6f * sr) + 4096, pos = 300;
                    std::vector<float> l((size_t)N, 0.f), r((size_t)N, 0.f); l[(size_t)pos] = r[(size_t)pos] = 0.2f;
                    runChunks(e, l, r, ep);
                    int idx = 0; float mx = 0.f; for (int i = 0; i < N; ++i) if (std::fabs(l[(size_t)i]) > mx) { mx = std::fabs(l[(size_t)i]); idx = i; }
                    const int measured = idx - pos, reported = e.latency();
                    std::printf("[engine %5.0f Hz %-8s %-6s] total latency %5d smp (%5.1f ms), measured %5d  %s\n", sr, an[algo], rn[range], reported, 1000.f * reported / sr, measured,
                                std::abs(measured - reported) <= 1 ? "OK" : "FAIL");
                    fails += std::abs(measured - reported) > 1;
                }
            }
        }
    }
    { // switching algorithms and ranges while audio runs: finite, bounded, and the tone survives
        const float sr = 48000.f; Engine e; e.prepare(sr);
        const int N = (int)(3.0f * sr); std::vector<float> l((size_t)N), r((size_t)N);
        for (int i = 0; i < N; ++i) l[(size_t)i] = r[(size_t)i] = 0.3f * std::sin(2.f * kPi * 440.f * (float)i / sr);
        EngineParams ep; ep.opto.on = false; int i = 0; bool bad = false; float mx = 0.f; int changes = 0;
        while (i < N) {
            const int m = std::min(128, N - i);
            ep.algo = (i / (int)(0.5f * sr)) % 3; ep.range = (i / (int)(0.7f * sr)) % 3;
            if (e.processFront(&l[(size_t)i], &r[(size_t)i], m, ep)) ++changes;
            e.processBack(&l[(size_t)i], &r[(size_t)i], m, ep);
            for (int k = 0; k < m; ++k) { const float v = l[(size_t)(i + k)]; if (!std::isfinite(v)) bad = true; mx = std::max(mx, std::fabs(v)); }
            i += m;
        }
        double s = 0; for (int k = N - 9600; k < N; ++k) s += (double)l[(size_t)k] * l[(size_t)k];
        const float db = 20.f * std::log10((float)std::sqrt(s / 9600) / (0.3f / std::sqrt(2.f)));
        check("engine", !bad && mx < 1.0f && std::fabs(db) < 1.5f && changes >= 8, "%.0f algorithm/range switches mid-stream: finite, peak %.2f, final level %+.2f dB", changes, mx, db);
    }
    { // Recording mode shorter than v1 (672 samples at 48 kHz)
        Engine e; e.prepare(48000.f); e.setMode(ModeRecording);
        check("engine", e.latency() < 672 / 2, "Recording latency %.0f samples vs v1 = 672 samples", (double)e.latency());
    }
    return fails;
}
} // namespace engt
