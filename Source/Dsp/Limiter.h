#pragma once
#include "Common.h"

namespace oc {

// True-peak look-ahead limiter: inter-sample peaks are detected with a 4x windowed-sinc
// interpolator (only when the signal is near the ceiling), the gain is a sliding minimum
// (O(1) monotonic deque) smoothed by a boxcar, followed by a release stage.
// The look-ahead (= latency) can be changed at run time (Recording mode uses a shorter one).
class TruePeakLimiter {
public:
    void prepare(float sampleRate, float maxLookaheadMs = 1.5f) {
        sr = sampleRate;
        maxN = std::max(16, (int)(maxLookaheadMs * 0.001f * sr));
        dl.assign((size_t)maxN + 8, 0.f); dr.assign((size_t)maxN + 8, 0.f);
        mins.assign((size_t)maxN, 1.f);
        cap = maxN + 2; qv.assign((size_t)cap, 1.f); qi.assign((size_t)cap, 0);
        relCoef = msToCoef(100.f, sr);
        for (int p = 0; p < 3; ++p) {
            const float f = 0.25f * (float)(p + 1);
            float s = 0.f;
            for (int j = 0; j < 12; ++j) {
                const float x = 5.f + f - (float)j;
                const float sinc = std::fabs(x) < 1e-6f ? 1.f : std::sin(kPi * x) / (kPi * x);
                const float w = 0.5f * (1.f + std::cos(kPi * x / 6.f));
                coef[p][j] = sinc * w; s += coef[p][j];
            }
            for (int j = 0; j < 12; ++j) coef[p][j] /= s;
        }
        N = 0;
        setLookaheadMs(maxLookaheadMs);
    }
    void setLookaheadMs(float ms) {
        const int n = std::min(maxN, std::max(8, (int)(ms * 0.001f * sr)));
        if (n == N) return;
        N = n; D = N + 8; reset();
    }
    void reset() {
        std::fill(dl.begin(), dl.end(), 0.f); std::fill(dr.begin(), dr.end(), 0.f);
        std::fill(mins.begin(), mins.end(), 1.f);
        idx = 0; didx = 0; sum = (float)N; gSm = 1.f; qh = 0; qn = 0; t = 0; resum = 0; hw = 0;
        for (int i = 0; i < 24; ++i) hl[i] = hr[i] = 0.f;
    }
    int latency() const { return D; }

    // returns max gain reduction (dB, positive) in this block
    float process(float* l, float* r, int n, float ceilingDb) {
        const float ceil = dbToLin(ceilingDb - 0.1f);       // small safety margin for interpolation error
        const float safe = 0.35f * ceil;                      // below this no inter-sample peak can reach the ceiling
        float minG = 1.f;
        for (int i = 0; i < n; ++i) {
            hl[hw] = hl[hw + 12] = l[i]; hr[hw] = hr[hw + 12] = r[i];
            const float* L = &hl[hw + 1]; const float* R = &hr[hw + 1];
            hw = (hw + 1 == 12) ? 0 : hw + 1;

            float mx = 0.f;
            for (int j = 0; j < 12; ++j) mx = std::max(mx, std::max(std::fabs(L[j]), std::fabs(R[j])));
            float rq = 1.f;
            if (mx >= safe) {
                float pk = std::max(std::fabs(L[5]), std::fabs(R[5]));
                for (int p = 0; p < 3; ++p) {
                    float yl = 0.f, yr = 0.f;
                    for (int j = 0; j < 12; ++j) { yl += coef[p][j] * L[j]; yr += coef[p][j] * R[j]; }
                    pk = std::max(pk, std::max(std::fabs(yl), std::fabs(yr)));
                }
                if (pk > ceil) rq = ceil / pk;
            }

            // sliding minimum over the last N requirements (monotonic deque)
            while (qn > 0 && qv[(size_t)wrap(qh + qn - 1)] >= rq) --qn;
            { const int pos = wrap(qh + qn); qv[(size_t)pos] = rq; qi[(size_t)pos] = t; ++qn; }
            while (qi[(size_t)qh] <= t - N) { qh = wrap(qh + 1); --qn; }
            const float m = qv[(size_t)qh];
            ++t;

            // boxcar average of the minima
            sum += m - mins[(size_t)idx]; mins[(size_t)idx] = m;
            if (++resum >= 8192) { resum = 0; float s2 = 0.f; for (int k = 0; k < N; ++k) s2 += mins[(size_t)k]; sum = s2; }
            const float avg = sum / (float)N;

            gSm = avg < gSm ? avg : avg + (gSm - avg) * relCoef;
            const float g = std::min(gSm, avg);
            minG = std::min(minG, g);

            const float oL = dl[(size_t)didx], oR = dr[(size_t)didx];
            dl[(size_t)didx] = l[i]; dr[(size_t)didx] = r[i];
            l[i] = oL * g; r[i] = oR * g;
            idx = (idx + 1 == N) ? 0 : idx + 1;
            didx = (didx + 1 == D) ? 0 : didx + 1;
        }
        return -linToDb(minG);
    }
private:
    int wrap(int i) const { return i >= cap ? i - cap : i; }

    float sr = 44100.f, sum = 0.f, gSm = 1.f, relCoef = 0.99f;
    int maxN = 72, N = 72, D = 80, idx = 0, didx = 0, cap = 74, qh = 0, qn = 0, resum = 0, hw = 0;
    int64_t t = 0;
    std::vector<float> dl, dr, mins, qv;
    std::vector<int64_t> qi;
    float hl[24] = {}, hr[24] = {}, coef[3][12] = {};
};

} // namespace oc
