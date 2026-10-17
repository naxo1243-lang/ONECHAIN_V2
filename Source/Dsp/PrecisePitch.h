#pragma once
#include "Common.h"
#include "YinDetector.h"
#include "PsolaShifter.h"
#include "PitchCorrector.h"     // PitchParams, quantize()

namespace oc {

// "Precise" tuner: YIN detection -> scale quantizer -> TD-PSOLA.
// The output pitch is set by the grain spacing, so it lands exactly on the note (sub-cent) and the
// formants are preserved. Latency is about 2.3 periods of the lowest pitch (range dependent).
class PrecisePitch {
public:
    void prepare(float sampleRate)
    {
        sr = sampleRate;
        det.prepare(sr, 70.f); shifter.prepare(sr, 70.f);
        hop = std::max(64, (int)(sr / 100.f));
        minHz = -1.f; setMinHz(100.f);
    }
    void setMinHz(float hz)
    {
        if (hz == minHz) return;
        minHz = hz; det.setMinHz(hz); shifter.setMinHz(hz); resetState();
    }
    void reset() { det.reset(); shifter.reset(); resetState(); }
    int latency() const { return shifter.latency(); }
    float getDetectedHz() const { return lastF0; }

    void process(float* l, float* r, int n, const PitchParams& p)
    {
        const float coef = 1.f - std::exp(-1.f / (std::max(0.5f, p.speedMs) * 0.001f * sr));
        const bool on = p.on && p.amount >= 0.001f;
        const float dryTarget = on ? 0.f : 1.f;
        if (!on) { voiced = false; targetRatio = 1.f; lastF0 = 0.f; curNote = -1000.f; }
        for (int i = 0; i < n; ++i) {
            if (on) {
                det.push(0.5f * (l[i] + r[i]));
                if (++cnt >= hop) { cnt = 0; analyse(p); }
            }
            ratio += (targetRatio - ratio) * coef;
            const float fIn = voiced ? fDet : sr / shifter.defaultPeriod();
            shifter.processSample(l[i], r[i], fIn, fIn * ratio, voiced, 0.f, dryTarget);
        }
    }

private:
    void resetState() { ratio = targetRatio = 1.f; fDet = 0.f; voiced = false; miss = 99; curNote = -1000.f; cnt = 0; lastF0 = 0.f; }

    void analyse(const PitchParams& p)
    {
        float conf = 0.f;
        const float f0 = det.detect(conf);
        if (f0 > 0.f && conf > 0.85f) { fDet = f0; voiced = true; miss = 0; }
        else if (++miss >= 3) voiced = false;
        lastF0 = voiced ? fDet : 0.f;
        if (voiced) {
            const float m = 69.f + 12.f * std::log2(fDet / 440.f);
            float t = PitchCorrector::quantize(m, p.key, p.scale);
            // hysteresis: stay on the previous note unless the new one is clearly closer
            if (curNote > -900.f && t != curNote && std::fabs(m - curNote) < 1.5f && std::fabs(m - curNote) <= std::fabs(m - t) + 0.25f)
                t = curNote;
            curNote = t;
            targetRatio = std::pow(2.f, (t - m) * p.amount / 12.f);
        } else { curNote = -1000.f; targetRatio = 1.f; }
    }

    float sr = 48000.f, minHz = -1.f, ratio = 1.f, targetRatio = 1.f, fDet = 0.f, lastF0 = 0.f, curNote = -1000.f;
    bool voiced = false;
    int miss = 99, cnt = 0, hop = 480;
    YinDetector det;
    PsolaShifter shifter;
};

} // namespace oc
