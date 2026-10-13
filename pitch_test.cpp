#include <cstdio>
#include <functional>
#include "../Source/Dsp/Experimental/PsolaPitch.h"
using namespace oc;

static float zcFreq(const std::vector<float>& x, int from, float sr) {
    int cross = 0, first = -1, last = -1;
    for (int i = from + 1; i < (int)x.size(); ++i)
        if (x[i - 1] < 0 && x[i] >= 0) { if (first < 0) first = i; last = i; ++cross; }
    return cross > 1 ? (cross - 1) * sr / (float)(last - first) : 0.f;
}
// synthetic vowel: impulse train through 3 resonators (formants 700/1220/2600 Hz)
static std::vector<float> vowel(float f0, float sr, int N) {
    struct Res { float a1, a2, g; float y1 = 0, y2 = 0; };
    auto mk = [&](float f, float bw) { float r = std::exp(-kPi * bw / sr); Res q; q.a1 = 2 * r * std::cos(2 * kPi * f / sr); q.a2 = -r * r; q.g = 1 - r; return q; };
    Res rs[3] = { mk(700, 80), mk(1220, 90), mk(2600, 120) };
    std::vector<float> x(N); double ph = 0; float per = sr / f0;
    for (int i = 0; i < N; ++i) {
        float in = 0.f; if (i >= ph) { in = 1.f; ph += per; }
        float o = 0.f;
        for (auto& q : rs) { float y = q.g * in + q.a1 * q.y1 + q.a2 * q.y2; q.y2 = q.y1; q.y1 = y; o += y; }
        x[i] = o;
    }
    float mx = 0; for (float v : x) mx = std::max(mx, std::fabs(v)); for (auto& v : x) v *= 0.5f / mx;
    return x;
}
static float centroid(const std::vector<float>& x, int from, int len, float sr) {
    double num = 0, den = 0;
    for (float f = 300; f <= 3500; f += 10) {
        double re = 0, im = 0;
        for (int i = 0; i < len; ++i) { double w = 0.5 - 0.5 * std::cos(2 * kPi * i / len); double ph = 2 * kPi * f * i / sr; re += w * x[from + i] * std::cos(ph); im -= w * x[from + i] * std::sin(ph); }
        double mag = std::sqrt(re * re + im * im); num += f * mag * mag; den += mag * mag;
    }
    return (float)(num / den);
}


static float acPitch(const std::vector<float>& x, int from, int len, float sr, float fmin = 70.f, float fmax = 450.f) {
    int lmin = (int)(sr / fmax), lmax = (int)(sr / fmin); double best = -1; int bl = lmin; std::vector<double> r(lmax + 2, 0.0);
    for (int L = lmin - 1; L <= lmax + 1; ++L) { double num = 0, d1 = 0, d2 = 0; for (int i = 0; i < len; ++i) { double a = x[from + i], b = x[from + i + L]; num += a * b; d1 += a * a; d2 += b * b; } r[L] = num / std::sqrt(d1 * d2 + 1e-12); }
    for (int L = lmin; L <= lmax; ++L) if (r[L] > best) { best = r[L]; bl = L; }
    for (int L = lmin; L < bl; ++L) if (r[L] > 0.95 * best && r[L] >= r[L - 1] && r[L] >= r[L + 1]) { bl = L; break; }  // avoid octave-down errors
    double a = r[bl - 1], b = r[bl], c = r[bl + 1], den = a - 2 * b + c; double sh = std::fabs(den) > 1e-12 ? 0.5 * (a - c) / den : 0.0;
    return sr / (float)(bl + sh);
}
static double bestLagSnr(const std::vector<float>& y, const std::vector<float>& x, int from, int len, int maxLag) {
    double best = -1e9;
    for (int lag = -maxLag; lag <= maxLag; ++lag) {
        double num = 0, d1 = 0, d2 = 0;
        for (int i = 0; i < len; ++i) { double a = y[from + i], b = x[from + i + lag]; num += a * b; d1 += a * a; d2 += b * b; }
        double c = num / std::sqrt(d1 * d2 + 1e-12); best = std::max(best, c);
    }
    return best;
}

