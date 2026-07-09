// Vox AC30 Top Boost (JMI AC30/6 + OS/010 Top Boost unit) — white-box C++.
// Circuit values transcribed from the original JMI drawings (铁律一):
//   OS/065 "AC30.36 Amplifier Circuit" (29-4-60, iss.4 1964)
//   OS/010 "Top Boost Mod — Optional Brilliance Unit" (J. Bell, 11-12-61)
// cross-checked vs R. Kuehnel (ampbooks.com) AC30 analysis. Head is
// "(schematic)": real values + real topology, no NAM capture on hand yet.
//
//   in -> V1 (ECC83, 220k/1.5k||25u, plate 170V) -> C3 500p (Brilliant coupling)
//      -> VR2 500k log volume + 100p TB bright cap
//      -> TB gain triode (ECC83 100k/1.5k||25u, 290V rail) -> DC-coupled
//      -> cathode follower (56k) -> Top Boost tone stack
//         (50p / Treble 1M log / slope 100k / 2x 22n / Bass 1M log / 10k)
//      -> R9 220k mix into PI grid (1M leak)
//      -> LTP phase inverter (ECC83 100k/100k, 47k+1.2k tail, 0.15u out)
//      -> CUT control (250k log + 4.7n differentially across the PI outputs)
//      -> 4x EL84 CATHODE-BIASED (50R shared || 250u), screens 310V via 100R,
//         NO negative feedback, OT 4k a-a  ->  speaker volts out.
//
// The Vox feel comes from: no NFB (speaker sees a high-Z source, breakup is
// gradual), cathode bias whose bias point slides under drive (OS/065 note:
// "12.5V at 30 watts, quiescent 10V" -> touch compression / bloom), EL84s near
// class A into a low 4k Raa, and the bright 500p+100p coupling into a CF-driven
// treble-forward stack.
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include "princeton.hpp"

