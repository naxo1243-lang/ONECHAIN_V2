#pragma once
#include "../Common.h"

namespace oc {

// ---------------------------------------------------------------------------
// TD-PSOLA pitch shifter (streaming, stereo, constant latency).
//  * Pitch of the output is set by the spacing of the synthesis grains, so it is
//    exact (no phase-slip error like delay-line shifters).
//  * Each grain keeps its original spectral envelope, so formants are preserved.
//  * An optional formant shift resamples the grains.
// ---------------------------------------------------------------------------
class PsolaShifter {
public:
    void prepare(float sampleRate, float minHz = 80.f) {
        sr = sampleRate;
        maxT = (int)(sr / minHz);
        Tdef = std::max(32, (int)(0.007f * sr));
        lat = (int)std::ceil(3.8f * (float)maxT);
        int sz = 1; while (sz < 4 * lat + 4096) sz <<= 1;
        mask = sz - 1;
        inL.assign(sz, 0.f); inR.assign(sz, 0.f); sig.assign(sz, 0.f);
        accL.assign(sz, 0.f); accR.assign(sz, 0.f); accW.assign(sz, 0.f);
        marks.clear(); marks.reserve(8192); marks.push_back(0); mp = 0;
        now = 0; synthPos = 0.0; lp1 = lp2 = 0.f; dryMix = 1.f;
        lpA = 1.f - std::exp(-2.f * kPi * 700.f / sr);
        dryCoef = 1.f - std::exp(-1.f / (0.005f * sr));
    }
    int latency() const { return lat; }
    float defaultPeriod() const { return (float)Tdef; }

    // fIn: current input pitch (Hz), fOut: wanted output pitch (Hz), voiced: pitched or not
    // dryTarget: 1 = pass delayed dry signal (crossfaded), 0 = PSOLA output
    void processSample(float& l, float& r, float fIn, float fOut, bool voiced,
                       float formantSemis, float dryTarget)
    {
        const int64_t nowI = now;
        const int64_t outIdx = nowI - lat;
        const int64_t M = (int64_t)mask;

        inL[(size_t)(nowI & M)] = l; inR[(size_t)(nowI & M)] = r;
        const float mono = 0.5f * (l + r);
        lp1 += lpA * (mono - lp1); lp2 += lpA * (lp1 - lp2);
        sig[(size_t)(nowI & M)] = lp2;

        float Tcur = voiced ? sr / std::min(std::max(fIn, 40.f), 2000.f) : (float)Tdef;
        Tcur = std::min(std::max(Tcur, 8.f), 2.f * (float)maxT);

        // ---- analysis: find pitch marks (peaks of the low-passed signal) ----
        for (;;) {
            const int64_t last = marks.back();
            if (!voiced) {
                const int64_t nm = last + Tdef;
                if (nm <= nowI) { marks.push_back(nm); continue; }
                break;
            }
            const int64_t lo = last + (int64_t)(0.8f * Tcur);
            const int64_t hi = last + (int64_t)(1.2f * Tcur);
            if (nowI < hi) break;
            int64_t best = lo; float bv = -1e30f;
            for (int64_t i = lo; i <= hi; ++i) {
                const float v = sig[(size_t)(i & M)];
                if (v > bv) { bv = v; best = i; }
            }
            marks.push_back(best);
        }
        if (marks.size() > 6000) {
            marks.erase(marks.begin(), marks.begin() + 3000);
            mp = (mp > 3000) ? mp - 3000 : 0;
        }

        // ---- synthesis: overlap-add grains at spacing 1/fOut ----
        const double Ts = (double)sr / (double)std::max(fOut, 20.f);
        const float rf = std::pow(2.f, formantSemis / 12.f);
        if (synthPos < (double)outIdx) synthPos = (double)outIdx;
        for (;;) {
            const double s = synthPos;
            if ((double)marks.back() < s) break;
            while (mp + 1 < marks.size() &&
                   std::fabs((double)marks[mp + 1] - s) <= std::fabs((double)marks[mp] - s)) ++mp;
            const int64_t m = marks[mp];
            const int hw = std::max(8, (int)std::ceil(std::max((double)Tcur, Ts)));
            if ((double)nowI < (double)m + (double)hw * rf + 2.0) break;

            const int64_t c = (int64_t)std::floor(s + 0.5);
            const double delta = s - (double)c;
            for (int j = -hw; j <= hw; ++j) {
                const int64_t n = c + j;
                if (n < outIdx) continue;
                const double x = (double)j - delta;
                if (std::fabs(x) >= (double)hw) continue;
                const float w = 0.5f * (1.f + std::cos(kPi * (float)(x / (double)hw)));
                const double sp = (double)m + x * (double)rf;
                const int64_t i0 = (int64_t)std::floor(sp);
                const float fr = (float)(sp - (double)i0);
                const size_t a = (size_t)(i0 & M), b = (size_t)((i0 + 1) & M);
                const size_t k = (size_t)(n & M);
                accL[k] += w * (inL[a] * (1.f - fr) + inL[b] * fr);
                accR[k] += w * (inR[a] * (1.f - fr) + inR[b] * fr);
                accW[k] += w;
            }
            synthPos += Ts;
        }

        // ---- output (delayed by 'lat' samples) ----
        float oL = 0.f, oR = 0.f, dL = 0.f, dR = 0.f;
        if (outIdx >= 0) {
            const size_t k = (size_t)(outIdx & M);
            const float inv = 1.f / std::max(accW[k], 0.25f);
            oL = accL[k] * inv; oR = accR[k] * inv;
            dL = inL[k]; dR = inR[k];
            accL[k] = accR[k] = accW[k] = 0.f;
        }
        dryMix += (dryTarget - dryMix) * dryCoef;
        l = dryMix * dL + (1.f - dryMix) * oL;
        r = dryMix * dR + (1.f - dryMix) * oR;
        ++now;
    }

private:
    float sr = 44100.f, lpA = 0.1f, lp1 = 0.f, lp2 = 0.f, dryMix = 1.f, dryCoef = 0.01f;
    int maxT = 551, Tdef = 308, lat = 2000, mask = 8191;
    int64_t now = 0;
    double synthPos = 0.0;
    size_t mp = 0;
    std::vector<int64_t> marks;
    std::vector<float> inL, inR, sig, accL, accR, accW;
};

struct PitchParams {
    bool  on = true;
    int   key = 0;          // 0 = C ... 11 = B
    int   scale = 0;        // 0 chromatic, 1 major, 2 minor
    float speedMs = 20.f;   // retune speed
    float amount = 1.f;     // 0..1
    float formantSemis = 0.f; // 0 = formants preserved; +/- shifts them
};

// Auto-tune style corrector: YIN detection -> scale quantizer -> PSOLA.
class PitchCorrector {
public:
    void prepare(float sampleRate) {
        sr = sampleRate;
        minTau = std::max(2, (int)(sr / 800.f));
        maxTau = (int)(sr / 80.f);
        A = 2 * maxTau + 64; Wy = A - maxTau;
        hist.assign(A, 0.f); scratch.assign(A, 0.f);
        cm.assign(maxTau + 2, 1.f); df.assign(maxTau + 2, 0.f);
        hw = 0; cnt = 0; hop = std::max(64, (int)(sr / 100.f));
        ratio = targetRatio = 1.f; fDet = 0.f; voiced = false; miss = 99; lastF0 = 0.f; curNote = -1000.f;
        shifter.prepare(sr, 80.f);
    }
    int latency() const { return shifter.latency(); }
    float getDetectedHz() const { return lastF0; }

