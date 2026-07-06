// PURE WHITE-BOX pedal tone stages for C++ (ports proto/pedal_tone.py).
// Small linear MNA network with IDEAL OP-AMPS, trapezoidal (bilinear) discretized
// to companion form  x[n] = M x[n-1] + K (u[n]+u[n-1]) ; one voltage source.
// compile() (heap) runs only on a knob change; step() is allocation-free (RT-safe).
#pragma once
#include <vector>
#include <array>
#include <cmath>

namespace pedal {

class MnaTone {
public:
    MnaTone() {}
    MnaTone(int nNodes, double fs) { init(nNodes, fs); }
    void init(int nNodes, double fs) {
        n_ = nNodes; fs_ = fs;
        G_.assign(n_ * n_, 0.0); C_.assign(n_ * n_, 0.0);
        ops_.clear(); src_ = 1;
    }
    void R(int a, int b, double ohms) { stamp(G_, a, b, 1.0 / (ohms > 1e-9 ? ohms : 1e-9)); }
    void Cap(int a, int b, double f) { stamp(C_, a, b, f); }
    void Vsrc(int node) { src_ = node; }
    void Opamp(int vp, int vm, int vo) { ops_.push_back({vp, vm, vo}); }

    void compile(int outNode) {
        int m = (int)ops_.size();
        N_ = n_ + 1 + m;
        std::vector<double> A1(N_ * N_, 0.0), A2(N_ * N_, 0.0);
        double k = 2.0 * fs_;
        for (int r = 0; r < n_; ++r)
            for (int c = 0; c < n_; ++c) {
                A1[r * N_ + c] = G_[r * n_ + c] + k * C_[r * n_ + c];
                A2[r * N_ + c] = -G_[r * n_ + c] + k * C_[r * n_ + c];
            }
        int s = src_ - 1;
        A1[s * N_ + n_] = 1.0; A1[n_ * N_ + s] = 1.0; A2[n_ * N_ + s] = -1.0;
        for (int j = 0; j < m; ++j) {
            int col = n_ + 1 + j; auto& o = ops_[j];
            A1[(o[2] - 1) * N_ + col] = -1.0;            // i_op into vout
            A1[col * N_ + (o[0] - 1)] = 1.0;             // v+ - v- = 0
            A1[col * N_ + (o[1] - 1)] = -1.0;
        }
        std::vector<double> Ai = invert(A1, N_);
        M_.assign(N_ * N_, 0.0); K_.assign(N_, 0.0);
        for (int r = 0; r < N_; ++r)
            for (int c = 0; c < N_; ++c) {
                double acc = 0.0;
                for (int kk = 0; kk < N_; ++kk) acc += Ai[r * N_ + kk] * A2[kk * N_ + c];
                M_[r * N_ + c] = acc;
            }
        for (int r = 0; r < N_; ++r) K_[r] = Ai[r * N_ + n_];   // B has 1 at row n_
        x_.assign(N_, 0.0); xn_.assign(N_, 0.0); u1_ = 0.0; out_ = outNode - 1;
    }

