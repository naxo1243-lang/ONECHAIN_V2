#pragma once
#include <array>
#include "Common.h"
#include "Compressor.h"
#include "Multiband.h"
#include "Limiter.h"

namespace oc {

// Mastering chain:
//   input gain -> EQ (low cut, low shelf, mid bell, high shelf)
//   -> 3-band multiband (comp + sat + level) -> glue compressor (mix) -> drive (mix)
//   -> auto-gain (matches loudness to the EQ'd input) -> width + bass-mono -> output gain
//   -> true-peak limiter (always on, for safety and constant latency)
class MasterChain {
public:
    static constexpr int kChunk = Multiband::kChunk;

    struct Params {
        bool  enabled = true;
        float inDb = 0.f, lowCut = 20.f, lowDb = 0.f, midFreq = 2500.f, midDb = 0.f, highDb = 0.f;
        Multiband::Params mb;
        float glueThr = -12.f, glueRatio = 2.f, glueAtk = 30.f, glueRel = 200.f, glueMix = 0.f;   // mix 0..1
        float driveDb = 0.f, driveMix = 1.f;                                                    // mix 0..1
        bool  autoGain = true;
        float width = 1.f, bassMono = 0.f, outDb = 0.f, ceilingDb = -0.3f;                       // bassMono Hz (<=20 = off)
    };
    struct Meters { float inPeakDb = -100.f, outPeakDb = -100.f, glueGrDb = 0.f, limGrDb = 0.f, bandGr[3] = {0, 0, 0}; };

    void prepare(float sampleRate) {
        sr = sampleRate;
        mb.prepare(sr); glue.prepare(sr); lim.prepare(sr);
        for (int c = 0; c < 2; ++c) { cut1[c].reset(); cut2[c].reset(); lowS[c].reset(); midB[c].reset(); highS[c].reset(); }
        bmHP.reset(); bmHP2.reset();
        lastLowCut = lastLow = lastMidF = lastMid = lastHigh = lastBm = -1e9f;
        inG.prepare(sr, 15.f);   inG.setImmediate(1.f);
        outG.prepare(sr, 15.f);  outG.setImmediate(1.f);
        widthS.prepare(sr, 15.f); widthS.setImmediate(1.f);
        glueMixS.prepare(sr, 15.f); glueMixS.setImmediate(0.f);
        driveMixS.prepare(sr, 15.f); driveMixS.setImmediate(1.f);
        eIn = eOut = 0.f; agGain = 1.f; agTarget = 1.f;
        cE = 1.f - std::exp(-1.f / (0.4f * sr));
        cG = 1.f - std::exp(-1.f / (0.1f * sr));
    }
    int latency() const { return lim.latency(); }
    void setLookaheadMs(float ms) { lim.setLookaheadMs(ms); }
    const Meters& meters() const { return meters_; }

    void process(float* l, float* r, int n, const Params& p) {
        meters_.inPeakDb = meters_.outPeakDb = -100.f; meters_.glueGrDb = meters_.limGrDb = 0.f;
        for (int b = 0; b < 3; ++b) meters_.bandGr[b] = 0.f;
        int done = 0;
        while (done < n) {
            const int m = std::min(kChunk, n - done);
            chunk(l + done, r + done, m, p);
            done += m;
        }
    }

private:
    void updateFilters(const Params& p) {
        if (p.lowCut != lastLowCut) { for (int c = 0; c < 2; ++c) { cut1[c].highpass(sr, p.lowCut); cut2[c].highpass(sr, p.lowCut); } lastLowCut = p.lowCut; }
        if (p.lowDb != lastLow)   { for (int c = 0; c < 2; ++c) lowS[c].lowShelf(sr, 90.f, p.lowDb); lastLow = p.lowDb; }
        if (p.midDb != lastMid || p.midFreq != lastMidF) { for (int c = 0; c < 2; ++c) midB[c].peaking(sr, p.midFreq, p.midDb, 0.8f); lastMid = p.midDb; lastMidF = p.midFreq; }
        if (p.highDb != lastHigh) { for (int c = 0; c < 2; ++c) highS[c].highShelf(sr, 9000.f, p.highDb); lastHigh = p.highDb; }
        if (p.bassMono != lastBm) { bmHP.highpass(sr, std::max(p.bassMono, 21.f)); bmHP2.highpass(sr, std::max(p.bassMono, 21.f)); lastBm = p.bassMono; }
    }