    void process(float* l, float* r, int n, const PitchParams& p) {
        const float coef = 1.f - std::exp(-1.f / (std::max(0.5f, p.speedMs) * 0.001f * sr));
        const float dryTarget = (!p.on || p.amount < 0.001f) ? 1.f : 0.f;
        for (int i = 0; i < n; ++i) {
            hist[hw] = 0.5f * (l[i] + r[i]);
            hw = (hw + 1) % A;
            if (++cnt >= hop) { cnt = 0; analyse(p); }
            ratio += (targetRatio - ratio) * coef;
            const float fIn = voiced ? fDet : sr / shifter.defaultPeriod();
            shifter.processSample(l[i], r[i], fIn, fIn * ratio, voiced, p.formantSemis, dryTarget);
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
        if (f0 > 0.f && conf > 0.85f) { fDet = f0; voiced = true; miss = 0; }
        else if (++miss >= 3) voiced = false;
        lastF0 = voiced ? fDet : 0.f;

        if (p.on && voiced) {
            float m = 69.f + 12.f * std::log2(fDet / 440.f);
            float t = quantize(m, p.key, p.scale);
            // hysteresis: stay on the previous note unless the new one is clearly closer
            if (curNote > -900.f && t != curNote && std::fabs(m - curNote) < 1.5f &&
                std::fabs(m - curNote) <= std::fabs(m - t) + 0.25f)
                t = curNote;
            curNote = t;
            targetRatio = std::pow(2.f, (t - m) * p.amount / 12.f);
        } else {
            curNote = -1000.f;
            targetRatio = 1.f;
        }
    }

    // YIN (de Cheveigne & Kawahara, 2002), parabolic refinement on the difference function
    float detect(const float* x, float& conf) {
        float e = 0.f;
        for (int j = 0; j < Wy; ++j) e += x[j] * x[j];
        if (e / (float)Wy < 1e-6f) { conf = 0.f; return 0.f; }

        float run = 0.f; cm[0] = 1.f; df[0] = 0.f;
        for (int tau = 1; tau <= maxTau; ++tau) {
            float s = 0.f;
            for (int j = 0; j < Wy; ++j) { float d = x[j] - x[j + tau]; s += d * d; }
            df[tau] = s;
            run += s;
            cm[tau] = run > 0.f ? s * (float)tau / run : 1.f;
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

    float sr = 44100.f, ratio = 1.f, targetRatio = 1.f, fDet = 0.f, lastF0 = 0.f;
    bool voiced = false;
    float curNote = -1000.f;
    int miss = 99, minTau = 55, maxTau = 551, A = 1166, Wy = 615, hw = 0, cnt = 0, hop = 441;
    std::vector<float> hist, scratch, cm, df;
    PsolaShifter shifter;
};

} // namespace oc
