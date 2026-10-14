#pragma once
#include "Common.h"
#include "LinearPhaseEq.h"

namespace oc {

// Zero-latency (minimum-phase, IIR) version of the same 8-band EQ: identical band shapes and
// magnitude response as LinearPhaseEq, but no delay. Used in the Recording / Original modes.
// Frequency, gain and Q are smoothed (20 ms) so moving a band never zippers or clicks.
// Mid/Side bands work in the M/S domain. Idle bands cost nothing.
class ZeroLatencyEq {
public:
    void prepare(float sampleRate)
    {
        sr = (double)sampleRate;
        for (int b = 0; b < kEqBands; ++b) active[b] = false;
        gPrev = 1.f;
        reset();
    }
    void reset()
    {
        for (int b = 0; b < kEqBands; ++b) for (int k = 0; k < 10; ++k) for (int c = 0; c < 2; ++c) f[b][k][c].reset();
    }

    void process(float* l, float* r, int n, const EqParams& p)
    {
        if (!p.on) { for (int b = 0; b < kEqBands; ++b) active[b] = false; gPrev = 1.f; return; }

        const float a = 1.f - std::exp(-(float)n / ((float)sr * 0.02f));
        bool anyMS = false;
        for (int b = 0; b < kEqBands; ++b) {
            const EqBand& t = p.band[b];
            if (!t.on) { active[b] = false; continue; }
            bool recompute = false;
            if (!active[b]) {
                cur[b] = t; active[b] = true; recompute = true;
                for (int k = 0; k < 10; ++k) for (int c = 0; c < 2; ++c) f[b][k][c].reset();
            } else {
                EqBand& c = cur[b];
                if (c.shape != t.shape || c.slope != t.slope) { c.shape = t.shape; c.slope = t.slope; recompute = true; }
                c.place = t.place;
                const float lf = std::log(c.freq), tf = std::log(t.freq);
                const float nf = (std::fabs(tf - lf) < 1e-4f) ? t.freq : std::exp(lf + a * (tf - lf));
                const float lq = std::log(c.q), tq = std::log(t.q);
                const float nq = (std::fabs(tq - lq) < 1e-4f) ? t.q : std::exp(lq + a * (tq - lq));
                const float ng = (std::fabs(t.gainDb - c.gainDb) < 1e-3f) ? t.gainDb : c.gainDb + a * (t.gainDb - c.gainDb);
                if (nf != c.freq || nq != c.q || ng != c.gainDb) { c.freq = nf; c.q = nq; c.gainDb = ng; recompute = true; }
            }
            if (recompute) {
                const EqSecs s = eqSections(cur[b], sr);
                nSecs[b] = s.n;
                for (int k = 0; k < s.n; ++k)
                    for (int c = 0; c < 2; ++c) {
                        Biquad& q = f[b][k][c];
                        q.b0 = (float)s.s[k].b0; q.b1 = (float)s.s[k].b1; q.b2 = (float)s.s[k].b2;
                        q.a1 = (float)s.s[k].a1; q.a2 = (float)s.s[k].a2;
                    }
            }
            if (t.place == EqMid || t.place == EqSide) anyMS = true;
        }

        // stage 1: L/R-domain bands
        for (int b = 0; b < kEqBands; ++b) {
            if (!active[b]) continue;
            const int pl = cur[b].place;
            if (pl == EqMid || pl == EqSide) continue;
            for (int k = 0; k < nSecs[b]; ++k) {
                if (pl != EqRight) { Biquad& q = f[b][k][0]; for (int i = 0; i < n; ++i) l[i] = q.process(l[i]); }
                if (pl != EqLeft)  { Biquad& q = f[b][k][1]; for (int i = 0; i < n; ++i) r[i] = q.process(r[i]); }
            }
        }
        // stage 2: Mid/Side bands
        if (anyMS) {
            for (int i = 0; i < n; ++i) { const float m = 0.5f * (l[i] + r[i]), s = 0.5f * (l[i] - r[i]); l[i] = m; r[i] = s; }
            for (int b = 0; b < kEqBands; ++b) {
                if (!active[b]) continue;
                const int pl = cur[b].place;
                if (pl != EqMid && pl != EqSide) continue;
                float* x = (pl == EqMid) ? l : r;
                const int c = (pl == EqMid) ? 0 : 1;
                for (int k = 0; k < nSecs[b]; ++k) { Biquad& q = f[b][k][c]; for (int i = 0; i < n; ++i) x[i] = q.process(x[i]); }
            }
            for (int i = 0; i < n; ++i) { const float m = l[i], s = r[i]; l[i] = m + s; r[i] = m - s; }
        }
        // output gain (ramped over the block)
        const float g1 = (std::fabs(p.gainDb) > 0.001f) ? dbToLin(p.gainDb) : 1.f;
        if (g1 != 1.f || gPrev != 1.f) {
            const float step = (g1 - gPrev) / (float)n;
            float g = gPrev;
            for (int i = 0; i < n; ++i) { g += step; l[i] *= g; r[i] *= g; }
        }
        gPrev = g1;
    }

private:
    double sr = 48000.0;
    float gPrev = 1.f;
    bool active[kEqBands] = {};
    int nSecs[kEqBands] = {};
    EqBand cur[kEqBands];
    Biquad f[kEqBands][10][2];
};

} // namespace oc
