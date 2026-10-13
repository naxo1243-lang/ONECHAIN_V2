#pragma once
#include "Common.h"

namespace oc {

// Clean feed-forward stereo-linked compressor (dB domain, soft knee).
class Compressor {
public:
    struct Params { float thrDb = -12.f, ratio = 2.f, kneeDb = 6.f, attackMs = 20.f, releaseMs = 150.f; };

    void prepare(float sampleRate) { sr = sampleRate; gr = 0.f; }
    void reset() { gr = 0.f; }

    // processes in place, returns the maximum gain reduction (dB, positive) in this block
    float process(float* l, float* r, int n, const Params& p) {
        const float slope = 1.f - 1.f / std::max(1.f, p.ratio);
        const float knee = std::max(0.1f, p.kneeDb);
        const float aA = msToCoef(p.attackMs, sr), aR = msToCoef(p.releaseMs, sr);
        float maxGr = 0.f;
        for (int i = 0; i < n; ++i) {
            const float lvl = std::max(std::fabs(l[i]), std::fabs(r[i]));
            const float over = linToDb(lvl) - p.thrDb;
            float target;
            if (over <= -knee * 0.5f)      target = 0.f;
            else if (over >= knee * 0.5f)  target = over * slope;
            else { const float x = over + knee * 0.5f; target = slope * x * x / (2.f * knee); }
            if (target > gr) gr = aA * gr + (1.f - aA) * target;
            else             gr = aR * gr + (1.f - aR) * target;
            const float g = dbToLin(-gr);
            l[i] *= g; r[i] *= g;
            maxGr = std::max(maxGr, gr);
        }
        return maxGr;
    }
private:
    float sr = 44100.f, gr = 0.f;
};

} // namespace oc