    double step(double u) {
        for (int r = 0; r < N_; ++r) {
            double acc = K_[r] * (u + u1_);
            const double* Mr = &M_[r * N_];
            for (int c = 0; c < N_; ++c) acc += Mr[c] * x_[c];
            xn_[r] = acc;
        }
        x_.swap(xn_); u1_ = u; return x_[out_];
    }

private:
    void stamp(std::vector<double>& Mx, int a, int b, double v) {
        if (a > 0) Mx[(a - 1) * n_ + (a - 1)] += v;
        if (b > 0) Mx[(b - 1) * n_ + (b - 1)] += v;
        if (a > 0 && b > 0) { Mx[(a - 1) * n_ + (b - 1)] -= v; Mx[(b - 1) * n_ + (a - 1)] -= v; }
    }
    static std::vector<double> invert(std::vector<double> A, int N) {
        std::vector<double> I(N * N, 0.0);
        for (int i = 0; i < N; ++i) I[i * N + i] = 1.0;
        for (int col = 0; col < N; ++col) {
            int piv = col; double best = std::fabs(A[col * N + col]);
            for (int r = col + 1; r < N; ++r) { double v = std::fabs(A[r * N + col]); if (v > best) { best = v; piv = r; } }
            if (piv != col) for (int c = 0; c < N; ++c) { std::swap(A[col * N + c], A[piv * N + c]); std::swap(I[col * N + c], I[piv * N + c]); }
            double d = A[col * N + col];
            for (int c = 0; c < N; ++c) { A[col * N + c] /= d; I[col * N + c] /= d; }
            for (int r = 0; r < N; ++r) {
                if (r == col) continue; double f = A[r * N + col]; if (f == 0.0) continue;
                for (int c = 0; c < N; ++c) { A[r * N + c] -= f * A[col * N + c]; I[r * N + c] -= f * I[col * N + c]; }
            }
        }
        return I;
    }
    int n_ = 0, N_ = 0, src_ = 1, out_ = 0; double fs_ = 48000.0, u1_ = 0.0;
    std::vector<double> G_, C_, M_, K_, x_, xn_;
    std::vector<std::array<int, 3>> ops_;
};

// Ibanez TS808/TS9 Tone/Volume (JRC4558) — ElectroSmash real values.
inline MnaTone ts808_tone(double fs, double tone) {
    double a = 1.0 - std::min(std::max(tone, 1e-3), 0.999);   // pot-left frac
    MnaTone n(6, fs); n.Vsrc(1);
    n.R(1, 2, 1e3); n.Cap(2, 0, 0.22e-6); n.R(2, 0, 10e3);    // R7 / C5(723Hz LP) / R9 bias
    n.R(3, 4, 1e3);                                            // R11 feedback
    n.R(3, 5, a * 20e3); n.R(3, 4, (1 - a) * 20e3);            // Tone pot 20k (wiper=-in)
    n.Cap(5, 6, 0.22e-6); n.R(6, 0, 220.0);                    // C6 / R8 (3.3kHz)
    n.Opamp(2, 3, 4); n.compile(4); return n;
}

// Boss SD-1 Tone — ACTIVE op-amp EQ (same topology as the TS9, SD-1 values; per
// electric-safari + fig1). +in R7(10k)/C4(.018µF)=884Hz LP; -in R9(10k)||C6(.01µF)
// feedback (1.59kHz) + tone-pot wiper; pot 20k left->C5(.027µF)+R8(470)=12.5kHz to
// gnd, right->out. Sweeps a variable boost in ~1.6-12.5kHz. (C6 feedback exact
// placement is one reading — verify vs a clean SD-1 schematic.)
inline MnaTone sd1_tone(double fs, double tone) {
    double a = 1.0 - std::min(std::max(tone, 1e-3), 0.999);
    MnaTone n(6, fs); n.Vsrc(1);
    n.R(1, 2, 10e3); n.Cap(2, 0, 0.018e-6); n.R(2, 0, 100e3);
    n.R(3, 4, 10e3); n.Cap(3, 4, 0.01e-6);
    n.R(3, 5, a * 20e3); n.R(3, 4, (1 - a) * 20e3);
    n.Cap(5, 6, 0.027e-6); n.R(6, 0, 470.0);
    n.Opamp(2, 3, 4); n.compile(4); return n;
}

// Marshall Bluesbreaker — PASSIVE treble-cut (real values, AionFX Cerulean): R4 10k
// series, C4 10n treble-bleed cap, Tone 25k pot to ground (more = brighter). White-box.
inline MnaTone bb_tone(double fs, double tone) {
    double tw = std::min(std::max(tone, 1e-3), 0.999);
    MnaTone n(3, fs); n.Vsrc(1);
    n.R(1, 2, 10e3);            // R4 series (source impedance)
    n.Cap(2, 3, 10e-9);         // C4 treble-bleed
    n.R(3, 0, tw * 25e3);       // Tone pot 25k to gnd (tw->0 = full treble cut)
    n.compile(2); return n;
}

// Mad Professor / BJFE Dyna Red — PASSIVE "Treble" cut (real values): 3k series,
// 47n treble-bleed via 50k pot, 22n fixed to ground. White-box.
inline MnaTone dynared_tone(double fs, double tone) {
    double tw = std::min(std::max(tone, 1e-3), 0.999);
    MnaTone n(3, fs); n.Vsrc(1);
    n.R(1, 2, 3e3);             // 3k series
    n.Cap(2, 3, 47e-9);         // 47n treble-bleed
    n.R(3, 0, tw * 50e3);       // Treble pot 50k
    n.Cap(2, 0, 22e-9);         // 22n fixed treble roll to gnd
    n.compile(2); return n;
}

}  // namespace pedal
