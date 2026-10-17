#pragma once
#include "Common.h"

namespace oc {

// Two-head crossfading delay-line pitch shifter, both channels at once.
// (Original v1 pitch engine - same algorithm and character. The two read heads share their
//  position and gains, so the sine and the interpolation set-up are computed once per sample.)
// The window length is adjustable: latency = window / 2.
class PitchShifterStereo {
public:
    void prepare(float sampleRate, float maxWindowMs = 25.f) {
        sr = sampleRate;
        Wmax = std::max(64, int(maxWindowMs * 0.001f * sr));
        size = 1; while (size < 2 * Wmax + 8) size <<= 1;
        mask = size - 1;
        bufL.assign((size_t)size, 0.f); bufR.assign((size_t)size, 0.f);
        W = 0; setWindowMs(maxWindowMs);
    }
    void setWindowMs(float ms) {
        const int w = std::min(Wmax, std::max(64, int(ms * 0.001f * sr)));
        if (w == W) return;
        W = w; std::fill(bufL.begin(), bufL.end(), 0.f); std::fill(bufR.begin(), bufR.end(), 0.f);
        wp = 0; p = 0.f;
    }
    int latency() const { return W / 2; }
    void reset() { std::fill(bufL.begin(), bufL.end(), 0.f); std::fill(bufR.begin(), bufR.end(), 0.f); wp = 0; p = 0.f; }

    void process(float xl, float xr, float ratio, float& yl, float& yr) {
        bufL[(size_t)wp] = xl; bufR[(size_t)wp] = xr;
        const float s = std::sin(kPi * p);
        const float g1 = s * s, g2 = 1.f - g1;          // sin^2(pi p) and sin^2(pi (p+0.5)) = cos^2(pi p)
        float p2 = p + 0.5f; if (p2 >= 1.f) p2 -= 1.f;
        float a1l, a1r, a2l, a2r;
        read(p * (float)W, a1l, a1r);
        read(p2 * (float)W, a2l, a2r);
        yl = a1l * g1 + a2l * g2;
        yr = a1r * g1 + a2r * g2;
        p += (1.f - ratio) / (float)W;
        while (p >= 1.f) p -= 1.f;
        while (p < 0.f)  p += 1.f;
        wp = (wp + 1) & mask;
    }
private:
    void read(float d, float& l, float& r) const {
        const float pos = (float)wp - d;
        const float fl = std::floor(pos), fr = pos - fl;
        const int i0 = ((int)fl) & mask, i1 = (i0 + 1) & mask;
        l = bufL[(size_t)i0] * (1.f - fr) + bufL[(size_t)i1] * fr;
        r = bufR[(size_t)i0] * (1.f - fr) + bufR[(size_t)i1] * fr;
    }
    std::vector<float> bufL, bufR;
    float sr = 44100.f, p = 0.f;
    int W = 1024, Wmax = 1024, size = 4096, mask = 4095, wp = 0;
};

struct PitchParams {
    bool  on = true;
    int   key = 0;        // 0 = C ... 11 = B
    int   scale = 0;      // 0 chromatic, 1 major, 2 minor
    float speedMs = 20.f; // retune speed
    float amount = 1.f;   // 0..1
};

// Auto-tune style corrector: YIN pitch detection -> scale quantizer -> shifter.
// Detection runs on a decimated (~12 kHz) copy of the signal and the result is refined at the
// full sample rate (about 12x cheaper than a full-rate YIN, same precision).
class PitchCorrector {
public:
    void prepare(float sampleRate) {
        sr = sampleRate;
        minTau = std::max(2, int(sr / 800.f));
        maxTau = int(sr / 70.f);
        A = 2 * maxTau + 64; Wy = A - maxTau;
        hist.assign((size_t)A, 0.f); scratch.assign((size_t)A, 0.f);

        dec = std::max(1, (int)std::lround(sr / 12000.f));
        srD = sr / (float)dec;
        minTauD = std::max(2, int(srD / 800.f));
        maxTauD = int(srD / 70.f);
        AD = 2 * maxTauD + 32; WyD = AD - maxTauD;
        histD.assign((size_t)AD, 0.f); scratchD.assign((size_t)AD, 0.f);
        cmD.assign((size_t)maxTauD + 2, 1.f); dfD.assign((size_t)maxTauD + 2, 0.f);
        const float fc = std::min(3000.f, 0.3f * srD);
        lp1.lowpass(sr, fc); lp2.lowpass(sr, fc); lp1.reset(); lp2.reset();

        hw = 0; hwD = 0; cnt = 0; decCnt = 0; hop = std::max(64, int(sr / 100.f));
        ratio = targetRatio = 1.f; lastF0 = 0.f;
        shifter.prepare(sr, 25.f);
    }
    void setWindowMs(float ms) { shifter.setWindowMs(ms); }
    void reset() {
        std::fill(hist.begin(), hist.end(), 0.f); std::fill(histD.begin(), histD.end(), 0.f);
        lp1.reset(); lp2.reset(); hw = 0; hwD = 0; cnt = 0; decCnt = 0;
        ratio = targetRatio = 1.f; lastF0 = 0.f; shifter.reset();
    }
    int latency() const { return shifter.latency(); }
    float getDetectedHz() const { return lastF0; }