int main() {
    const float sr = 48000.f; int fails = 0;
    auto check = [&](const char* name, bool ok, const char* fmt, double v) { std::printf("[%s] ", name); std::printf(fmt, v); std::printf("  %s\n", ok ? "OK" : "FAIL"); fails += !ok; };

    { // precision: sine 224 Hz -> chromatic A3 = 220 Hz
        PitchCorrector pc; pc.prepare(sr); int N = (int)(3 * sr);
        std::vector<float> l(N), r(N);
        for (int i = 0; i < N; ++i) l[i] = r[i] = 0.5f * std::sin(2 * kPi * 224.f * i / sr);
        PitchParams p; p.speedMs = 5; p.amount = 1; p.scale = 0;
        for (int i = 0; i < N; i += 128) pc.process(&l[i], &r[i], std::min(128, N - i), p);
        float f = zcFreq(l, (int)(2 * sr), sr);
        double cents = 1200 * std::log2(f / 220.f);
        check("pitch", std::fabs(cents) < 3.0, "224 Hz -> 220 Hz: out error %+.2f cents", cents);
    }
    { // precision across several pitches, major scale C
        float ins[] = {110.f, 147.f, 261.f, 330.f, 392.f, 523.f};
        float tgs[] = {110.f, 146.83f, 261.63f, 329.63f, 392.f, 523.25f};
        double worst = 0;
        for (int k = 0; k < 6; ++k) {
            PitchCorrector pc; pc.prepare(sr); int N = (int)(3 * sr);
            std::vector<float> l(N), r(N);
            for (int i = 0; i < N; ++i) l[i] = r[i] = 0.5f * std::sin(2 * kPi * ins[k] * i / sr);
            PitchParams p; p.speedMs = 5; p.scale = 1; p.key = 0;
            for (int i = 0; i < N; i += 128) pc.process(&l[i], &r[i], std::min(128, N - i), p);
            float f = zcFreq(l, (int)(2 * sr), sr);
            worst = std::max(worst, std::fabs(1200.0 * std::log2(f / tgs[k])));
        }
        check("pitch", worst < 4.0, "6 notes in C major, worst error %.2f cents", worst);
    }
    { // transparency + latency at the lowest voice pitch (82 Hz), ratio 1
        PsolaShifter s; s.prepare(sr, 80.f); int N = (int)(2 * sr);
        auto x = vowel(82.f, sr, N); std::vector<float> y(N);
        for (int i = 0; i < N; ++i) { float l = x[i], r = x[i]; s.processSample(l, r, 82.f, 82.f, true, 0.f, 0.f); y[i] = l; }
        int lat = s.latency(); int a = (int)sr;
        // waveform match allowing +-1 period shift (PSOLA may re-time by a fraction of a period)
        std::vector<float> xs(N, 0.f); for (int i = lat; i < N; ++i) xs[i] = x[i - lat];
        double c = bestLagSnr(y, xs, a, 12000, (int)(sr / 82.f));
        double eIn = 0, eOut = 0; for (int i = a; i < N; ++i) { eIn += (double)x[i] * x[i]; eOut += (double)y[i] * y[i]; }
        check("psola", c > 0.9, "ratio 1 @82Hz: waveform correlation %.3f", c);
        check("psola", std::fabs(10 * std::log10(eOut / eIn)) < 1.0, "ratio 1 @82Hz: level change %+.2f dB", 10 * std::log10(eOut / eIn));
    }
    { // formant preservation: vowel 120 -> 180 Hz (ratio 1.5)
        PsolaShifter s; s.prepare(sr, 80.f); int N = (int)(2 * sr);
        auto x = vowel(120.f, sr, N); std::vector<float> y(N);
        for (int i = 0; i < N; ++i) { float l = x[i], r = x[i]; s.processSample(l, r, 120.f, 180.f, true, 0.f, 0.f); y[i] = l; }
        float cin = centroid(x, (int)sr, 24000, sr), cout_ = centroid(y, (int)sr, 24000, sr);
        float zc = zcFreq(y, (int)sr, sr);
        check("formant", std::fabs(cout_ / cin - 1.f) < 0.15f, "centroid out/in = %.3f (1.0 = formants preserved)", cout_ / cin);
        std::printf("          (naive shift would give ~1.50)\n");
        (void)zc;
    }
    { // formant shift +4 semitones with unchanged pitch
        PsolaShifter s; s.prepare(sr, 80.f); int N = (int)(2 * sr);
        auto x = vowel(150.f, sr, N); std::vector<float> y(N);
        for (int i = 0; i < N; ++i) { float l = x[i], r = x[i]; s.processSample(l, r, 150.f, 150.f, true, 4.f, 0.f); y[i] = l; }
        float cin = centroid(x, (int)sr, 24000, sr), cout_ = centroid(y, (int)sr, 24000, sr);
        check("formant", cout_ / cin > 1.12f && cout_ / cin < 1.45f, "formant +4 st: centroid ratio %.3f (expect ~1.26)", cout_ / cin);
    }
    { // full corrector on a vowel: 185 Hz (between F#3/G3) in C major -> F3 or G3
        PitchCorrector pc; pc.prepare(sr); int N = (int)(3 * sr);
        auto x = vowel(185.f, sr, N); std::vector<float> r = x;
        PitchParams p; p.speedMs = 5; p.scale = 1;
        for (int i = 0; i < N; i += 128) pc.process(&x[i], &r[i], std::min(128, N - i), p);
        float f = acPitch(x, (int)(2 * sr), 9600, sr);
        bool ok = std::fabs(f - 174.61f) < 1.5f || std::fabs(f - 196.f) < 1.5f;
        check("vowel", ok, "185 Hz vowel in C major -> %.2f Hz (F3 174.61 / G3 196.00)", f);
    }
    { // vowel shifted 120 -> 180 Hz: check output pitch with autocorrelation
        PsolaShifter s; s.prepare(sr, 80.f); int N = (int)(2 * sr);
        auto x = vowel(120.f, sr, N); std::vector<float> y(N);
        for (int i = 0; i < N; ++i) { float l = x[i], r = x[i]; s.processSample(l, r, 120.f, 180.f, true, 0.f, 0.f); y[i] = l; }
        float f = acPitch(y, (int)sr, 9600, sr);
        check("psola", std::fabs(1200 * std::log2(f / 180.f)) < 5.f, "120 -> 180 Hz vowel: out %.2f Hz", f);
    }
    std::printf(fails ? "\n%d FAILED\n" : "\nAll pitch tests passed\n", fails);
    return fails;
}
