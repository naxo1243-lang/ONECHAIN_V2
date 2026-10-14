#pragma once
#include <array>
#include "Common.h"
#include "Compressor.h"

namespace oc {

// 3-band processor: Linkwitz-Riley 4th-order crossovers (phase-aligned, flat sum),
// per-band compression, saturation and level.
class Multiband {
public:
    static constexpr int kChunk = 512;
    struct BandParams { float comp = 0.f, sat = 0.f, gainDb = 0.f; };   // comp/sat 0..100
    struct Params { float x1 = 150.f, x2 = 3500.f; BandParams b[3]; };

    void prepare(float sampleRate) {
        sr = sampleRate;
        for (int i = 0; i < 3; ++i) comp[i].prepare(sr);
        for (int c = 0; c < 2; ++c) for (int k = 0; k < 9; ++k) f[c][k].reset();
        lastX1 = lastX2 = -1.f;
    }

    // n <= kChunk. bandGr: optional array of 3 floats receiving max GR per band.
    void process(float* l, float* r, int n, const Params& p, float* bandGr = nullptr) {
        updateCrossovers(p.x1, p.x2);
        float* io[2] = { l, r };
        for (int c = 0; c < 2; ++c) {
            for (int i = 0; i < n; ++i) {
                const float x = io[c][i];
                const float low  = f[c][1].process(f[c][0].process(x));
                const float rest = f[c][3].process(f[c][2].process(x));
                const float mid  = f[c][5].process(f[c][4].process(rest));
                const float high = f[c][7].process(f[c][6].process(rest));
                // phase-align the low band with the second crossover: LR4 LP+HP is exactly a 2nd-order all-pass
                const float lowAp = f[c][8].process(low);
                band[0][c][i] = lowAp; band[1][c][i] = mid; band[2][c][i] = high;
            }
        }
        static const float atk[3] = { 30.f, 15.f, 5.f };
        static const float rel[3] = { 180.f, 120.f, 80.f };
        for (int b = 0; b < 3; ++b) {
            float gr = 0.f;
            const BandParams& bp = p.b[b];
            if (bp.comp > 0.5f) {
                Compressor::Params cp;
                cp.thrDb = -6.f - bp.comp * 0.30f;
                cp.ratio = 2.f + bp.comp * 0.03f;
                cp.kneeDb = 8.f; cp.attackMs = atk[b]; cp.releaseMs = rel[b];
                gr = comp[b].process(band[b][0].data(), band[b][1].data(), n, cp);
            } else comp[b].reset();
            if (bp.sat > 0.5f) {
                const float g = dbToLin(bp.sat * 0.12f), tn = std::tanh(g), itn = 1.f / tn;
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < n; ++i) band[b][c][i] = fastTanh(g * band[b][c][i]) * itn;
            }
            const float lv = dbToLin(bp.gainDb);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) band[b][c][i] *= lv;
            if (bandGr) bandGr[b] = gr;
        }
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i) io[c][i] = band[0][c][i] + band[1][c][i] + band[2][c][i];
    }

private:
    void updateCrossovers(float x1, float x2) {
        x2 = std::max(x2, x1 * 1.5f);
        if (x1 == lastX1 && x2 == lastX2) return;
        for (int c = 0; c < 2; ++c) {
            f[c][0].lowpass(sr, x1);  f[c][1].lowpass(sr, x1);
            f[c][2].highpass(sr, x1); f[c][3].highpass(sr, x1);
            f[c][4].lowpass(sr, x2);  f[c][5].lowpass(sr, x2);
            f[c][6].highpass(sr, x2); f[c][7].highpass(sr, x2);
            f[c][8].allpass(sr, x2);
        }
        lastX1 = x1; lastX2 = x2;
    }
    float sr = 44100.f, lastX1 = -1.f, lastX2 = -1.f;
    Biquad f[2][9];
    Compressor comp[3];
    std::array<float, kChunk> band[3][2];
};

} // namespace oc
