#pragma once
#include "PitchCorrector.h"
#include "OptoCompressor.h"
#include "LinearPhaseEq.h"
#include "ZeroLatencyEq.h"
#include "MasterChain.h"

namespace oc {

// Latency modes
//  Recording : shortest possible  - 10 ms pitch window, zero-latency EQ, 0.5 ms limiter look-ahead
//  Original  : v1 behaviour       - 25 ms pitch window, zero-latency EQ, 1.5 ms limiter look-ahead
//  Mixing    : linear-phase EQ    - 25 ms pitch window, linear-phase EQ (+~48 ms), 1.5 ms look-ahead
enum LatencyMode { ModeRecording = 0, ModeOriginal = 1, ModeMixing = 2 };

struct OptoParams { bool on = true; float peak = 40.f, gainDb = 0.f; bool limit = false; float link = 1.f, mix = 1.f; };

struct EngineParams {
    PitchParams pitch;
    OptoParams opto;
    EqParams eq;
    MasterChain::Params master;
    int mode = ModeRecording;
};

// The whole chain (JUCE independent, so it can be unit-tested): Pitch -> Opto -> EQ -> Master.
class Engine {
public:
    void prepare(float sampleRate)
    {
        pitch.prepare(sampleRate); opto.prepare(sampleRate);
        eqLin.prepare(sampleRate); eqZl.prepare(sampleRate);
        master.prepare(sampleRate);
        setMode(mode >= 0 ? mode : ModeRecording);
    }
    void setMode(int m)
    {
        m = std::min(std::max(m, 0), 2);
        pitch.setWindowMs(m == ModeRecording ? 10.f : 25.f);
        master.setLookaheadMs(m == ModeRecording ? 0.5f : 1.5f);
        if (m == ModeMixing) eqLin.reset(); else eqZl.reset();
        mode = m;
    }
    int  currentMode() const { return mode; }
    int  latency() const { return pitch.latency() + (mode == ModeMixing ? eqLin.latency() : 0) + master.latency(); }

    // Pitch -> Opto -> EQ.  Returns true when the latency mode changed during this call.
    bool processFront(float* l, float* r, int n, const EngineParams& p)
    {
        bool changed = false;
        if (p.mode != mode) { setMode(p.mode); changed = true; }
        pitch.process(l, r, n, p.pitch);
        if (p.opto.on) opto.process(l, r, n, p.opto.peak, p.opto.gainDb, p.opto.limit, p.opto.link, p.opto.mix);
        if (mode == ModeMixing) eqLin.process(l, r, n, p.eq); else eqZl.process(l, r, n, p.eq);
        return changed;
    }
    void processBack(float* l, float* r, int n, const EngineParams& p) { master.process(l, r, n, p.master); }

    const MasterChain::Meters& meters() const { return master.meters(); }
    float optoGainReductionDb() const { return opto.getGainReductionDb(); }

private:
    PitchCorrector pitch;
    OptoCompressor opto;
    LinearPhaseEq eqLin;
    ZeroLatencyEq eqZl;
    MasterChain master;
    int mode = -1;
};

} // namespace oc