namespace ac30 {

inline constexpr double kPi = 3.14159265358979323846;

// EL84 Koren pentode (Koren-formulation SPICE library, fitted to the Mullard
// datasheet: MU=21.29 EX=1.240 KG1=401.7 KG2=4500 KP=111.04 KVB=17.9).
// The AC30 runs FOUR EL84s = two per push-pull side; two tubes in parallel
// double the current, i.e. KG1 and KG2 halve. (KG2's screen fit is coarse in
// that library — noted upstream.)
inline princeton::Pentode PEL84pair() {
    return {21.29, 1.240, 401.7 / 2.0, 4500.0 / 2.0, 111.04, 17.9};
}

// ---- Top Boost tone stack (OS/010, traced) ----------------------------------
// IN(CF cathode, ~612R source) -> 50p -> Treble 1M log (wiper = OUT)
// treble cold end = N1; N1 -22n- N2 -22n- N3; IN -100k- N2 (slope);
// Bass 1M log: element N1->gnd, wiper tied to N3; N3 -10k- gnd.
// Same double-buffered RT-safe MNA as the Dumble/Recti stacks.
class TBToneStack {
public:
    TBToneStack(double fs, double treble, double bass) { rebuild(fs, treble, bass); }
    void rebuild(double fs, double treble, double bass) {
        auto tap = [](double p) { p = std::clamp(p, 1e-3, 0.999); return p * p; };  // log pots
        double tw = tap(treble), bw = tap(bass);
        // nodes: 1 src | 2 stack in | 3 treble top | 4 wiper=OUT | 5 N1 | 6 N2 | 7 N3
        double G[7][7] = {}, C[7][7] = {};
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
        R(1, 2, 612.0);              // CF output impedance (ampbooks)
        Cap(2, 3, 50e-12);           // treble cap 50p
        R(3, 4, (1 - tw) * 1e6);     // Treble 1M upper (wiper 4 = output)
        R(4, 5, tw * 1e6);           // Treble 1M lower -> N1
        R(2, 6, 100e3);              // slope 100k -> N2
        Cap(5, 6, 0.022e-6);         // N1 - 22n - N2
        Cap(6, 7, 0.022e-6);         // N2 - 22n - N3
        R(5, 7, (1 - bw) * 1e6);     // Bass 1M upper (element N1->gnd, wiper=N3)
        R(7, 0, bw * 1e6);           // Bass 1M lower
        R(7, 0, 10e3);               // fixed 10k N3 -> gnd
        double k = 2.0 * fs, A1[N][N] = {}, A2[N][N] = {};
        for (int r = 0; r < 7; ++r)
            for (int c = 0; c < 7; ++c) { A1[r][c] = G[r][c] + k * C[r][c]; A2[r][c] = -G[r][c] + k * C[r][c]; }
        A1[0][7] = 1.0; A1[7][0] = 1.0; A2[7][0] = -1.0;
        double B[N] = {}; B[7] = 1.0;
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
        x_ = xn; u1_ = u; return x_[3];    // node 4 = treble wiper
    }
private:
    static constexpr int N = 8;
    std::array<std::array<std::array<double, N>, N>, 2> M_{};
    std::array<std::array<double, N>, 2> K_{};
    std::atomic<int> active_{0};
    std::array<double, N> x_{};
    double u1_ = 0.0;
};

class AC30 {
public:
    AC30(double fs, double gain = 0.5, double treble = 0.6, double cut = 0.3,
         double bass = 0.5)
        : fs_(fs),
          // V1: ECC83, Rp 220k, Rk 1.5k || 25u, plate labeled 170V (OS/065; rail
          // 275V per ampbooks — OS/065's own rail label is inconsistent, flagged).
          // Output coupling = C3 500p into VR2 500k: the Brilliant channel's
          // famously thin/bright coupling — real, part of the chime.
          v1_(princeton::T12AX7(), fs, 275.0, 220e3, 1.5e3, 25e-6, 500e-12, 500e3),
          // TB gain triode: 100k plate to the 290V TB-unit rail, 1.5k || 25u.
          // DC-coupled to the follower -> huge Cc into 1M is AC-equivalent.
          tb_(princeton::T12AX7(), fs, 290.0, 100e3, 1.5e3, 25e-6, 10e-6, 1e6),
          cf_(princeton::T12AX7(), fs, 290.0, 56e3, 10e-6, 1e6),
          stack_(fs, treble, bass),
          // PI: ECC83 LTP, 100k/100k plates (B derived: plates labeled 230V at
          // ~0.57 mA/side -> ~287V node), tail 47k+1.2k with the grids
          // bootstrapped ~55V up -> equivalent tail to a -53.5V rail.
          pi_(fs, 287.0, 100e3, 100e3, 48.2e3, -53.5, 0.15e-6, 220e3),
          // 4x EL84 cathode-biased into a 4k a-a OT (OS/065: "PRIMARY IMPED
          // ANODE TO ANODE 4K"), HT 320V, screens ~310V behind 100R. mHalf =
          // sqrt(4000/8)/2 (8-ohm side). Lm 40H: typical large-OT magnetizing
          // inductance (UNCONFIRMED for the Vox OT — sets only the deep-LF
          // corner). Idle 100 mA/side = 2 tubes x ~50 mA (10V across shared 50R
          // for the quad, per the OS/065 note).
          power_(fs, 4000.0, 40.0, 11.18, 100.0, PEL84pair(), 310.0, 320.0,
                 -11.0) {   // cathode-bias point ~-10V (10V across the shared 50R)
        setGain(gain);
        setCut(cut);
        // cathode-bias compression time constant: Rk*Ck = 50R * 250u = 12.5 ms
        double tau = 50.0 * 250e-6;
        envA_ = 1.0 - std::exp(-1.0 / (tau * fs));
    }