    void chunk(float* l, float* r, int n, const Params& p) {
        { float pk = 0.f; for (int i = 0; i < n; ++i) pk = std::max(pk, std::max(std::fabs(l[i]), std::fabs(r[i])));
          meters_.inPeakDb = std::max(meters_.inPeakDb, linToDbFast(pk)); }

        if (p.enabled) {
            updateFilters(p);
            const bool cutOn = p.lowCut > 21.f, bmOn = p.bassMono > 20.f;
            const bool lowOn = std::fabs(p.lowDb) > 0.01f, midOn = std::fabs(p.midDb) > 0.01f, highOn = std::fabs(p.highDb) > 0.01f;
            inG.set(dbToLin(p.inDb)); outG.set(dbToLin(p.outDb)); widthS.set(p.width);
            glueMixS.set(p.glueMix); driveMixS.set(p.driveMix);

            // 1) input gain + EQ
            for (int i = 0; i < n; ++i) {
                const float g = inG.next();
                float a = l[i] * g, b = r[i] * g;
                if (cutOn) { a = cut2[0].process(cut1[0].process(a)); b = cut2[1].process(cut1[1].process(b)); }
                if (lowOn)  { a = lowS[0].process(a);  b = lowS[1].process(b); }
                if (midOn)  { a = midB[0].process(a);  b = midB[1].process(b); }
                if (highOn) { a = highS[0].process(a); b = highS[1].process(b); }
                l[i] = a; r[i] = b; refL[i] = a; refR[i] = b;
            }
            // 2) multiband
            float gr3[3] = {0, 0, 0};
            bool mbActive = false;
            for (int b = 0; b < 3; ++b)
                if (p.mb.b[b].comp > 0.5f || p.mb.b[b].sat > 0.5f || std::fabs(p.mb.b[b].gainDb) > 0.01f) mbActive = true;
            if (mbActive) {
                mb.process(l, r, n, p.mb, gr3);
                for (int b = 0; b < 3; ++b) meters_.bandGr[b] = std::max(meters_.bandGr[b], gr3[b]);
            }

            // 3) glue compressor with parallel mix
            if (p.glueMix > 0.001f || glueMixS.v > 0.001f) {
                for (int i = 0; i < n; ++i) { dryL[i] = l[i]; dryR[i] = r[i]; }
                Compressor::Params cp;
                cp.thrDb = p.glueThr; cp.ratio = p.glueRatio; cp.kneeDb = 6.f;
                cp.attackMs = p.glueAtk; cp.releaseMs = p.glueRel;
                const float gr = glue.process(l, r, n, cp);
                meters_.glueGrDb = std::max(meters_.glueGrDb, gr);
                for (int i = 0; i < n; ++i) {
                    const float m = glueMixS.next();
                    l[i] = dryL[i] * (1.f - m) + l[i] * m;
                    r[i] = dryR[i] * (1.f - m) + r[i] * m;
                }
            } else glue.reset();

            // 4) drive (saturation) with parallel mix
            if (p.driveDb > 0.05f) {
                const float g = dbToLin(p.driveDb), itn = 1.f / std::tanh(g);
                for (int i = 0; i < n; ++i) {
                    const float m = driveMixS.next();
                    l[i] = l[i] * (1.f - m) + (fastTanh(g * l[i]) * itn) * m;
                    r[i] = r[i] * (1.f - m) + (fastTanh(g * r[i]) * itn) * m;
                }
            }
            // 5) auto-gain: keep the loudness of the processed section equal to its input
            if (p.autoGain) {
                for (int i = 0; i < n; ++i) {
                    eIn  += cE * (0.5f * (refL[i] * refL[i] + refR[i] * refR[i]) - eIn);
                    eOut += cE * (0.5f * (l[i] * l[i] + r[i] * r[i]) - eOut);
                    if ((i & 15) == 0) agTarget = std::min(std::max(std::sqrt((eIn + 1e-10f) / (eOut + 1e-10f)), 0.1f), 4.f);
                    agGain += (agTarget - agGain) * cG;
                    l[i] *= agGain; r[i] *= agGain;
                }
            } else agGain = 1.f;

            // 6) stereo width + bass mono, 7) output gain
            for (int i = 0; i < n; ++i) {
                const float wd = widthS.next();
                const float mid = 0.5f * (l[i] + r[i]);
                float side = 0.5f * (l[i] - r[i]) * wd;
                if (bmOn) side = bmHP2.process(bmHP.process(side));
                const float og = outG.next();
                l[i] = (mid + side) * og; r[i] = (mid - side) * og;
            }
        }
        // 8) true-peak limiter (always on)
        meters_.limGrDb = std::max(meters_.limGrDb, lim.process(l, r, n, p.ceilingDb));
        { float pk = 0.f; for (int i = 0; i < n; ++i) pk = std::max(pk, std::max(std::fabs(l[i]), std::fabs(r[i])));
          meters_.outPeakDb = std::max(meters_.outPeakDb, linToDbFast(pk)); }
    }

    float sr = 44100.f;
    float lastLowCut = 0, lastLow = 0, lastMidF = 0, lastMid = 0, lastHigh = 0, lastBm = 0;
    Biquad cut1[2], cut2[2], lowS[2], midB[2], highS[2], bmHP, bmHP2;
    Smoothed inG, outG, widthS, glueMixS, driveMixS;
    float eIn = 0, eOut = 0, agGain = 1, agTarget = 1, cE = 0.001f, cG = 0.001f;
    Multiband mb;
    Compressor glue;
    TruePeakLimiter lim;
    Meters meters_;
    std::array<float, kChunk> refL, refR, dryL, dryR;
};

} // namespace oc
