#pragma once
#include "Common.h"

namespace oc {

// Two-head crossfading delay-line pitch shifter (one per channel).
// (Original v1 pitch engine - kept as-is on purpose: it has the character.)
class PitchShifterCh {
public:
    void prepare(float sr) {
        W = std::max(64, int(0.025f * sr));          // 25 ms window
        size = 1; while (size < 2 * W + 8) size <<= 1;
        mask = size - 1;
        buf.assign(size, 0.f); wp = 0; p = 0.f;
    }
    int latency() const { return W / 2; }

    float process(float x, float ratio) {
        buf[wp] = x;
        float p2 = p + 0.5f; if (p2 >= 1.f) p2 -= 1.f;
        float s1 = std::sin(kPi * p), s2 = std::sin(kPi * p2);
        float y = readAt(p * W) * s1 * s1 + readAt(p2 * W) * s2 * s2;
        p += (1.f - ratio) / (float)W;
        while (p >= 1.f) p -= 1.f;
        while (p < 0.f)  p += 1.f;
        wp = (wp + 1) & mask;
        return y;
    }
private:
    float readAt(float d) const {
        float pos = (float)wp - d;
        float fl = std::floor(pos), fr = pos - fl;
        int i0 = ((int)fl) & mask, i1 = (i0 + 1) & mask;
        return buf[i0] * (1.f - fr) + buf[i1] * fr;
    }
    std::vector<float> buf;
    int W = 1024, size = 4096, mask = 4095, wp = 0;
    float p = 0.f;
};

struct PitchParams {
    bool  on = true;
    int   key = 0;        // 0 = C ... 11 = B
    int   scale = 0;      // 0 chromatic, 1 major, 2 minor
    float speedMs = 20.f; // retune speed
    float amount = 1.f;   // 0..1
};

// Auto-tune style corrector: YIN pitch detection -> scale quantizer -> shifter.
class PitchCorrector {
public:
    void prepare(float sampleRate) {
        sr = sampleRate;
        minTau = std::max(2, int(sr / 800.f));
        maxTau = int(sr / 70.f);
        A = 2 * maxTau + 64; Wy = A - maxTau;
        hist.assign(A, 0.f); scratch.assign(A, 0.f); cm.assign(maxTau + 2, 1.f); df.assign(maxTau + 2, 0.f);
        hw = 0; cnt = 0; hop = std::max(64, int(sr / 100.f));
        ratio = targetRatio = 1.f; lastF0 = 0.f;
        sh[0].prepare(sr); sh[1].prepare(sr);
    }
    int latency() const { return sh[0].latency(); }
    float getDetectedHz() const { return lastF0; }

    void process(float* l, float* r, int n, const PitchParams& p) {
        const float coef = 1.f - std::exp(-1.f / (std::max(0.5f, p.speedMs) * 0.001f * sr));
        for (int i = 0; i < n; ++i) {
            hist[hw] = 0.5f * (l[i] + r[i]);
            hw = (hw + 1) % A;
            if (++cnt >= hop) { cnt = 0; analyse(p); }
            ratio += (targetRatio - ratio) * coef;
            l[i] = sh[0].process(l[i], ratio);
            r[i] = sh[1].process(r[i], ratio);
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
        for (int k = 0; k < A; ++k) scratch[k] = hist[(hw + k) % A];
        float conf = 0.f;
        float f0 = detect(scratch.data(), conf);
        lastF0 = f0;
        if (p.on && f0 > 0.f && conf > 0.85f) {
            float m = 69.f + 12.f * std::log2(f0 / 440.f);
            float t = quantize(m, p.key, p.scale);
            targetRatio = std::pow(2.f, (t - m) * p.amount / 12.f);
        } else {
            targetRatio = 1.f;
        }
    }

    // YIN (de Cheveigne & Kawahara, 2002)
    float detect(const float* x, float& conf) {
        float e = 0.f;
        for (int j = 0; j < Wy; ++j) e += x[j] * x[j];
        if (e / Wy < 1e-6f) { conf = 0.f; return 0.f; }

        float run = 0.f; cm[0] = 1.f;
        for (int tau = 1; tau <= maxTau; ++tau) {
            float s = 0.f;
            for (int j = 0; j < Wy; ++j) { float d = x[j] - x[j + tau]; s += d * d; }
            df[tau] = s;
            run += s;
            cm[tau] = run > 0.f ? s * tau / run : 1.f;
        }
        int tau = -1;
        for (int t = minTau; t <= maxTau; ++t) {
            if (cm[t] < 0.15f) {
                while (t + 1 <= maxTau && cm[t + 1] < cm[t]) ++t;
                tau = t; break;
            }
        }
        if (tau < 0) { conf = 0.f; return 0.f; }
        float tf = (float)tau;
        if (tau > 1 && tau < maxTau) {
            float a = df[tau - 1], b = df[tau], c = df[tau + 1];
            float den = a - 2 * b + c;
            if (std::fabs(den) > 1e-9f) tf += 0.5f * (a - c) / den;
        }
        conf = 1.f - cm[tau];
        return sr / tf;
    }

    float sr = 44100.f, ratio = 1.f, targetRatio = 1.f, lastF0 = 0.f;
    int minTau = 55, maxTau = 630, A = 1324, Wy = 694, hw = 0, cnt = 0, hop = 441;
    std::vector<float> hist, scratch, cm, df;
    PitchShifterCh sh[2];
};

} // namespace oc
