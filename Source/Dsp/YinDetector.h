#pragma once
#include "Common.h"

namespace oc {

// YIN pitch detector (de Cheveigne & Kawahara, 2002) used by the Precise and Waves engines.
// Coarse search on a decimated (~12 kHz) copy of the signal, then a precise lag search around the
// result at the full sample rate. The lowest detectable pitch can be changed at run time
// (buffers are allocated for the absolute minimum).
class YinDetector {
public:
    void prepare(float sampleRate, float absMinHz = 70.f)
    {
        sr = sampleRate; absMin = absMinHz;
        const int maxTauAbs = (int)(sr / absMin);
        Aabs = 2 * maxTauAbs + 64;
        hist.assign((size_t)Aabs, 0.f); scratch.assign((size_t)Aabs, 0.f);
        dec = std::max(1, (int)std::lround(sr / 12000.f));
        srD = sr / (float)dec;
        const int maxTauDAbs = (int)(srD / absMin);
        ADabs = 2 * maxTauDAbs + 32;
        histD.assign((size_t)ADabs, 0.f); scratchD.assign((size_t)ADabs, 0.f);
        cmD.assign((size_t)maxTauDAbs + 2, 1.f); dfD.assign((size_t)maxTauDAbs + 2, 0.f);
        const float fc = std::min(3000.f, 0.3f * srD);
        lp1.lowpass(sr, fc); lp2.lowpass(sr, fc);
        setMinHz(absMin);
        reset();
    }
    void setMinHz(float hz)
    {
        hz = std::max(hz, absMin);
        maxTau = (int)(sr / hz); minTau = std::max(2, (int)(sr / 800.f));
        A = 2 * maxTau + 64; Wy = A - maxTau;
        maxTauD = (int)(srD / hz); minTauD = std::max(2, (int)(srD / 800.f));
        AD = 2 * maxTauD + 32; WyD = AD - maxTauD;
    }
    void reset()
    {
        std::fill(hist.begin(), hist.end(), 0.f); std::fill(histD.begin(), histD.end(), 0.f);
        lp1.reset(); lp2.reset(); hw = 0; hwD = 0; decCnt = 0;
    }

    void push(float m)
    {
        hist[(size_t)hw] = m; if (++hw == Aabs) hw = 0;
        const float y = lp2.process(lp1.process(m));
        if (++decCnt >= dec) { decCnt = 0; histD[(size_t)hwD] = y; if (++hwD == ADabs) hwD = 0; }
    }

    // returns f0 in Hz (0 = not periodic); conf in 0..1
    float detect(float& conf)
    {
        for (int k = 0; k < AD; ++k) scratchD[(size_t)k] = histD[(size_t)((hwD + ADabs - AD + k) % ADabs)];
        const float* xd = scratchD.data();
        float e = 0.f;
        for (int j = 0; j < WyD; ++j) e += xd[j] * xd[j];
        if (e / (float)WyD < 1e-6f) { conf = 0.f; return 0.f; }

        float run = 0.f; cmD[0] = 1.f; dfD[0] = 0.f;
        for (int tau = 1; tau <= maxTauD; ++tau) {
            float s = 0.f;
            for (int j = 0; j < WyD; ++j) { const float d = xd[j] - xd[j + tau]; s += d * d; }
            dfD[(size_t)tau] = s; run += s;
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
        if (dec == 1) return srD / tfD;

        for (int k = 0; k < A; ++k) scratch[(size_t)k] = hist[(size_t)((hw + Aabs - A + k) % Aabs)];
        const float* x = scratch.data();
        const int c0 = (int)std::lround(tfD * (float)dec);
        const int lo = std::max(minTau, c0 - dec), hi = std::min(maxTau, c0 + dec);
        if (hi - lo < 2) return sr / (tfD * (float)dec);
        float best = 1e30f; int bl = 0; float dv[64];
        const int cnt = std::min(hi - lo + 1, 64);
        for (int k = 0; k < cnt; ++k) {
            const int lag = lo + k; float s = 0.f;
            for (int j = 0; j < Wy; ++j) { const float d = x[j] - x[j + lag]; s += d * d; }
            dv[k] = s;
            if (s < best) { best = s; bl = k; }
        }
        float tf = (float)(lo + bl);
        if (bl > 0 && bl < cnt - 1) {
            const float a = dv[bl - 1], b = dv[bl], c = dv[bl + 1];
            const float den = a - 2 * b + c;
            if (std::fabs(den) > 1e-9f) tf += 0.5f * (a - c) / den;
        }
        return sr / tf;
    }

private:
    float sr = 44100.f, srD = 11025.f, absMin = 70.f;
    int dec = 4, Aabs = 1324, ADabs = 346, hw = 0, hwD = 0, decCnt = 0;
    int minTau = 55, maxTau = 630, A = 1324, Wy = 694, minTauD = 13, maxTauD = 157, AD = 346, WyD = 189;
    std::vector<float> hist, scratch, histD, scratchD, cmD, dfD;
    Biquad lp1, lp2;
};

} // namespace oc
