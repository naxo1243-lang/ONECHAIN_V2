#pragma once
#include "Common.h"
#include "YinDetector.h"
#include "PitchCorrector.h"     // PitchParams, PitchShifterStereo, quantize()

namespace oc {

// Waves Tune Real-Time style tuner (inspired by its documented behaviour):
//  * very low latency: short time-domain shifter whose window follows the singer's Range
//  * Speed = how fast the correction acts inside a note, Note Transition = how fast a NEW note is reached
//  * the note centre is tracked separately from the vibrato, so the vibrato can be kept (Vibrato 100 %)
//    or ironed out (0 %) while the note itself is corrected
//  * Tolerance = deviation (cents) that is left alone
struct WavesParams {
    float transMs = 80.f;     // note transition time
    float tolCents = 10.f;    // tolerance
    float vibrato = 1.f;      // 0..1 : amount of natural vibrato that is kept
};

class WavesPitch {
public:
    void prepare(float sampleRate)
    {
        sr = sampleRate;
        det.prepare(sr, 70.f); shifter.prepare(sr, 25.f);
        hop = std::max(32, (int)(sr * 0.004f));
        aCenter = 1.f - std::exp(-((float)hop / sr) / 0.12f);
        range = -1; setRange(1);
    }
    // 0 = Low (70 Hz+), 1 = Medium (100 Hz+), 2 = High (150 Hz+): detector range and shifter window
    void setRange(int r)
    {
        r = std::min(std::max(r, 0), 2);
        if (r == range) return;
        static const float minHz[3] = { 70.f, 100.f, 150.f }, winMs[3] = { 10.f, 7.f, 5.f };
        range = r; det.setMinHz(minHz[r]); shifter.setWindowMs(winMs[r]); resetState();
    }
    void reset() { det.reset(); shifter.reset(); resetState(); }
    void setLeadSec(float s) { kLeadSec = s; }
    int latency() const { return shifter.latency(); }
    float getDetectedHz() const { return lastF0; }

    void process(float* l, float* r, int n, const PitchParams& p, const WavesParams& w)
    {
        const float cSpeed = 1.f - std::exp(-1.f / (std::max(0.5f, p.speedMs) * 0.001f * sr));
        const float cTrans = 1.f - std::exp(-1.f / (std::max(0.5f, w.transMs) * 0.001f * sr));
        const float msPerSample = 1000.f / sr;
        const bool on = p.on;
        if (!on) { voiced = false; corrTarget = 0.f; curNote = -1000.f; lastF0 = 0.f; }
        for (int i = 0; i < n; ++i) {
            if (on) {
                det.push(0.5f * (l[i] + r[i]));
                if (++cnt >= hop) { cnt = 0; analyse(p, w); }
            }
            if (transTimer > 0.f) transTimer -= msPerSample;
            corrSm += (corrTarget - corrSm) * (transTimer > 0.f ? cTrans : cSpeed);
            const float ratio = std::exp2(corrSm * (1.f / 12.f));
            float yl, yr;
            shifter.process(l[i], r[i], ratio, yl, yr);
            l[i] = yl; r[i] = yr;
        }
    }

private:
    void resetState()
    {
        voiced = false; miss = 99; jump = 0; cnt = 0; curNote = -1000.f; center = 0.f; mPrev = 0.f; slope = 0.f;
        corrTarget = corrSm = 0.f; transTimer = 0.f; lastF0 = 0.f;
    }

    void analyse(const PitchParams& p, const WavesParams& w)
    {
        float conf = 0.f;
        const float f0 = det.detect(conf);
        if (f0 > 0.f && conf > 0.85f) {
            miss = 0;
            lastF0 = f0;
            const float m = 69.f + 12.f * std::log2(f0 / 440.f);
            const bool onset = !voiced;
            if (onset) { voiced = true; center = m; jump = 0; curNote = -1000.f; mPrev = m; slope = 0.f; }
            // pitch slope (semitones/s, smoothed): used to look ahead by the detection delay when ironing vibrato
            { const float dm = (m - mPrev) / ((float)hop / sr); slope += 0.5f * (dm - slope); mPrev = m; }
            const float mLead = m + std::max(-1.f, std::min(1.f, slope * kLeadSec));
            if (!onset) {
                if (std::fabs(m - center) > 0.7f) { if (++jump >= 2) { center = m; jump = 0; } }   // a new note: re-centre
                else { jump = 0; center += aCenter * (m - center); }                                // slow note centre
            }

            float t = PitchCorrector::quantize(center, p.key, p.scale);
            if (curNote > -900.f && t != curNote && std::fabs(center - curNote) < 1.5f &&
                std::fabs(center - curNote) <= std::fabs(center - t) + 0.25f)
                t = curNote;
            if (t != curNote) { if (curNote > -900.f) transTimer = w.transMs; curNote = t; }

            float err = curNote - center;                         // semitones
            if (std::fabs(err) <= w.tolCents * 0.01f) err = 0.f;  // inside the tolerance: leave it
            corrTarget = err * p.amount + (1.f - w.vibrato) * (center - mLead);   // note correction + vibrato ironing
        } else if (++miss >= 2) {
            voiced = false; corrTarget = 0.f; curNote = -1000.f; lastF0 = 0.f;
        }
    }

    float kLeadSec = 0.020f;   // look-ahead that cancels the detection delay (seconds)
    float mPrev = 0.f, slope = 0.f;
    float sr = 48000.f, aCenter = 0.03f, center = 0.f, curNote = -1000.f, corrTarget = 0.f, corrSm = 0.f, transTimer = 0.f, lastF0 = 0.f;
    bool voiced = false;
    int range = -1, miss = 99, jump = 0, cnt = 0, hop = 192;
    YinDetector det;
    PitchShifterStereo shifter;
};

} // namespace oc