    // VR2 500k log volume with the TB-mod 100p bright cap across the top arm:
    // H(s) = a * (1 + s(1-a)RC) / (1 + s a(1-a)RC), R=500k, C=100p (OS/010).
    void setGain(double g) {
        double a = std::clamp(g, 1e-3, 0.999); a *= a;      // log taper
        gainA_ = a;
        double R = 500e3, C = 100e-12, k = 2.0 * fs_;
        double tz = (1.0 - a) * R * C, tp = a * (1.0 - a) * R * C;
        bB0_ = a * (1.0 + tz * k) / (1.0 + tp * k);
        bB1_ = a * (1.0 - tz * k) / (1.0 + tp * k);
        bA1_ = (1.0 - tp * k) / (1.0 + tp * k);
    }
    void setTone(double treble, double bass) { stack_.rebuild(fs_, treble, bass); }
    // CUT: VR3 250k log + C10 4.7n bridging the PI outputs. For the differential
    // signal that's a series-RC shunt against the ~38k plate source impedance:
    //   H(s) = (1 + sRC) / (1 + s(R+Rs)C),  R = (1-cut)^2 * 250k
    // cut=0 -> R=250k, shelf < 1.5 dB (flat); cut=1 -> R->0, ~890 Hz rolloff.
    void setCut(double c) {
        double p = std::clamp(c, 0.0, 0.999);
        double R = (1.0 - p) * (1.0 - p) * 250e3, Rs = 38e3, C = 4.7e-9;
        double k = 2.0 * fs_, tz = R * C, tp = (R + Rs) * C;
        cB0_ = (1.0 + tz * k) / (1.0 + tp * k);
        cB1_ = (1.0 - tz * k) / (1.0 + tp * k);
        cA1_ = (1.0 - tp * k) / (1.0 + tp * k);
    }
    void setMaster(double m) { master_ = 0.1 + 0.9 * std::clamp(m, 0.0, 1.0); }
    void setInScale(double s) { inScale_ = s; }

    double step(double x) {
        double y = v1_.step(x * inScale_);
        // VR2 volume + bright cap (bilinear 1z1p)
        double v = bB0_ * y + bB1_ * bX1_ - bA1_ * bY1_; bX1_ = y; bY1_ = v;
        y = tb_.step(v);
        y = cf_.step(y);
        y = stack_.step(y);
        y *= 1e6 / 1.22e6;              // R9 220k into the PI 1M grid leak
        double outT, outB;
        pi_.step(y * master_, outT, outB);
        // CUT (differential HF shunt) applied symmetrically to both drives
        double t = cB0_ * outT + cB1_ * cT1_ - cA1_ * cT2_; cT1_ = outT; cT2_ = t;
        double b = cB0_ * outB + cB1_ * cU1_ - cA1_ * cU2_; cU1_ = outB; cU2_ = b;
        // cathode-bias slide (OS/065: quiescent 10V -> 12.5V at 30W): the shared
        // 50R||250u cathode voltage follows output power with tau 12.5 ms; the
        // rising Vk backs the bias off -> compression / "bloom".
        double vaa = vaaPrev_;
        env_ += envA_ * (vaa * vaa / 4000.0 - env_);        // ~watts into 4k Raa
        double vkRise = 2.5 * std::min(env_ / 30.0, 2.0);   // 10V -> 12.5V @ 30W
        double vspk = power_.step(t, b, 320.0, 310.0, -vkRise);
        vaaPrev_ = std::clamp(vspk * 2.0 * 11.18, -1000.0, 1000.0);  // back to plate volts
        return vspk;
    }
    void processBlock(const double* in, double* out, int n) {
        for (int i = 0; i < n; ++i) out[i] = step(in[i]);
    }

private:
    double fs_, gainA_ = 0.25, inScale_ = 1.0, master_ = 1.0;
    princeton::TriodeStage v1_, tb_;
    princeton::CathodeFollower cf_;
    TBToneStack stack_;
    princeton::LTP pi_;
    princeton::PowerStage power_;
    double bB0_ = 0, bB1_ = 0, bA1_ = 0, bX1_ = 0, bY1_ = 0;   // VR2 + bright
    double cB0_ = 1, cB1_ = 0, cA1_ = 0, cT1_ = 0, cT2_ = 0, cU1_ = 0, cU2_ = 0;
    double env_ = 0.0, envA_ = 0.01, vaaPrev_ = 0.0;
};

}  // namespace ac30
