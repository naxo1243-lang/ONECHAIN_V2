#pragma once
#include <vector>
#include <algorithm>
#include "Common.h"
#include "Fft.h"

namespace oc {

// ---------------------------------------------------------------------------
// Parametric EQ model (Pro-Q style): 8 bands, each with shape, freq, gain, Q,
// slope (cuts) and stereo placement.
// ---------------------------------------------------------------------------
constexpr int kEqBands = 8;
enum EqShape { EqBell = 0, EqLowShelf, EqHighShelf, EqLowCut, EqHighCut, EqNotch, EqBandPass, EqTilt, EqNumShapes };
enum EqPlace { EqStereo = 0, EqLeft, EqRight, EqMid, EqSide };
inline int eqSlopeDb(int idx) { static const int s[8] = { 6, 12, 18, 24, 36, 48, 72, 96 }; return s[std::min(std::max(idx, 0), 7)]; }
inline bool eqShapeHasGain(int shape) { return shape == EqBell || shape == EqLowShelf || shape == EqHighShelf || shape == EqTilt; }

struct EqBand {
    bool  on = false;
    int   shape = EqBell;
    float freq = 1000.f, gainDb = 0.f, q = 1.f;
    int   slope = 3;          // index into eqSlopeDb (cuts only)
    int   place = EqStereo;
    bool operator==(const EqBand& o) const {
        return on == o.on && shape == o.shape && freq == o.freq && gainDb == o.gainDb && q == o.q && slope == o.slope && place == o.place;
    }
};
struct EqParams {
    bool  on = true;
    float gainDb = 0.f;
    EqBand band[kEqBands];
    bool operator==(const EqParams& o) const {
        if (on != o.on || gainDb != o.gainDb) return false;
        for (int i = 0; i < kEqBands; ++i) if (!(band[i] == o.band[i])) return false;
        return true;
    }
};

// ---- biquad sections of a band (RBJ cookbook, Butterworth cascades for cuts) ----
struct EqSec { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
struct EqSecs { EqSec s[10]; int n = 0; };

inline EqSecs eqSections(const EqBand& b, double sr)
{
    EqSecs r;
    const double PI = 3.14159265358979323846;
    const double f = std::min(std::max((double)b.freq, 10.0), 0.49 * sr);
    const double w0 = 2.0 * PI * f / sr, cs = std::cos(w0), sn = std::sin(w0);
    const double Q = std::max(0.05, (double)b.q);
    auto push = [&](double b0, double b1, double b2, double a0, double a1, double a2) {
        EqSec s; s.b0 = b0 / a0; s.b1 = b1 / a0; s.b2 = b2 / a0; s.a1 = a1 / a0; s.a2 = a2 / a0;
        if (r.n < 10) r.s[r.n++] = s;
    };
    auto lowShelf = [&](double gDb, double q) {
        const double A = std::pow(10.0, gDb / 40.0), al = sn / (2.0 * q), be = 2.0 * std::sqrt(A) * al;
        push(A * ((A + 1) - (A - 1) * cs + be), 2 * A * ((A - 1) - (A + 1) * cs), A * ((A + 1) - (A - 1) * cs - be),
             (A + 1) + (A - 1) * cs + be, -2 * ((A - 1) + (A + 1) * cs), (A + 1) + (A - 1) * cs - be);
    };
    auto highShelf = [&](double gDb, double q) {
        const double A = std::pow(10.0, gDb / 40.0), al = sn / (2.0 * q), be = 2.0 * std::sqrt(A) * al;
        push(A * ((A + 1) + (A - 1) * cs + be), -2 * A * ((A - 1) + (A + 1) * cs), A * ((A + 1) + (A - 1) * cs - be),
             (A + 1) - (A - 1) * cs + be, 2 * ((A - 1) - (A + 1) * cs), (A + 1) - (A - 1) * cs - be);
    };

    switch (b.shape) {
    case EqBell: {
        const double A = std::pow(10.0, b.gainDb / 40.0), al = sn / (2.0 * Q);
        push(1 + al * A, -2 * cs, 1 - al * A, 1 + al / A, -2 * cs, 1 - al / A);
        break; }
    case EqLowShelf:  lowShelf(b.gainDb, Q);  break;
    case EqHighShelf: highShelf(b.gainDb, Q); break;
    case EqTilt:      lowShelf(0.5 * b.gainDb, 0.70710678); highShelf(-0.5 * b.gainDb, 0.70710678); break;
    case EqNotch: {
        const double al = sn / (2.0 * Q);
        push(1, -2 * cs, 1, 1 + al, -2 * cs, 1 - al);
        break; }
    case EqBandPass: {
        const double al = sn / (2.0 * Q);
        push(al, 0, -al, 1 + al, -2 * cs, 1 - al);
        break; }
    case EqLowCut:
    case EqHighCut: {
        const bool hp = (b.shape == EqLowCut);
        const int order = std::max(1, eqSlopeDb(b.slope) / 6);
        const double K = std::tan(w0 / 2.0);
        if (order % 2 == 1) {                       // first-order section
            if (hp) push(1, -1, 0, 1 + K, K - 1, 0);
            else    push(K, K, 0, 1 + K, K - 1, 0);
        }
        const int pairs = order / 2;
        for (int k = 1; k <= pairs; ++k) {
            double qk = 1.0 / (2.0 * std::cos((2.0 * k - 1.0) * PI / (2.0 * order)));
            if (k == pairs) qk *= Q / 0.70710678;    // user Q acts on the sharpest section (resonance)
            const double al = sn / (2.0 * qk);
            if (hp) push((1 + cs) / 2, -(1 + cs), (1 + cs) / 2, 1 + al, -2 * cs, 1 - al);
            else    push((1 - cs) / 2,   1 - cs,  (1 - cs) / 2, 1 + al, -2 * cs, 1 - al);
        }
        break; }
    default: break;
    }
    return r;
}

inline double eqSecMag2(const EqSec& s, double cw, double sw, double c2, double s2)
{
    const double nr = s.b0 + s.b1 * cw + s.b2 * c2,  ni = -(s.b1 * sw + s.b2 * s2);
    const double dr = 1.0 + s.a1 * cw + s.a2 * c2,  di = -(s.a1 * sw + s.a2 * s2);
    return (nr * nr + ni * ni) / (dr * dr + di * di + 1e-30);
}

// magnitude of one band at a frequency, in dB (used by the GUI curve too)
inline float eqBandResponseDb(const EqBand& b, float fHz, double sr)
{
    const EqSecs secs = eqSections(b, sr);
    const double w = 2.0 * 3.14159265358979323846 * (double)fHz / sr;
    const double cw = std::cos(w), sw = std::sin(w), c2 = std::cos(2 * w), s2 = std::sin(2 * w);
    double m2 = 1.0;
    for (int i = 0; i < secs.n; ++i) m2 *= eqSecMag2(secs.s[i], cw, sw, c2, s2);
    return (float)(10.0 * std::log10(m2 + 1e-30));
}

// ---------------------------------------------------------------------------
// Linear-phase engine: the band magnitudes are sampled on an FFT grid, turned into a
// symmetric (zero-phase) impulse response, delayed N/2 and windowed. Filtering uses
// uniform-partitioned FFT convolution. Mid/Side bands are handled with a 2x2 filter matrix.
// Parameter changes crossfade between the old and the new filter (no clicks).
// ---------------------------------------------------------------------------
class LinearPhaseEq {
public:
    static constexpr int B = 256;                         // internal block / partition size

