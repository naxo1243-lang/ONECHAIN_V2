#pragma once
#include "Common.h"

namespace oc {

// TD-PSOLA pitch shifter (streaming, stereo). The pitch of the output is set by the spacing of the
// synthesis grains, so it is exact; every grain keeps its own spectral envelope, so formants stay put.
class PsolaShifter {
public:
    // buffers are sized for absMinHz; setMinHz() can then raise the lowest pitch at run time (= less latency)
    void prepare(float sampleRate, float absMinHz = 70.f) {
        sr = sampleRate;
        absMaxT = (int)(sr / absMinHz);
        Tdef = std::max(32, (int)(0.007f * sr));
        int sz = 1; while (sz < 4 * (int)std::ceil(kLatFactor * (float)absMaxT) + 4096) sz <<= 1;
        mask = sz - 1;
        lpA = 1.f - std::exp(-2.f * kPi * 700.f / sr);
        dryCoef = 1.f - std::exp(-1.f / (0.005f * sr));
        inL.assign(sz, 0.f); inR.assign(sz, 0.f); sig.assign(sz, 0.f);
        accL.assign(sz, 0.f); accR.assign(sz, 0.f); accW.assign(sz, 0.f);
        marks.reserve(8192);
        setMinHz(absMinHz);
    }
    void setMinHz(float hz) {
        hz = std::max(hz, sr / (float)absMaxT);
        maxT = (int)(sr / hz);
        lat = (int)std::ceil(kLatFactor * (float)maxT);
        reset();
    }
    void reset() {
        std::fill(inL.begin(), inL.end(), 0.f); std::fill(inR.begin(), inR.end(), 0.f); std::fill(sig.begin(), sig.end(), 0.f);
        std::fill(accL.begin(), accL.end(), 0.f); std::fill(accR.begin(), accR.end(), 0.f); std::fill(accW.begin(), accW.end(), 0.f);
        marks.clear(); marks.push_back(0); mp = 0;
        now = 0; synthPos = 0.0; lp1 = lp2 = 0.f; dryMix = 1.f; needSync = false;
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

        // fast path: plain delayed dry signal (pitch off) - no analysis, no grains
        if (dryTarget >= 0.999f && dryMix >= 0.999f) {
            // keep the analysis signal current so the pitch marks can be found right after returning
            const float mm = 0.5f * (l + r);
            lp1 += lpA * (mm - lp1); lp2 += lpA * (lp1 - lp2);
            sig[(size_t)(nowI & M)] = lp2;
            if (outIdx >= 0) { l = inL[(size_t)(outIdx & M)]; r = inR[(size_t)(outIdx & M)]; } else { l = 0.f; r = 0.f; }
            needSync = true; ++now;
            return;
        }
        if (needSync) {                       // coming back from bypass: restart the pitch marks here
            std::fill(accL.begin(), accL.end(), 0.f); std::fill(accR.begin(), accR.end(), 0.f); std::fill(accW.begin(), accW.end(), 0.f);
            const int64_t start = std::max<int64_t>(0, outIdx);          // resume where the output is being read
            marks.clear(); marks.push_back(start); mp = 0; synthPos = (double)start; needSync = false;
        }
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
            const int64_t lo = last + (int64_t)(0.85f * Tcur);
            const int64_t hi = last + (int64_t)(1.15f * Tcur);
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
    static constexpr float kLatFactor = 2.3f;   // measured: complete grains from ~2.2 x the longest period
    bool needSync = false;
    int absMaxT = 686, maxT = 551, Tdef = 308, lat = 2000, mask = 8191;
    int64_t now = 0;
    double synthPos = 0.0;
    size_t mp = 0;
    std::vector<int64_t> marks;
    std::vector<float> inL, inR, sig, accL, accR, accW;
};


} // namespace oc
