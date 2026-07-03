// Small self-contained DSP infrastructure for the standalone practice amp:
// radix-2 FFT, uniform partitioned convolution (spring tank), and 2x
// halfband resamplers for the 48k <-> 96k <-> 192k rate ladder.
#pragma once
#include <complex>
#include <cstring>
#include <vector>

namespace dsp {

using cplx = std::complex<double>;

// in-place iterative radix-2 FFT (n = power of two)
inline void fft(std::vector<cplx>& a, bool inverse) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        double ang = 2.0 * 3.14159265358979323846 / static_cast<double>(len)
                     * (inverse ? 1.0 : -1.0);
        cplx wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            cplx w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k) {
                cplx u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    if (inverse)
        for (auto& x : a) x /= static_cast<double>(n);
}

// uniform partitioned convolution, block size B, overlap-save
class PartConv {
public:
    void init(const std::vector<double>& ir, size_t B) {
        B_ = B;
        N_ = 2 * B;
        P_ = (ir.size() + B - 1) / B;
        H_.assign(P_, std::vector<cplx>(N_));
        for (size_t p = 0; p < P_; ++p) {
            std::vector<cplx> h(N_, cplx(0, 0));
            for (size_t k = 0; k < B && p * B + k < ir.size(); ++k)
                h[k] = ir[p * B + k];
            fft(h, false);
            H_[p] = std::move(h);
        }
        fdl_.assign(P_, std::vector<cplx>(N_, cplx(0, 0)));
        head_ = 0;
        prev_.assign(B, 0.0);
        buf_.resize(N_);
        acc_.resize(N_);
    }

    // in/out length must equal B
    void process(const double* in, double* out) {
        for (size_t k = 0; k < B_; ++k) {
            buf_[k] = prev_[k];
            buf_[B_ + k] = in[k];
        }
        fft(buf_, false);
        head_ = (head_ + P_ - 1) % P_;
        fdl_[head_] = buf_;
        for (size_t k = 0; k < N_; ++k) acc_[k] = cplx(0, 0);
        for (size_t p = 0; p < P_; ++p) {
            const auto& X = fdl_[(head_ + p) % P_];
            const auto& H = H_[p];
            for (size_t k = 0; k < N_; ++k) acc_[k] += X[k] * H[k];
        }
        fft(acc_, true);
        for (size_t k = 0; k < B_; ++k) out[k] = acc_[B_ + k].real();
        std::memcpy(prev_.data(), in, B_ * sizeof(double));
    }

private:
    size_t B_ = 0, N_ = 0, P_ = 0, head_ = 0;
    std::vector<std::vector<cplx>> H_, fdl_;
    std::vector<double> prev_;
    std::vector<cplx> buf_, acc_;
};

// 63-tap halfband (firwin(63, 0.5, kaiser 8.0))
static const double kHalfband[63] = {
    -2.4015252436811136e-05, 3.0822444080757243e-19, 1.0903623056181318e-04, -3.1803581527749437e-19,
    -2.9356008100292686e-04, -8.6531077841155339e-19, 6.3810637543991835e-04, -1.3144009748006918e-18,
    -1.2220760031961134e-03, 8.0079556174234605e-18, 2.1459085710929925e-03, -3.3961626450342012e-18,
    -3.5344143020879315e-03, 4.9048638605022585e-18, 5.5436470171553454e-03, -6.7030306440093657e-18,
    -8.3762104292271784e-03, 8.7311353118264772e-18, 1.2315583034667371e-02, -1.0895892751698457e-17,
    -1.7804700211773501e-02, 1.3076017880177753e-17, 2.5637685282126059e-02, -1.5132334992271515e-17,
    -3.7489381567611592e-02, 1.6921183893060975e-17, 5.7724044394320502e-02, -1.8309479118436543e-17,
    -1.0244250878154293e-01, 1.9189426547096118e-17, 3.1707287210093404e-01, 4.9999996724516188e-01,
    3.1707287210093404e-01, 1.9189426547096118e-17, -1.0244250878154293e-01, -1.8309479118436543e-17,
    5.7724044394320502e-02, 1.6921183893060975e-17, -3.7489381567611592e-02, -1.5132334992271515e-17,
    2.5637685282126059e-02, 1.3076017880177753e-17, -1.7804700211773501e-02, -1.0895892751698457e-17,
    1.2315583034667371e-02, 8.7311353118264772e-18, -8.3762104292271784e-03, -6.7030306440093657e-18,
    5.5436470171553454e-03, 4.9048638605022585e-18, -3.5344143020879315e-03, -3.3961626450342012e-18,
    2.1459085710929925e-03, 8.0079556174234605e-18, -1.2220760031961134e-03, -1.3144009748006918e-18,
    6.3810637543991835e-04, -8.6531077841155339e-19, -2.9356008100292686e-04, -3.1803581527749437e-19,
    1.0903623056181318e-04, 3.0822444080757243e-19, -2.4015252436811136e-05,
};

