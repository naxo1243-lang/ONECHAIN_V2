#include <cstdio>
#include <functional>
#include "../Source/Dsp/OptoCompressor.h"
#include "../Source/Dsp/MasterChain.h"
#include "../Source/Dsp/PitchCorrector.h"
#include "eq_tests.h"
#include "engine_tests.h"
using namespace oc;

static const float sr = 48000.f;
static int fails = 0;
static void check(const char* name, bool ok, const char* fmt, double a = 0, double b = 0) {
    std::printf("[%s] ", name); std::printf(fmt, a, b); std::printf("  %s\n", ok ? "OK" : "FAIL"); fails += !ok;
}
static float rmsDbTail(const std::vector<float>& x, int tail) {
    double s = 0; for (size_t i = x.size() - tail; i < x.size(); ++i) s += (double)x[i] * x[i];
    return 20.f * std::log10(std::sqrt(s / tail) + 1e-12);
}
struct Stereo { std::vector<float> l, r; };
static Stereo sine(float f, float a, int N, float phaseR = 0.f) {
    Stereo s; s.l.resize(N); s.r.resize(N);
    for (int i = 0; i < N; ++i) { s.l[i] = a * std::sin(2 * kPi * f * i / sr); s.r[i] = a * std::sin(2 * kPi * f * i / sr + phaseR); }
    return s;
}
static void runMaster(MasterChain& m, Stereo& s, const MasterChain::Params& p) {
    int N = (int)s.l.size();
    for (int i = 0; i < N; i += 256) m.process(&s.l[i], &s.r[i], std::min(256, N - i), p);
}

