#pragma once
#include <complex>
#include <vector>
#include <cmath>
#include <utility>

namespace oc {

using cpx = std::complex<float>;

// Iterative radix-2 FFT (power-of-two sizes), in place.
class Fft {
public:
    void init(int size) {
        n = size;
        rev.assign((size_t)n, 0);
        int bits = 0; while ((1 << bits) < n) ++bits;
        for (int i = 0; i < n; ++i) {
            int r = 0;
            for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            rev[(size_t)i] = r;
        }
        tw.resize((size_t)(n / 2));
        for (int k = 0; k < n / 2; ++k) {
            const double a = -2.0 * 3.14159265358979323846 * (double)k / (double)n;
            tw[(size_t)k] = cpx((float)std::cos(a), (float)std::sin(a));
        }
    }
    int size() const { return n; }

    void forward(cpx* x) const {
        for (int i = 0; i < n; ++i) { const int j = rev[(size_t)i]; if (j > i) std::swap(x[i], x[j]); }
        for (int len = 2; len <= n; len <<= 1) {
            const int half = len >> 1, step = n / len;
            for (int i = 0; i < n; i += len)
                for (int j = 0; j < half; ++j) {
                    const cpx w = tw[(size_t)(j * step)];
                    const cpx a = x[i + j], b = x[i + j + half];
                    const float vr = b.real() * w.real() - b.imag() * w.imag();
                    const float vi = b.real() * w.imag() + b.imag() * w.real();
                    x[i + j]        = cpx(a.real() + vr, a.imag() + vi);
                    x[i + j + half] = cpx(a.real() - vr, a.imag() - vi);
                }
        }
    }
    void inverse(cpx* x) const {   // includes 1/n scaling
        for (int i = 0; i < n; ++i) x[i] = std::conj(x[i]);
        forward(x);
        const float s = 1.f / (float)n;
        for (int i = 0; i < n; ++i) x[i] = cpx(x[i].real() * s, -x[i].imag() * s);
    }
private:
    int n = 0;
    std::vector<int> rev;
    std::vector<cpx> tw;
};

} // namespace oc