    void process(float* l, float* r, int n, const PitchParams& p) {
        const float coef = 1.f - std::exp(-1.f / (std::max(0.5f, p.speedMs) * 0.001f * sr));
        const bool on = p.on;
        if (!on) { targetRatio = 1.f; lastF0 = 0.f; }
        for (int i = 0; i < n; ++i) {
            if (on) {
                const float m = 0.5f * (l[i] + r[i]);
                hist[(size_t)hw] = m; if (++hw == A) hw = 0;
                const float y = lp2.process(lp1.process(m));
                if (++decCnt >= dec) { decCnt = 0; histD[(size_t)hwD] = y; if (++hwD == AD) hwD = 0; }
                if (++cnt >= hop) { cnt = 0; analyse(p); }
            }
            ratio += (targetRatio - ratio) * coef;
            float yl, yr;
            shifter.process(l[i], r[i], ratio, yl, yr);
            l[i] = yl; r[i] = yr;
        }
    }

    static float quantize(float m, int key, int scale) {
        if (scale == 0) return std::round(m);
        static const bool maj[12] = {1,0,1,0,1,1,0,1,0,1,0,1};
        static const bool mnr[12] = {1,0,1,1,0,1,0,1,1,0,1,0};
        const bool* mask = scale == 1 ? maj : mnr;
        int base = (int)std::floor(m);
        float best = std::round(m), bestD = 1e9f;
        for (int nn = base - 1; nn <= base + 2; ++nn) {
            int pc = (((nn - key) % 12) + 12) % 12;
            if (!mask[pc]) continue;
            float d = std::fabs((float)nn - m);
            if (d < bestD) { bestD = d; best = (float)nn; }
        }
        return best;
    }

private:
    void analyse(const PitchParams& p) {
        for (int k = 0; k < AD; ++k) scratchD[(size_t)k] = histD[(size_t)((hwD + k) % AD)];
        float conf = 0.f;
        const float f0 = detect(conf);
        lastF0 = f0;
        if (f0 > 0.f && conf > 0.85f) {
            float m = 69.f + 12.f * std::log2(f0 / 440.f);
            float t = quantize(m, p.key, p.scale);
            targetRatio = std::pow(2.f, (t - m) * p.amount / 12.f);
        } else {
            targetRatio = 1.f;
        }
    }

    // YIN (de Cheveigne & Kawahara, 2002): coarse search on the decimated signal,
    // then a precise lag search around the result at the full sample rate.
    float detect(float& conf) {
        const float* xd = scratchD.data();
        float e = 0.f;
        for (int j = 0; j < WyD; ++j) e += xd[j] * xd[j];
        if (e / (float)WyD < 1e-6f) { conf = 0.f; return 0.f; }

        float run = 0.f; cmD[0] = 1.f; dfD[0] = 0.f;
        for (int tau = 1; tau <= maxTauD; ++tau) {
            float s = 0.f;
            for (int j = 0; j < WyD; ++j) { const float d = xd[j] - xd[j + tau]; s += d * d; }
            dfD[(size_t)tau] = s;
            run += s;
            cmD[(size_t)tau] = run > 0.f ? s * (float)tau / run : 1.f;
        }
        int tau = -1;
        for (int t = minTauD; t <= maxTauD; ++t) {
            if (cmD[(size_t)t] < 0.15f) {
                while (t + 1 <= maxTauD && cmD[(size_t)t + 1] < cmD[(size_t)t]) ++t;
                tau = t; break;
            }
        }
        if (tau < 0) { conf = 0.f; return 0.f; }
        float tfD = (float)tau;
        if (tau > 1 && tau < maxTauD) {
            const float a = dfD[(size_t)tau - 1], b = dfD[(size_t)tau], c = dfD[(size_t)tau + 1];
            const float den = a - 2 * b + c;
            if (std::fabs(den) > 1e-9f) tfD += 0.5f * (a - c) / den;
        }
        conf = 1.f - cmD[(size_t)tau];

        // ---- refine at the full rate: +-dec lags around the coarse estimate ----
        if (dec == 1) return srD / tfD;
        for (int k = 0; k < A; ++k) scratch[(size_t)k] = hist[(size_t)((hw + k) % A)];
        const float* x = scratch.data();
        const int c0 = (int)std::lround(tfD * (float)dec);
        const int lo = std::max(minTau, c0 - dec), hi = std::min(maxTau, c0 + dec);
        if (hi - lo < 2) return sr / (tfD * (float)dec);
        float best = 1e30f; int bl = lo; float dv[64];
        const int cntLags = std::min(hi - lo + 1, 64);
        for (int k = 0; k < cntLags; ++k) {
            const int lag = lo + k; float s = 0.f;
            for (int j = 0; j < Wy; ++j) { const float d = x[j] - x[j + lag]; s += d * d; }
            dv[k] = s;
            if (s < best) { best = s; bl = k; }
        }
        float tf = (float)(lo + bl);
        if (bl > 0 && bl < cntLags - 1) {
            const float a = dv[bl - 1], b = dv[bl], c = dv[bl + 1];
            const float den = a - 2 * b + c;
            if (std::fabs(den) > 1e-9f) tf += 0.5f * (a - c) / den;
        }
        return sr / tf;
    }

    float sr = 44100.f, srD = 11025.f, ratio = 1.f, targetRatio = 1.f, lastF0 = 0.f;
    int minTau = 55, maxTau = 630, A = 1324, Wy = 694, hw = 0, cnt = 0, hop = 441;
    int dec = 4, minTauD = 13, maxTauD = 157, AD = 346, WyD = 189, hwD = 0, decCnt = 0;
    std::vector<float> hist, scratch, histD, scratchD, cmD, dfD;
    Biquad lp1, lp2;
    PitchShifterStereo shifter;
};

} // namespace oc