int main() {
    // ---------------- Opto v2 ----------------
    {
        OptoCompressor c; c.prepare(sr); int N = (int)(2 * sr);
        auto run = [&](float amp, float link, float mix, float ampR) {
            c.reset(); auto s = sine(1000.f, amp, N); for (auto& v : s.r) v *= ampR / amp;
            c.process(s.l.data(), s.r.data(), N, 60.f, 0.f, false, link, mix, false);
            return s;
        };
        auto loud = run(0.7f, 1.f, 1.f, 0.7f);
        float gLoud = rmsDbTail(loud.l, 4800) - rmsDbTail(sine(1000.f, 0.7f, 4800).l, 4800);
        check("opto", gLoud < -3.f, "loud signal compressed: %.2f dB", gLoud);
        auto dry = run(0.7f, 1.f, 0.f, 0.7f);
        auto ref = sine(1000.f, 0.7f, N); double d = 0; for (int i = 0; i < N; ++i) d += std::fabs(dry.l[i] - ref.l[i]);
        check("opto", d < 1e-3, "mix=0 equals dry (sum abs diff %.6f)", d);
        auto lk0 = run(0.7f, 0.f, 1.f, 0.02f);   // L loud, R quiet, unlinked
        float rGain = rmsDbTail(lk0.r, 4800) - rmsDbTail(sine(1000.f, 0.02f, 4800).l, 4800);
        auto lk1 = run(0.7f, 1.f, 1.f, 0.02f);
        float rGain1 = rmsDbTail(lk1.r, 4800) - rmsDbTail(sine(1000.f, 0.02f, 4800).l, 4800);
        check("opto", rGain > -0.5f && rGain1 < -3.f, "link 0%%: quiet side %.2f dB; link 100%%: %.2f dB", rGain, rGain1);
    }
    // ---------------- Multiband flat sum ----------------
    {
        double worst = 0;
        for (float f : {60.f, 120.f, 400.f, 1000.f, 3000.f, 6000.f, 12000.f}) {
            Multiband mb; mb.prepare(sr); Multiband::Params p; int N = (int)(sr);
            auto s = sine(f, 0.3f, N);
            for (int i = 0; i < N; i += 512) mb.process(&s.l[i], &s.r[i], std::min(512, N - i), p);
            float g = rmsDbTail(s.l, 9600) - rmsDbTail(sine(f, 0.3f, 9600).l, 9600);
            worst = std::max(worst, (double)std::fabs(g));
        }
        check("multiband", worst < 0.3, "neutral bands sum flat, worst deviation %.3f dB", worst);
        (void)worst;
        Multiband mb; mb.prepare(sr); Multiband::Params p; p.b[0].gainDb = 6.f; int N = (int)sr;
        auto s = sine(60.f, 0.1f, N);
        for (int i = 0; i < N; i += 512) mb.process(&s.l[i], &s.r[i], std::min(512, N - i), p);
        float g = rmsDbTail(s.l, 9600) - rmsDbTail(sine(60.f, 0.1f, 9600).l, 9600);
        check("multiband", std::fabs(g - 6.f) < 0.7f, "low band +6 dB at 60 Hz: %+.2f dB", g);
    }
    // ---------------- Master: auto-gain ----------------
    {
        MasterChain m; m.prepare(sr); MasterChain::Params p;
        for (int b = 0; b < 3; ++b) { p.mb.b[b].comp = 70; p.mb.b[b].sat = 60; }
        p.glueMix = 1; p.glueThr = -20; p.glueRatio = 4; p.driveDb = 6; p.autoGain = true; p.ceilingDb = 0;
        int N = (int)(6 * sr);
        auto s = sine(800.f, 0.25f, N); auto in = s;
        // add a second tone so signal is not a pure sine
        for (int i = 0; i < N; ++i) { float v = 0.15f * std::sin(2 * kPi * 3100.f * i / sr); s.l[i] += v; s.r[i] += v; in.l[i] += v; }
        runMaster(m, s, p);
        float gdb = rmsDbTail(s.l, 24000) - rmsDbTail(in.l, 24000);
        check("master", std::fabs(gdb) < 1.5f, "heavy processing + auto-gain: level change %+.2f dB (target 0)", gdb);
        MasterChain m2; m2.prepare(sr); p.autoGain = false; auto s2 = in; runMaster(m2, s2, p);
        float g2 = rmsDbTail(s2.l, 24000) - rmsDbTail(in.l, 24000);
        std::printf("          without auto-gain the same settings change level by %+.2f dB\n", g2);
        std::printf("          glue GR meter: %.2f dB\n", m.meters().glueGrDb);
    }
    // ---------------- Master: bass mono + width ----------------
    {
        MasterChain::Params p; p.bassMono = 150.f; p.autoGain = false; p.ceilingDb = 0;
        MasterChain m; m.prepare(sr); int N = (int)(2 * sr);
        auto lowS = sine(60.f, 0.3f, N, kPi);     // out-of-phase 60 Hz -> pure side
        runMaster(m, lowS, p);
        double side = 0; for (int i = N - 9600; i < N; ++i) side += std::fabs(lowS.l[i] - lowS.r[i]); side /= 9600;
        MasterChain m2; m2.prepare(sr); auto hiS = sine(3000.f, 0.3f, N, kPi); runMaster(m2, hiS, p);
        double side2 = 0; for (int i = N - 9600; i < N; ++i) side2 += std::fabs(hiS.l[i] - hiS.r[i]); side2 /= 9600;
        check("bassmono", side < 0.03 && side2 > 0.3, "60 Hz side %.4f (~0), 3 kHz side %.4f (kept)", side, side2);
        MasterChain m3; m3.prepare(sr); p.bassMono = 0; p.width = 2.f; auto w = sine(1000.f, 0.2f, N, 0.6f); auto w0 = w;
        runMaster(m3, w, p);
        double d0 = 0, d1 = 0; for (int i = N - 9600; i < N; ++i) { d0 += std::fabs(w0.l[i] - w0.r[i]); d1 += std::fabs(w.l[i] - w.r[i]); }
        check("width", d1 / d0 > 1.7, "width 200%%: side energy x%.2f", d1 / d0);
    }
    // ---------------- Limiter: true peak ----------------
    {
        // fs/4 sine at 45 degrees: samples reach only 0.707*A but the true peak is A
        MasterChain m; m.prepare(sr); MasterChain::Params p; p.ceilingDb = -1.f; p.autoGain = false;
        int N = (int)sr; Stereo s; s.l.resize(N); s.r.resize(N);
        for (int i = 0; i < N; ++i) s.l[i] = s.r[i] = 1.4f * std::sin(2 * kPi * (sr / 4.f) * i / sr + kPi / 4.f);
        runMaster(m, s, p);
        // reference true-peak: 16x windowed sinc interpolation (64 taps)
        float tp = 0, sp = 0; int a = 4000;
        for (int i = a; i < N - 64; ++i) {
            sp = std::max(sp, std::fabs(s.l[i]));
            for (int k = 0; k < 16; ++k) {
                float f = k / 16.f, y = 0; for (int j = -31; j <= 32; ++j) { float x = f - j; float sc = std::fabs(x) < 1e-6f ? 1.f : std::sin(kPi * x) / (kPi * x); float w = 0.5f * (1.f + std::cos(kPi * x / 32.f)); y += s.l[i + j] * sc * w; }
                tp = std::max(tp, std::fabs(y));
            }
        }
        float lim = dbToLin(-1.f);
        check("limiter", tp <= lim * 1.02f, "true peak %.4f (ceiling %.4f)", tp, lim);
        std::printf("          sample peak %.4f\n", sp);
    }
    // ---------------- Latency constant when sections are off ----------------
    {
        MasterChain m; m.prepare(sr);
        std::printf("[latency] master %d samples (%.2f ms), pitch ", m.latency(), 1000.f * m.latency() / sr);
        PitchCorrector pc; pc.prepare(sr); std::printf("%d samples (%.1f ms)\n", pc.latency(), 1000.f * pc.latency() / sr);
    }
    std::printf("\n--- linear-phase EQ ---\n");
    fails += eqt::runAll();
    fails += engt::runAll();
    std::printf(fails ? "\n%d test(s) FAILED\n" : "\nAll DSP tests passed\n", fails);
    return fails;
}
