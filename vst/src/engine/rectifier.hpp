// Mesa/Boogie Dual Rectifier — RHYTHM (Orange) channel, white-box C++ port.
// Mirrors proto/rectifier.py (validated to 1.7 dB voicing / 11% THD vs the real-amp
// NAM). All component values from the factory Rev-F / 3-ch schematics (铁律一):
//   V1A -> [OR GAIN 1M] -> V2A -> V2B(unbypassed 39K cold clipper) -> V3A -> tone -> OT
// Reuses princeton::TriodeStage (Koren + cathode cap + coupling, Newton) and the MNA
// tone-stack pattern. Miller HF rolloff + OT leakage-resonance biquad complete the head.
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include "princeton.hpp"

namespace recti {

inline constexpr double kPi = 3.14159265358979323846;

// ---- one-pole LP (Miller/grid-stopper HF rolloff) ----------------------------
class OnePoleLP {
public:
    void set(double fs, double fc) {
        double wc = 2.0 * kPi * fc, k = 2.0 * fs;
        b_ = wc / (k + wc); a_ = (wc - k) / (k + wc); x1_ = y1_ = 0.0;
    }
    double step(double x) { double y = b_ * (x + x1_) - a_ * y1_; x1_ = x; y1_ = y; return y; }
private:
    double b_ = 1, a_ = 0, x1_ = 0, y1_ = 0;
};

// ---- resonant biquad LP (OT leakage-L + winding-C: presence peak then rolloff) -
class BiquadLP {
public:
    void set(double fs, double fc, double Q) {
        double w0 = 2.0 * kPi * fc / fs, c = std::cos(w0), s = std::sin(w0), al = s / (2.0 * Q);
        double a0 = 1.0 + al;
        b0_ = (1.0 - c) / 2.0 / a0; b1_ = (1.0 - c) / a0; b2_ = b0_;
        a1_ = -2.0 * c / a0; a2_ = (1.0 - al) / a0;
    }
    double step(double x) {
        double y = b0_ * x + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;
        x2_ = x1_; x1_ = x; y2_ = y1_; y1_ = y; return y;
    }
private:
    double b0_ = 1, b1_ = 0, b2_ = 0, a1_ = 0, a2_ = 0, x1_ = 0, x2_ = 0, y1_ = 0, y2_ = 0;
};

// ---- peaking EQ (RBJ) — Modern-mode pre-distortion mid scoop (250-800Hz dip) ----
class BiquadPeak {
public:
    void set(double fs, double fc, double Q, double gainDb) {
        double A = std::pow(10.0, gainDb / 40.0);
        double w0 = 2.0 * kPi * fc / fs, c = std::cos(w0), s = std::sin(w0), al = s / (2.0 * Q);
        double a0 = 1.0 + al / A;
        b0_ = (1.0 + al * A) / a0; b1_ = (-2.0 * c) / a0; b2_ = (1.0 - al * A) / a0;
        a1_ = (-2.0 * c) / a0; a2_ = (1.0 - al / A) / a0;
    }
    double step(double x) {
        double y = b0_ * x + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;
        x2_ = x1_; x1_ = x; y2_ = y1_; y1_ = y; return y;
    }
private:
    double b0_ = 1, b1_ = 0, b2_ = 0, a1_ = 0, a2_ = 0, x1_ = 0, x2_ = 0, y1_ = 0, y2_ = 0;
};

// ---- Recti tone stack: Marshall/FMV with a MID POT (Orange values) -----------
// TRBL 250K + 500pF, BASS 1M, MID 25K pot, slope 47K, bass/mid caps .02µF, master 1M.
// Same double-buffered RT-safe MNA as princeton::ToneStack; adds the mid-pot dof.
class RectiToneStack {
public:
    RectiToneStack(double fs, double treble, double mid, double bass, double master) {
        rebuild(fs, treble, mid, bass, master);
    }
    void rebuild(double fs, double treble, double mid, double bass, double master) {
        const double RT = 250e3, RB = 1e6, RM = 25e3, RV = 1e6, Rsrc = 1e3;
        auto tap = [](double p) { p = std::clamp(p, 1e-3, 0.999); return p * p; };
        double tw = tap(treble), mw = tap(mid), bw = tap(bass), vw = tap(master);
        double G[8][8] = {}, C[8][8] = {};
        auto R = [&](int a, int b, double ohms) {
            double g = 1.0 / std::max(ohms, 1.0);
            if (a > 0) G[a - 1][a - 1] += g;
            if (b > 0) G[b - 1][b - 1] += g;
            if (a > 0 && b > 0) { G[a - 1][b - 1] -= g; G[b - 1][a - 1] -= g; }
        };
        auto Cap = [&](int a, int b, double f) {
            if (a > 0) C[a - 1][a - 1] += f;
            if (b > 0) C[b - 1][b - 1] += f;
            if (a > 0 && b > 0) { C[a - 1][b - 1] -= f; C[b - 1][a - 1] -= f; }
        };
        R(1, 2, Rsrc);            Cap(2, 3, 500e-12);
        R(3, 6, (1 - tw) * RT);   R(6, 4, tw * RT);
        R(2, 4, 47e3);            Cap(4, 8, 0.02e-6);
        R(8, 5, bw * RB);         Cap(4, 5, 0.02e-6);
        R(5, 0, mw * RM);         // MID POT to gnd (Marshall-style)
        R(6, 7, (1 - vw) * RV);   R(7, 0, vw * RV);
        double k = 2.0 * fs, A1[N][N] = {}, A2[N][N] = {};
        for (int r = 0; r < 8; ++r)
            for (int c = 0; c < 8; ++c) { A1[r][c] = G[r][c] + k * C[r][c]; A2[r][c] = -G[r][c] + k * C[r][c]; }
        A1[0][8] = 1.0; A1[8][0] = 1.0; A2[8][0] = -1.0;
        double B[N] = {}; B[8] = 1.0;
        double aug[N][2 * N + 1];
        for (int r = 0; r < N; ++r) {
            for (int c = 0; c < N; ++c) { aug[r][c] = A1[r][c]; aug[r][N + c] = A2[r][c]; }
            aug[r][2 * N] = B[r];
        }
        for (int col = 0; col < N; ++col) {
            int piv = col;
            for (int r = col + 1; r < N; ++r) if (std::fabs(aug[r][col]) > std::fabs(aug[piv][col])) piv = r;
            for (int c = 0; c <= 2 * N; ++c) std::swap(aug[col][c], aug[piv][c]);
            double d = aug[col][col];
            for (int c = 0; c <= 2 * N; ++c) aug[col][c] /= d;
            for (int r = 0; r < N; ++r) {
                if (r == col) continue;
                double f = aug[r][col]; if (f == 0.0) continue;
                for (int c = 0; c <= 2 * N; ++c) aug[r][c] -= f * aug[col][c];
            }
        }
        int inactive = 1 - active_.load(std::memory_order_relaxed);
        for (int r = 0; r < N; ++r) {
            for (int c = 0; c < N; ++c) M_[inactive][r][c] = aug[r][N + c];
            K_[inactive][r] = aug[r][2 * N];
        }
        active_.store(inactive, std::memory_order_release);
    }
    double step(double u) {
        int a = active_.load(std::memory_order_acquire);
        const auto& M = M_[a]; const auto& K = K_[a];
        std::array<double, N> xn{};
        for (int r = 0; r < N; ++r) {
            double acc = K[r] * (u + u1_);
            for (int c = 0; c < N; ++c) acc += M[r][c] * x_[c];
            xn[r] = acc;
        }
        x_ = xn; u1_ = u; return x_[6];   // node 7 (master wiper)
    }
private:
    static constexpr int N = 9;
    std::array<std::array<std::array<double, N>, N>, 2> M_{};
    std::array<std::array<double, N>, 2> K_{};
    std::atomic<int> active_{0};
    std::array<double, N> x_{};
    double u1_ = 0.0;
};

// ---- the amp -----------------------------------------------------------------
class Rectifier {
public:
    Rectifier(double fs, double gain = 0.5, double treble = 0.4, double mid = 0.9,
              double bass = 0.3, double master = 0.7)
        : fs_(fs),
          v1a_(princeton::T12AX7(), fs, 200.0, 220e3, 1.8e3, 0.0, 0.001e-6, 1e6),
          v2a_(princeton::T12AX7(), fs, 280.0, 100e3, 1.8e3, 0.0, 0.001e-6, 1e6),
          v2b_(princeton::T12AX7(), fs, 384.0, 100e3, 39e3, 0.0, 0.001e-6, 1e6),
          v3a_(princeton::T12AX7(), fs, 213.0, 220e3, 1.8e3, 0.0, 0.001e-6, 1e6),
          stack_(fs, treble, mid, bass, master) {
        lp2a_.set(fs, 1.0 / (2.0 * kPi * 470e3 * 1.6e-12 * 41.0));
        lp2b_.set(fs, 1.0 / (2.0 * kPi * 470e3 * 1.6e-12 * 15.0));
        lp3a_.set(fs, 1.0 / (2.0 * kPi * 220e3 * 1.6e-12 * 51.0));
        ot_.set(fs, 3800.0, 1.4);
        atkA_ = 1.0 - std::exp(-1.0 / (0.002 * fs));   // ~2 ms sag attack
        setMode(2);        // Modern default (the 99% recording voice)
        setRectifier(0);   // Diode default (solid-state, tight, ~0 sag)
        setGain(gain);
    }
    void setGain(double g) { gain_ = std::pow(std::clamp(g, 1e-3, 0.999), 2.0); }
    void setTone(double treble, double mid, double bass, double master) {
        stack_.rebuild(fs_, treble, mid, bass, master);
    }
    void setInScale(double s) { inScale_ = s; }
    // 0 Raw (min pre-dist, power-amp crunch), 1 Vintage (smooth, mid bump, soft),
    // 2 Modern (pre-dist mid scoop 500Hz -10dB, hard "steel-plate wall").
    void setMode(int m) {
        mode_ = m < 0 ? 0 : (m > 2 ? 2 : m);
        if (mode_ == 2)      { scoop_.set(fs_, 500.0, 0.7, -10.0); modeGain_ = 1.15; }
        else if (mode_ == 1) { scoop_.set(fs_, 650.0, 0.6,  +2.0); modeGain_ = 0.9; }
        else                 { scoop_.set(fs_, 500.0, 0.7,   0.0); modeGain_ = 0.55; }
    }
    // 0 Diode (solid-state, stiff, ~0 sag), 1 Spongy (5U4 tube, ~12% sag, slow bloom).
    // THE feel/手感 knob the digital models miss: B+ droops with how hard you play.
    void setRectifier(int r) {
        rect_ = (r != 0);
        sagK_ = rect_ ? 0.12 : 0.02;                    // droop depth
        relA_ = 1.0 - std::exp(-1.0 / ((rect_ ? 0.25 : 0.05) * fs_));  // slow(Spongy)/fast recovery
    }