class FIR63 {
public:
    double step(double x) {
        hist_[pos_] = x;
        double acc = 0.0;
        size_t idx = pos_;
        for (int k = 0; k < 63; ++k) {
            acc += kHalfband[k] * hist_[idx];
            idx = (idx + 63 - 1) % 63;
        }
        pos_ = (pos_ + 1) % 63;
        return acc;
    }
private:
    double hist_[63] = {};
    size_t pos_ = 0;
};

// 2x upsampler: n in -> 2n out
class Up2 {
public:
    void process(const double* in, double* out, int n) {
        for (int i = 0; i < n; ++i) {
            out[2 * i] = 2.0 * fir_.step(in[i]);
            out[2 * i + 1] = 2.0 * fir_.step(0.0);
        }
    }
private:
    FIR63 fir_;
};

// 2x downsampler: 2n in -> n out
class Down2 {
public:
    void process(const double* in, double* out, int n) {
        for (int i = 0; i < n; ++i) {
            double y = fir_.step(in[2 * i]);
            fir_.step(in[2 * i + 1]);
            out[i] = y;
        }
    }
private:
    FIR63 fir_;
};

// biquad (RBJ), for the placeholder monitor/cab filter
class Biquad {
public:
    void lowpass(double f, double q, double fs) {
        double w = 2 * 3.14159265358979323846 * f / fs;
        double alpha = std::sin(w) / (2 * q), c = std::cos(w);
        double a0 = 1 + alpha;
        b0_ = (1 - c) / 2 / a0; b1_ = (1 - c) / a0; b2_ = b0_;
        a1_ = -2 * c / a0; a2_ = (1 - alpha) / a0;
    }
    void highpass(double f, double q, double fs) {
        double w = 2 * 3.14159265358979323846 * f / fs;
        double alpha = std::sin(w) / (2 * q), c = std::cos(w);
        double a0 = 1 + alpha;
        b0_ = (1 + c) / 2 / a0; b1_ = -(1 + c) / a0; b2_ = b0_;
        a1_ = -2 * c / a0; a2_ = (1 - alpha) / a0;
    }
    void peaking(double f, double q, double gainDb, double fs) {
        double A = std::pow(10.0, gainDb / 40.0);
        double w = 2 * 3.14159265358979323846 * f / fs;
        double alpha = std::sin(w) / (2 * q), c = std::cos(w);
        double a0 = 1 + alpha / A;
        b0_ = (1 + alpha * A) / a0; b1_ = -2 * c / a0;
        b2_ = (1 - alpha * A) / a0;
        a1_ = -2 * c / a0; a2_ = (1 - alpha / A) / a0;
    }
    double step(double x) {
        double y = b0_ * x + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;
        x2_ = x1_; x1_ = x;
        y2_ = y1_; y1_ = y;
        return y;
    }
private:
    double b0_ = 1, b1_ = 0, b2_ = 0, a1_ = 0, a2_ = 0;
    double x1_ = 0, x2_ = 0, y1_ = 0, y2_ = 0;
};

}  // namespace dsp
