#pragma once
#include "Common.h"

namespace oc {

// Optical-style leveling compressor (original implementation).
// Soft knee, program-dependent two-stage release (fast at first, slower the longer
// the signal has been compressed), adjustable stereo link and parallel mix.
class OptoCompressor {
public:
    void prepare(float sampleRate) { sr = sampleRate; reset(); }
    void reset() { gr[0] = gr[1] = 0.f; mem[0] = mem[1] = 0.f; lastMax = 0.f; }

    // peakReduction 0..100, makeupDb, limitMode = higher ratio,
    // link 0..1 (1 = fully linked), mix 0..1 (1 = fully compressed)
    void process(float* l, float* r, int n, float peakReduction, float makeupDb, bool limitMode,
                 float link = 1.f, float mix = 1.f, bool saturate = true)
    {
        const float thr   = -6.f - peakReduction * 0.40f;   // 0 -> -6 dB, 100 -> -46 dB
        const float ratio = limitMode ? 12.f : 3.f;
        const float slope = 1.f - 1.f / ratio;
        const float knee  = 10.f;
        const float makeup = dbToLin(makeupDb);
        const float aAtt  = msToCoef(10.f, sr);
        const float aMem  = msToCoef(2500.f, sr);
        float maxGr = 0.f;

        for (int i = 0; i < n; ++i) {
            const float dryL = l[i], dryR = r[i];
            const float dl = linToDb(std::fabs(dryL)), dr = linToDb(std::fabs(dryR));
            const float dm = std::max(dl, dr);
            const float det[2] = { dl + link * (dm - dl), dr + link * (dm - dr) };

            float g[2];
            for (int c = 0; c < 2; ++c) {
                const float over = det[c] - thr;
                float target;
                if (over <= -knee / 2)      target = 0.f;
                else if (over >= knee / 2)  target = over * slope;
                else { const float x = over + knee / 2; target = slope * x * x / (2 * knee); }

                if (target > gr[c]) {
                    gr[c] = aAtt * gr[c] + (1.f - aAtt) * target;
                } else {
                    const float relMs = 60.f + mem[c] * 1400.f;
                    const float aRel = msToCoef(relMs, sr);
                    gr[c] = aRel * gr[c] + (1.f - aRel) * target;
                }
                mem[c] = aMem * mem[c] + (1.f - aMem) * (gr[c] > 3.f ? 1.f : 0.f);
                g[c] = dbToLin(-gr[c]) * makeup;
                maxGr = std::max(maxGr, gr[c]);
            }
            float wl = dryL * g[0], wr = dryR * g[1];
            if (saturate) {
                const float d = 1.2f;
                wl = std::tanh(wl * d) / d; wr = std::tanh(wr * d) / d;
            }
            l[i] = dryL * (1.f - mix) + wl * mix;
            r[i] = dryR * (1.f - mix) + wr * mix;
        }
        lastMax = maxGr;
    }

    float getGainReductionDb() const { return lastMax; }

private:
    float sr = 44100.f, gr[2] = {0, 0}, mem[2] = {0, 0}, lastMax = 0.f;
};

} // namespace oc