    void prepare(float sampleRate)
    {
        sr = (double)sampleRate;
        N = 4096; while ((double)N < 4096.0 * sr / 48000.0 - 1.0) N <<= 1;
        K = N / 2; P = N / B;
        fftIr.init(N); fftBlk.init(2 * B);
        win.resize((size_t)N);
        for (int n = 0; n < N; ++n) win[(size_t)n] = 0.5f - 0.5f * (float)std::cos(2.0 * 3.14159265358979323846 * n / N);
        cw.resize((size_t)K + 1); sw.resize((size_t)K + 1); c2.resize((size_t)K + 1); s2.resize((size_t)K + 1);
        for (int k = 0; k <= K; ++k) {
            const double w = 3.14159265358979323846 * (double)k / (double)K;
            cw[(size_t)k] = std::cos(w); sw[(size_t)k] = std::sin(w); c2[(size_t)k] = std::cos(2 * w); s2[(size_t)k] = std::sin(2 * w);
        }
        for (int m = 0; m < 4; ++m) mag[m].assign((size_t)K + 1, 1.0);
        spec.assign((size_t)N, cpx(0.f, 0.f)); irBuf.assign((size_t)N, 0.f);
        blk.assign((size_t)(2 * B), cpx(0.f, 0.f)); acc.assign((size_t)(B + 1), cpx(0.f, 0.f));
        const size_t setSize = (size_t)4 * (size_t)P * (size_t)(B + 1);
        cur.H.assign(setSize, cpx(0.f, 0.f)); nxt.H.assign(setSize, cpx(0.f, 0.f));
        for (int c = 0; c < 2; ++c) {
            fdl[c].assign((size_t)P * (size_t)(B + 1), cpx(0.f, 0.f));
            hist[c].assign((size_t)B, 0.f); inBuf[c].assign((size_t)B, 0.f); outBuf[c].assign((size_t)B, 0.f);
        }
        fdlPos = 0; pos = 0; blocksSince = 100; xfade = false;
        designed = EqParams(); pending = designed;
        design(designed, cur);                              // flat = pure delay
    }

