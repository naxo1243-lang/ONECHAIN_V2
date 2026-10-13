#pragma once
#include "Common.h"

namespace oc {

// True-peak look-ahead limiter: inter-sample peaks are detected with a 4x
// windowed-sinc interpolator, gain is the sliding minimum smoothed by a boxcar,
// followed by a release stage.
class TruePeakLimiter {
public:
    void prepare(float sampleRate, float lookaheadMs = 1.5f) {
        sr = sampleRate;
        N = std::max(16, (int)(lookaheadMs * 0.001f * sr));
        D = N + 8;                              // audio delay (covers detector latency)
        dl.assign(D, 0.f); dr.assign(D, 0.f);
        req.assign(N, 1.f); mins.assign(N, 1.f);
        idx = 0; didx = 0; sum = (float)N; gSm = 1.f;
        relCoef = msToCoef(100.f, sr);
        for (int i = 0; i < 12; ++i) hl[i] = hr[i] = 0.f;
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
    }
    int latency() const { return D; }

    // returns max gain reduction (dB, positive) in this block
    float process(float* l, float* r, int n, float ceilingDb) {
        const float ceil = dbToLin(ceilingDb - 0.1f);   // small safety margin for interpolation error
        float maxGr = 0.f;
        for (int i = 0; i < n; ++i) {
            for (int k = 0; k < 11; ++k) { hl[k] = hl[k + 1]; hr[k] = hr[k + 1]; }
            hl[11] = l[i]; hr[11] = r[i];

            float pk = std::max(std::fabs(hl[5]), std::fabs(hr[5]));
            for (int p = 0; p < 3; ++p) {
                float yl = 0.f, yr = 0.f;
                for (int j = 0; j < 12; ++j) { yl += coef[p][j] * hl[j]; yr += coef[p][j] * hr[j]; }
                pk = std::max(pk, std::max(std::fabs(yl), std::fabs(yr)));
            }
            req[idx] = pk > ceil ? ceil / pk : 1.f;

            float m = 1.f;
            for (int k = 0; k < N; ++k) m = std::min(m, req[k]);
            sum += m - mins[idx]; mins[idx] = m;
            const float avg = sum / (float)N;

            gSm = avg < gSm ? avg : avg + (gSm - avg) * relCoef;
            const float g = std::min(gSm, avg);
            maxGr = std::max(maxGr, -linToDb(g));

            const float oL = dl[didx], oR = dr[didx];
            dl[didx] = l[i]; dr[didx] = r[i];
            l[i] = oL * g; r[i] = oR * g;
            idx = (idx + 1) % N; didx = (didx + 1) % D;
        }
        return maxGr;
    }
private:
    float sr = 44100.f, sum = 0.f, gSm = 1.f, relCoef = 0.99f;
    int N = 72, D = 80, idx = 0, didx = 0;
    std::vector<float> dl, dr, req, mins;
    float hl[12] = {}, hr[12] = {}, coef[3][12] = {};
};

} // namespace oc
