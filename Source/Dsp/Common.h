#pragma once
#include <cmath>
#include <algorithm>
#include <vector>
#include <cstdint>
#include <cstring>

namespace oc {

constexpr float kPi = 3.14159265358979f;

inline float dbToLin(float d) { return std::pow(10.f, d / 20.f); }
inline float linToDb(float x) { return 20.f * std::log10(std::max(x, 1e-9f)); }
// one-pole coefficient for a time constant in milliseconds
inline float msToCoef(float ms, float sr) { return std::exp(-1.f / (0.001f * std::max(ms, 0.01f) * sr)); }


// ---------------------------------------------------------------------------
// Fast approximations for the per-sample hot loops (error far below audibility)
// ---------------------------------------------------------------------------
inline float fastLog2(float x)
{
    uint32_t xi; std::memcpy(&xi, &x, 4);
    const uint32_t mi = (xi & 0x007FFFFFu) | 0x3F000000u;
    float m; std::memcpy(&m, &mi, 4);
    return (float)xi * 1.1920928955078125e-7f - 124.22551499f - 1.498030302f * m - 1.72587999f / (0.3520887068f + m);
}
inline float fastExp2(float p)
{
    const float clipp = p < -126.f ? -126.f : p;
    const float offset = clipp < 0.f ? 1.f : 0.f;
    const int w = (int)clipp;
    const float z = clipp - (float)w + offset;
    const uint32_t vi = (uint32_t)((float)(1 << 23) * (clipp + 121.2740575f + 27.7280233f / (4.84252568f - z) - 1.49012907f * z));
    float v; std::memcpy(&v, &vi, 4);
    return v;
}
inline float linToDbFast(float x) { return 6.0205999f * fastLog2(x < 1e-9f ? 1e-9f : x); }
inline float dbToLinFast(float d) { return fastExp2(d * 0.16609640474f); }
// rational tanh: exact at 0, +-1 at |x| >= 3, continuous
inline float fastTanh(float x)
{
    if (x > 3.f) return 1.f;
    if (x < -3.f) return -1.f;
    const float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
}

// RBJ cookbook biquad (transposed direct form II)
struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    void reset() { z1 = z2 = 0; }
    void set(float nb0, float nb1, float nb2, float a0, float na1, float na2) {
        b0 = nb0 / a0; b1 = nb1 / a0; b2 = nb2 / a0; a1 = na1 / a0; a2 = na2 / a0;
    }
    static float clampF(float f, float sr) { return std::min(std::max(f, 10.f), 0.45f * sr); }

    void lowShelf(float sr, float f, float gDb) {
        f = clampF(f, sr);
        float A = std::pow(10.f, gDb / 40.f), w = 2 * kPi * f / sr;
        float cs = std::cos(w), sn = std::sin(w);
        float sq = 2 * std::sqrt(A) * (sn / 2 * std::sqrt(2.f));
        set(A * ((A + 1) - (A - 1) * cs + sq), 2 * A * ((A - 1) - (A + 1) * cs), A * ((A + 1) - (A - 1) * cs - sq),
            (A + 1) + (A - 1) * cs + sq, -2 * ((A - 1) + (A + 1) * cs), (A + 1) + (A - 1) * cs - sq);
    }
    void highShelf(float sr, float f, float gDb) {
        f = clampF(f, sr);
        float A = std::pow(10.f, gDb / 40.f), w = 2 * kPi * f / sr;
        float cs = std::cos(w), sn = std::sin(w);
        float sq = 2 * std::sqrt(A) * (sn / 2 * std::sqrt(2.f));
        set(A * ((A + 1) + (A - 1) * cs + sq), -2 * A * ((A - 1) + (A + 1) * cs), A * ((A + 1) + (A - 1) * cs - sq),
            (A + 1) - (A - 1) * cs + sq, 2 * ((A - 1) - (A + 1) * cs), (A + 1) - (A - 1) * cs - sq);
    }
    void peaking(float sr, float f, float gDb, float Q) {
        f = clampF(f, sr);
        float A = std::pow(10.f, gDb / 40.f), w = 2 * kPi * f / sr;
        float cs = std::cos(w), al = std::sin(w) / (2 * Q);
        set(1 + al * A, -2 * cs, 1 - al * A, 1 + al / A, -2 * cs, 1 - al / A);
    }
    void lowpass(float sr, float f, float Q = 0.70710678f) {
        f = clampF(f, sr);
        float w = 2 * kPi * f / sr, cs = std::cos(w), al = std::sin(w) / (2 * Q);
        set((1 - cs) / 2, 1 - cs, (1 - cs) / 2, 1 + al, -2 * cs, 1 - al);
    }
    void highpass(float sr, float f, float Q = 0.70710678f) {
        f = clampF(f, sr);
        float w = 2 * kPi * f / sr, cs = std::cos(w), al = std::sin(w) / (2 * Q);
        set((1 + cs) / 2, -(1 + cs), (1 + cs) / 2, 1 + al, -2 * cs, 1 - al);
    }
    void allpass(float sr, float f, float Q = 0.70710678f) {
        f = clampF(f, sr);
        float w = 2 * kPi * f / sr, cs = std::cos(w), al = std::sin(w) / (2 * Q);
        set(1 - al, -2 * cs, 1 + al, 1 + al, -2 * cs, 1 - al);
    }
    float process(float x) {
        float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// one-pole parameter smoother (avoids zipper noise)
struct Smoothed {
    float v = 0.f, t = 0.f, c = 0.01f;
    void prepare(float sr, float ms = 10.f) { c = 1.f - std::exp(-1.f / (0.001f * ms * sr)); }
    void setImmediate(float x) { v = t = x; }
    void set(float x) { t = x; }
    float next() { v += (t - v) * c; return v; }
};

} // namespace oc