    int latency() const { return N / 2 + B; }

    // clear the signal history (the current filter is kept)
    void reset()
    {
        for (int c = 0; c < 2; ++c) {
            std::fill(fdl[c].begin(), fdl[c].end(), cpx(0.f, 0.f));
            std::fill(hist[c].begin(), hist[c].end(), 0.f);
            std::fill(inBuf[c].begin(), inBuf[c].end(), 0.f);
            std::fill(outBuf[c].begin(), outBuf[c].end(), 0.f);
        }
        pos = 0; fdlPos = 0; xfade = false;
    }

    void process(float* l, float* r, int n, const EqParams& p)
    {
        pending = p;
        for (int i = 0; i < n; ++i) {
            inBuf[0][(size_t)pos] = l[i]; inBuf[1][(size_t)pos] = r[i];
            l[i] = outBuf[0][(size_t)pos]; r[i] = outBuf[1][(size_t)pos];
            if (++pos == B) { runBlock(); pos = 0; }
        }
    }

private:
    struct Set { std::vector<cpx> H; bool matrix = false; };   // H[((o*2+i)*P+p)*(B+1)+k]

    size_t hIdx(int o, int i, int p) const { return (((size_t)(o * 2 + i)) * (size_t)P + (size_t)p) * (size_t)(B + 1); }

    void buildPartitions(const std::vector<double>& m, Set& s, int o, int i)
    {
        for (int k = 0; k <= K; ++k) spec[(size_t)k] = cpx((float)m[(size_t)k], 0.f);
        for (int k = 1; k < K; ++k) spec[(size_t)(N - k)] = spec[(size_t)k];
        fftIr.inverse(spec.data());
        for (int n = 0; n < N; ++n) irBuf[(size_t)n] = spec[(size_t)((n + N / 2) % N)].real() * win[(size_t)n];
        for (int p = 0; p < P; ++p) {
            for (int j = 0; j < B; ++j) blk[(size_t)j] = cpx(irBuf[(size_t)(p * B + j)], 0.f);
            for (int j = B; j < 2 * B; ++j) blk[(size_t)j] = cpx(0.f, 0.f);
            fftBlk.forward(blk.data());
            cpx* dst = &s.H[hIdx(o, i, p)];
            for (int k = 0; k <= B; ++k) dst[k] = blk[(size_t)k];
        }
    }

    void design(const EqParams& p, Set& s)
    {
        for (int m = 0; m < 4; ++m) std::fill(mag[m].begin(), mag[m].end(), 1.0);   // 0 L, 1 R, 2 M, 3 S
        bool matrix = false;
        if (p.on) {
            for (int b = 0; b < kEqBands; ++b) {
                const EqBand& bd = p.band[b];
                if (!bd.on) continue;
                const EqSecs secs = eqSections(bd, sr);
                int t0 = 0, t1 = 1;
                switch (bd.place) {
                    case EqLeft:  t0 = 0; t1 = 0; break;
                    case EqRight: t0 = 1; t1 = 1; break;
                    case EqMid:   t0 = 2; t1 = 2; matrix = true; break;
                    case EqSide:  t0 = 3; t1 = 3; matrix = true; break;
                    default:      t0 = 0; t1 = 1; break;
                }
                for (int k = 0; k <= K; ++k) {
                    double m2 = 1.0;
                    for (int q = 0; q < secs.n; ++q) m2 *= eqSecMag2(secs.s[q], cw[(size_t)k], sw[(size_t)k], c2[(size_t)k], s2[(size_t)k]);
                    const double m = std::sqrt(m2);
                    mag[t0][(size_t)k] *= m;
                    if (t1 != t0) mag[t1][(size_t)k] *= m;
                }
            }
            const double g = std::pow(10.0, (double)p.gainDb / 20.0);
            for (int k = 0; k <= K; ++k) { mag[0][(size_t)k] *= g; mag[1][(size_t)k] *= g; }
        }
        s.matrix = matrix;
        if (!matrix) {
            buildPartitions(mag[0], s, 0, 0);
            buildPartitions(mag[1], s, 1, 1);
        } else {
            std::vector<double>& ll = tmp[0]; std::vector<double>& lr = tmp[1]; std::vector<double>& rl = tmp[2]; std::vector<double>& rr = tmp[3];
            for (int m = 0; m < 4; ++m) tmp[m].resize((size_t)K + 1);
            for (int k = 0; k <= K; ++k) {
                const size_t q = (size_t)k;
                const double sum = 0.5 * (mag[2][q] + mag[3][q]), dif = 0.5 * (mag[2][q] - mag[3][q]);
                ll[q] = mag[0][q] * sum; lr[q] = mag[0][q] * dif; rl[q] = mag[1][q] * dif; rr[q] = mag[1][q] * sum;
            }
            buildPartitions(ll, s, 0, 0); buildPartitions(lr, s, 0, 1);
            buildPartitions(rl, s, 1, 0); buildPartitions(rr, s, 1, 1);
        }
    }

