#pragma once
#include "PitchCorrector.h"
#include "PrecisePitch.h"
#include "WavesPitch.h"
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

// Tuning algorithms (the UI colours them green / blue / red)
//  Original : our delay-line shifter (the v1 character)
//  Precise  : PSOLA - lands exactly on the note, formants preserved (needs ~15-29 ms, depending on Range)
//  Waves    : Waves Tune Real-Time style - 2.5-5 ms, Speed / Note Transition / Tolerance / Vibrato
enum PitchAlgo { AlgoOriginal = 0, AlgoPrecise = 1, AlgoWaves = 2 };

struct OptoParams { bool on = true; float peak = 40.f, gainDb = 0.f; bool limit = false; float link = 1.f, mix = 1.f; };

struct EngineParams {
    PitchParams pitch;
    int algo = AlgoOriginal;     // PitchAlgo
    int range = 1;               // 0 Low, 1 Medium, 2 High  (Precise and Waves)
    WavesParams waves;
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
        pitch.prepare(sampleRate); precise.prepare(sampleRate); waves.prepare(sampleRate); opto.prepare(sampleRate);
        eqLin.prepare(sampleRate); eqZl.prepare(sampleRate);
        master.prepare(sampleRate);
        setMode(mode >= 0 ? mode : ModeRecording);
        algo = -1; range = -1;
        setPitch(AlgoOriginal, 1);
    }
    void setPitch(int a, int r)
    {
        a = std::min(std::max(a, 0), 2); r = std::min(std::max(r, 0), 2);
        static const float preciseMinHz[3] = { 80.f, 100.f, 150.f };
        precise.setMinHz(preciseMinHz[r]);
        waves.setRange(r);
        if (a != algo) {                       // start the incoming algorithm from a clean state
            if (a == AlgoOriginal) pitch.reset(); else if (a == AlgoPrecise) precise.reset(); else waves.reset();
        }
        algo = a; range = r;
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
    int  pitchLatency() const { return algo == AlgoPrecise ? precise.latency() : (algo == AlgoWaves ? waves.latency() : pitch.latency()); }
    int  latency() const { return pitchLatency() + (mode == ModeMixing ? eqLin.latency() : 0) + master.latency(); }

    // Pitch -> Opto -> EQ.  Returns true when the latency mode changed during this call.
    bool processFront(float* l, float* r, int n, const EngineParams& p)
    {
        bool changed = false;
        if (p.mode != mode) { setMode(p.mode); changed = true; }
        if (p.algo != algo || p.range != range) { setPitch(p.algo, p.range); changed = true; }
        if (algo == AlgoPrecise)     precise.process(l, r, n, p.pitch);
        else if (algo == AlgoWaves)  waves.process(l, r, n, p.pitch, p.waves);
        else                         pitch.process(l, r, n, p.pitch);
        if (p.opto.on) opto.process(l, r, n, p.opto.peak, p.opto.gainDb, p.opto.limit, p.opto.link, p.opto.mix);
        if (mode == ModeMixing) eqLin.process(l, r, n, p.eq); else eqZl.process(l, r, n, p.eq);
        return changed;
    }
    void processBack(float* l, float* r, int n, const EngineParams& p) { master.process(l, r, n, p.master); }

    const MasterChain::Meters& meters() const { return master.meters(); }
    float optoGainReductionDb() const { return opto.getGainReductionDb(); }

private:
    PitchCorrector pitch;
    PrecisePitch precise;
    WavesPitch waves;
    OptoCompressor opto;
    LinearPhaseEq eqLin;
    ZeroLatencyEq eqZl;
    MasterChain master;
    int mode = -1, algo = -1, range = -1;
};

} // namespace oc
