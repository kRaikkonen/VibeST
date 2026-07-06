// Dumble Steel String Singer (SSS) — white-box C++ port. Single-channel CLEAN amp
// (no OD circuit / no clipping diodes; "Drive" = pushing the same amp harder). Values
// from the Colgan #002 LTspice netlist (铁律一). Fit to the trusted NAM: 1.37 dB voicing.
//   in -> V1 (12AX7 Rp100k/Rk1.5k/Ck5µF) -> FMV tone stack -> V2 -> OT resonance
// Reuses princeton::TriodeStage and recti::{OnePoleLP,BiquadLP}.
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include "princeton.hpp"
#include "rectifier.hpp"

namespace dumble {

// FMV tone stack, SSS #002 values: TRBL 250k+360pF, BASS 250k+0.1µF, MID 100k pot+.047µF,
// slope 100k, Volume 1M. Same double-buffered RT-safe MNA as the others.
class DumbleToneStack {
public:
    DumbleToneStack(double fs, double treble, double mid, double bass, double volume) {
        rebuild(fs, treble, mid, bass, volume);
    }
    void rebuild(double fs, double treble, double mid, double bass, double volume) {
        const double RT = 250e3, RB = 250e3, RM = 100e3, RV = 1e6, Rsrc = 40e3;
        auto tap = [](double p) { p = std::clamp(p, 1e-3, 0.999); return p * p; };
        double tw = tap(treble), mw = tap(mid), bw = tap(bass), vw = tap(volume);
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
        R(1, 2, Rsrc);            Cap(2, 3, 360e-12);
        R(3, 6, (1 - tw) * RT);   R(6, 4, tw * RT);
        R(2, 4, 100e3);           Cap(4, 8, 0.1e-6);
        R(8, 5, bw * RB);         Cap(4, 5, 0.047e-6);
        R(5, 0, mw * RM);
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
        x_ = xn; u1_ = u; return x_[6];
    }
private:
    static constexpr int N = 9;
    std::array<std::array<std::array<double, N>, N>, 2> M_{};
    std::array<std::array<double, N>, 2> K_{};
    std::atomic<int> active_{0};
    std::array<double, N> x_{};
    double u1_ = 0.0;
};

class Dumble {
public:
    Dumble(double fs, double drive = 0.3, double treble = 0.75, double mid = 0.9,
           double bass = 0.1, double volume = 0.7)
        : fs_(fs),
          v1_(princeton::T12AX7(), fs, 325.0, 100e3, 1.5e3, 5e-6, 0.02e-6, 1e6),
          v2_(princeton::T12AX7(), fs, 340.0, 100e3, 1.5e3, 5e-6, 0.02e-6, 1e6),
          stack_(fs, treble, mid, bass, volume) {
        miller_.set(fs, 1.0 / (2.0 * recti::kPi * 100e3 * 1.6e-12 * 61.0));
        ot_.set(fs, 4500.0, 1.3);         // OT + presence resonance (fit)
        setDrive(drive);
    }
    void setDrive(double d) { drive_ = 0.3 + 1.7 * std::clamp(d, 0.0, 1.0); }
    void setTone(double treble, double mid, double bass, double volume) {
        stack_.rebuild(fs_, treble, mid, bass, volume);
    }
    void setInScale(double s) { inScale_ = s; }

    double step(double x) {
        double y = v1_.step(x * inScale_);
        y = miller_.step(y);
        y = stack_.step(y);
        y = v2_.step(y * drive_);
        return ot_.step(y) * outGain_;
    }
    void processBlock(const double* in, double* out, int n) {
        for (int i = 0; i < n; ++i) out[i] = step(in[i]);
    }
private:
    double fs_, drive_ = 0.8, inScale_ = 1.0, outGain_ = 0.5;
    princeton::TriodeStage v1_, v2_;
    DumbleToneStack stack_;
    recti::OnePoleLP miller_;
    recti::BiquadLP ot_;
};

}  // namespace dumble