    void convolveSet(const Set& S, float* outL, float* outR)
    {
        float* outs[2] = { outL, outR };
        for (int o = 0; o < 2; ++o) {
            float* a = reinterpret_cast<float*>(acc.data());
            for (int k = 0; k < 2 * (B + 1); ++k) a[k] = 0.f;
            for (int i = 0; i < 2; ++i) {
                if (i != o && !S.matrix) continue;
                for (int p = 0; p < P; ++p) {
                    const size_t slot = (size_t)(((fdlPos - p) % P + P) % P);
                    const float* x = reinterpret_cast<const float*>(&fdl[i][slot * (size_t)(B + 1)]);
                    const float* h = reinterpret_cast<const float*>(&S.H[hIdx(o, i, p)]);
                    for (int k = 0; k <= B; ++k) {
                        const float xr = x[2 * k], xi = x[2 * k + 1], hr = h[2 * k], hi = h[2 * k + 1];
                        a[2 * k]     += xr * hr - xi * hi;
                        a[2 * k + 1] += xr * hi + xi * hr;
                    }
                }
            }
            blk[0] = cpx(acc[0].real(), 0.f); blk[(size_t)B] = cpx(acc[(size_t)B].real(), 0.f);
            for (int k = 1; k < B; ++k) { blk[(size_t)k] = acc[(size_t)k]; blk[(size_t)(2 * B - k)] = std::conj(acc[(size_t)k]); }
            fftBlk.inverse(blk.data());
            for (int j = 0; j < B; ++j) outs[o][j] = blk[(size_t)(B + j)].real();
        }
    }

    void runBlock()
    {
        if (!(pending == designed) && blocksSince >= 4) {
            design(pending, nxt); designed = pending; xfade = true; blocksSince = 0;
        } else ++blocksSince;

        fdlPos = (fdlPos + 1) % P;
        for (int c = 0; c < 2; ++c) {
            for (int j = 0; j < B; ++j) { blk[(size_t)j] = cpx(hist[c][(size_t)j], 0.f); blk[(size_t)(B + j)] = cpx(inBuf[c][(size_t)j], 0.f); }
            fftBlk.forward(blk.data());
            cpx* dst = &fdl[c][(size_t)fdlPos * (size_t)(B + 1)];
            for (int k = 0; k <= B; ++k) dst[k] = blk[(size_t)k];
            hist[c] = inBuf[c];
        }
        float oc0[B], oc1[B];
        convolveSet(cur, oc0, oc1);
        if (xfade) {
            float on0[B], on1[B];
            convolveSet(nxt, on0, on1);
            for (int j = 0; j < B; ++j) {
                const float t = (float)(j + 1) / (float)B;
                oc0[j] = oc0[j] * (1.f - t) + on0[j] * t;
                oc1[j] = oc1[j] * (1.f - t) + on1[j] * t;
            }
            std::swap(cur, nxt); xfade = false;
        }
        for (int j = 0; j < B; ++j) { outBuf[0][(size_t)j] = oc0[j]; outBuf[1][(size_t)j] = oc1[j]; }
    }

    double sr = 48000.0;
    int N = 4096, K = 2048, P = 16, fdlPos = 0, pos = 0, blocksSince = 100;
    bool xfade = false;
    Fft fftIr, fftBlk;
    std::vector<float> win, irBuf, hist[2], inBuf[2], outBuf[2];
    std::vector<double> cw, sw, c2, s2, mag[4], tmp[4];
    std::vector<cpx> spec, blk, acc, fdl[2];
    Set cur, nxt;
    EqParams designed, pending;
};

} // namespace oc