    double step(double x) {
        double drv = x * inScale_;
        // power-supply sag: envelope of the drive -> B+ droop that tracks how hard you
        // play. Diode ~stiff, Spongy ~12% + slow recovery = dynamic compression/bloom.
        double a = std::fabs(drv) > env_ ? atkA_ : relA_;
        env_ += a * (std::fabs(drv) - env_);
        double sag = 1.0 - sagK_ * std::min(env_ * 6.0, 1.0);   // 1.0 idle .. 1-sagK slammed
        double y = v1a_.step(drv,                       200.0 * sag);
        y = v2a_.step(lp2a_.step(y * gain_ * modeGain_), 280.0 * sag);
        y = v2b_.step(lp2b_.step(y),                     384.0 * sag);
        y = v3a_.step(lp3a_.step(y),                     213.0 * sag);
        if (mode_ != 0) y = scoop_.step(y);   // Modern scoop / Vintage mid-bump (pre-dist EQ)
        y = stack_.step(y);
        return ot_.step(y) * outGain_;
    }
    void processBlock(const double* in, double* out, int n) {
        for (int i = 0; i < n; ++i) out[i] = step(in[i]);
    }
    double sagEnv() const { return env_; }   // for the DAFx sag-envelope figure

private:
    double fs_, gain_ = 0.25, inScale_ = 1.5, modeGain_ = 1.0;
    double outGain_ = 0.6;                 // makeup: tone-stack out -> ~amp volts
    double env_ = 0.0, atkA_ = 0.1, relA_ = 0.01, sagK_ = 0.02;
    int mode_ = 2; bool rect_ = false;
    princeton::TriodeStage v1a_, v2a_, v2b_, v3a_;
    RectiToneStack stack_;
    OnePoleLP lp2a_, lp2b_, lp3a_;
    BiquadLP ot_;
    BiquadPeak scoop_;
};

}  // namespace recti
