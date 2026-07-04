// Fender Princeton Reverb AA1164 white-box engine â€” C++ port of
// proto/princeton.py (validated against Radau golden references).
// Mirrors the Python classes 1:1; JUCE-free. The spring-tank convolution is
// injected as a callback so the plugin can use zero-latency partitioned
// convolution while tests use a direct FIR.
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <functional>
#include <vector>

namespace princeton {

inline constexpr double kPi = 3.14159265358979323846;

// ---- Koren tube models -------------------------------------------------------

struct Triode {
    double MU, EX, KG1, KP, KVB;
    double ip(double vgk, double vpk) const {
        vpk = std::max(vpk, 0.0);
        double a = std::clamp(
            KP * (1.0 / MU + vgk / std::sqrt(KVB + vpk * vpk)), -50.0, 50.0);
        double e1 = (vpk / KP) * std::log1p(std::exp(a));
        return e1 <= 0.0 ? 0.0 : 2.0 * std::pow(e1, EX) / KG1;
    }
    // current + analytic partials w.r.t. plate and cathode-referenced grid
    void ipd(double vgk, double vpk, double& i, double& dvp,
             double& dvg) const {
        vpk = std::max(vpk, 1e-6);
        double s = std::sqrt(KVB + vpk * vpk);
        double z = std::clamp(KP * (1.0 / MU + vgk / s), -50.0, 50.0);
        double ez = std::exp(z);
        double l = std::log1p(ez);
        double e1 = (vpk / KP) * l;
        if (e1 <= 0.0) { i = dvp = dvg = 0.0; return; }
        double sig = ez / (1.0 + ez);
        double core = std::pow(e1, EX);
        i = 2.0 * core / KG1;
        double die1 = 2.0 * EX * core / e1 / KG1;
        double dzdvp = -KP * vgk * vpk / (s * s * s);
        double de1dvp = l / KP + (vpk / KP) * sig * dzdvp;
        double de1dvg = vpk * sig / s;
        dvp = die1 * de1dvp;
        dvg = die1 * de1dvg;
    }
};

struct Pentode {
    double MU, EX, KG1, KG2, KP, KVB;
    void currents(double vg1k, double vg2k, double vpk,
                  double& ip, double& ig2) const {
        vpk = std::max(vpk, 0.0);
        vg2k = std::max(vg2k, 1.0);
        double a = std::clamp(KP * (1.0 / MU + vg1k / vg2k), -50.0, 50.0);
        double e1 = (vg2k / KP) * std::log1p(std::exp(a));
        if (e1 <= 0.0) { ip = ig2 = 0.0; return; }
        double core = std::pow(e1, EX);
        ip = (core / KG1) * std::atan(vpk / KVB);
        ig2 = core / KG2;
    }
};

inline Triode T12AX7() { return {100.0, 1.4, 1060.0, 600.0, 300.0}; }
inline Triode T12AT7() { return {60.0, 1.35, 460.0, 300.0, 300.0}; }
inline Pentode P6V6()  { return {12.7, 1.31, 583.0, 4500.0, 41.5, 12.7}; }
// EL34 (Marshall power tube): Koren fit, more power/gm and a harder knee than
// the 6V6 -> the aggressive Marshall power-stage breakup. KG1 scaled to hit
// the datasheet Vp=Vg2=250 Vg1=-13.5 -> ~100 mA point.
inline Pentode PEL34() { return {11.0, 1.35, 280.0, 4200.0, 60.0, 24.0}; }

// ---- generic triode gain stage -------------------------------------------------

class TriodeStage {
public:
    TriodeStage(Triode tube, double fs, double B, double Rp, double Rk,
                double Ck = 0.0, double Cc = 0.0, double RL = 0.0)
        : tube_(tube), T_(1.0 / fs), B_(B), Rp_(Rp), Rk_(Rk), Ck_(Ck),
          Cc_(Cc), RL_(RL) {
        double vp = 0.6 * B, vk = 1.0;
        for (int it = 0; it < 200; ++it) {
            double r1, r2, r1p, r2p, r1k, r2k;
            dcRes(vp, vk, r1, r2);
            const double e = 1e-4;
            dcRes(vp + e, vk, r1p, r2p);
            dcRes(vp, vk + e, r1k, r2k);
            double J11 = (r1p - r1) / e, J12 = (r1k - r1) / e;
            double J21 = (r2p - r2) / e, J22 = (r2k - r2) / e;
            double det = J11 * J22 - J12 * J21;
            if (det == 0.0) break;
            double dvp = (r1 * J22 - r2 * J12) / det;
            double dvk = (J11 * r2 - J21 * r1) / det;
            vp -= std::clamp(dvp, -30.0, 30.0);
            vk -= std::clamp(dvk, -2.0, 2.0);
            if (std::fabs(dvp) < 1e-10 && std::fabs(dvk) < 1e-10) break;
        }
        vpDc_ = vp; vkDc_ = vk;
        ipDc_ = tube_.ip(0.0 - vk, vp - vk);
        vck_ = vk; ikPrev_ = ipDc_; q_ = vp;
        vp_ = vp; vk_ = vk;
    }

    double nfb = 0.0;                    // extra cathode reference (V3B)
    double ikPrev() const { return ikPrev_; }

    double step(double vg, double B = -1.0) {
        if (B < 0.0) B = B_;
        double vp = vp_, vk = vk_;
#ifdef PA_FAST_JACOBIAN
        // realtime fast path: analytic Jacobian + relaxed tolerance.
        // Same physics; NR path (and thus branch picks in ambiguous hard-clip
        // states) may differ from the bit-matched validation build.
        const double ccp = Cc_ > 0.0 ? 2.0 * Cc_ / T_ : 0.0;
        const double diCdvp = Cc_ > 0.0 ? ccp / (1.0 + ccp * RL_) : 0.0;
        const double akf = Ck_ > 0.0 ? T_ / (2.0 * Rk_ * Ck_) : 0.0;
        const double dvckdip = Ck_ > 0.0 ? akf * Rk_ / (1.0 + akf) : Rk_;
        for (int it = 0; it < 40; ++it) {
            double r1, r2, ip, iC;
            residuals(vp, vk, vg, B, r1, r2, ip, iC);
            double i0, dvpk, dvgk;
            tube_.ipd(vg - vk, vp - vk, i0, dvpk, dvgk);
            double dipdvp = dvpk, dipdvk = -dvpk - dvgk;
            double J11 = -1.0 / Rp_ - dipdvp - diCdvp;
            double J12 = -dipdvk;
            double J21 = -dvckdip * dipdvp;
            double J22 = 1.0 - dvckdip * dipdvk;
            double det = J11 * J22 - J12 * J21;
            if (det == 0.0) break;
            double dvp = (r1 * J22 - r2 * J12) / det;
            double dvk = (J11 * r2 - J21 * r1) / det;
            vp -= std::clamp(dvp, -25.0, 25.0);
            vk -= std::clamp(dvk, -2.0, 2.0);
            if (std::fabs(dvp) < 1e-7 && std::fabs(dvk) < 1e-7) break;
        }
#else
        // FD Newton, arithmetic identical to the Python reference: keeps the
        // NR path (and thus branch choices under hard clipping) bit-matched.
        for (int it = 0; it < 40; ++it) {
            double r1, r2, ip, iC;
            residuals(vp, vk, vg, B, r1, r2, ip, iC);
            const double e = 1e-5;
            double r1p, r2p, r1k, r2k, d1, d2;
            residuals(vp + e, vk, vg, B, r1p, r2p, d1, d2);
            residuals(vp, vk + e, vg, B, r1k, r2k, d1, d2);
            double J11 = (r1p - r1) / e, J12 = (r1k - r1) / e;
            double J21 = (r2p - r2) / e, J22 = (r2k - r2) / e;
            double det = J11 * J22 - J12 * J21;
            if (det == 0.0) break;
            double dvp = (r1 * J22 - r2 * J12) / det;
            double dvk = (J11 * r2 - J21 * r1) / det;
            vp -= std::clamp(dvp, -25.0, 25.0);
            vk -= std::clamp(dvk, -2.0, 2.0);
            if (std::fabs(dvp) < 1e-9 && std::fabs(dvk) < 1e-9) break;
        }
#endif
        // physical bounds: a real triode plate cannot swing beyond its supply
        // nor below ~0, and the cathode sits low. Clamping to generous
        // physical limits stops a diverged Newton (huge grid drive on a
        // vol-drag transient) from running vp -> 1e10 -> pow() -> inf -> NaN,
        // which used to latch the whole amp permanently silent.
        vp = std::clamp(vp, -20.0, B + 100.0);
        vk = std::clamp(vk, -20.0, 150.0);
        double r1, r2, ip, iC;
        residuals(vp, vk, vg, B, r1, r2, ip, iC);
        if (Ck_ > 0.0) {
            double ak = T_ / (2.0 * Rk_ * Ck_);
            vck_ = (vck_ * (1.0 - ak) + ak * Rk_ * (ip + ikPrev_)) / (1.0 + ak);
        }
        ikPrev_ = ip;
        double vo2;
        if (Cc_ > 0.0) {
            vo2 = RL_ * iC;
            q_ = vp - vo2;
            iCPrev_ = iC;
        } else {
            vo2 = vp - vpDc_;
        }
        vp_ = vp; vk_ = vk;
        iSupply_ = (B - vp) / Rp_;
        return vo2;
    }

    double vpDc_ = 0, vkDc_ = 0, ipDc_ = 0, iSupply_ = 0;

private:
    void dcRes(double vp, double vk, double& r1, double& r2) const {
        double ip = tube_.ip(0.0 - vk, vp - vk);
        r1 = (B_ - vp) / Rp_ - ip;
        r2 = vk - Rk_ * ip;
    }
    void residuals(double vp, double vk, double vg, double B,
                   double& r1, double& r2, double& ip, double& iC) const {
        ip = tube_.ip(vg - vk, vp - vk);
        if (Cc_ > 0.0) {
            double c = 2.0 * Cc_ / T_;
            iC = (c * (vp - q_) - iCPrev_) / (1.0 + c * RL_);
        } else {
            iC = 0.0;
        }
        r1 = (B - vp) / Rp_ - ip - iC;
        if (Ck_ > 0.0) {
            double ak = T_ / (2.0 * Rk_ * Ck_);
            double vck = (vck_ * (1.0 - ak) + ak * Rk_ * (ip + ikPrev_))
                         / (1.0 + ak);
            r2 = vk - vck - nfb;
        } else {
            r2 = vk - Rk_ * ip - nfb;
        }
    }
    Triode tube_;
    double T_, B_, Rp_, Rk_, Ck_, Cc_, RL_;
    double vck_ = 0, ikPrev_ = 0, q_ = 0, iCPrev_ = 0, vp_ = 0, vk_ = 0;
};

// ---- cathodyne phase inverter ---------------------------------------------------

class Cathodyne {
public:
    Cathodyne(double fs, double B, double Rp = 56e3, double RkTop = 1e3,
              double RkTail = 56e3, double Cc = 0.1e-6, double RL = 220e3)
        : tube_(T12AX7()), T_(1.0 / fs), B_(B), Rp_(Rp),
          Rk_(RkTop + RkTail), Cc_(Cc), RL_(RL) {
        double vp = 0.7 * B, vk = 40.0;
        for (int it = 0; it < 300; ++it) {
            double ip = tube_.ip(0.0 - vk, vp - vk);
            double r1 = (B - vp) / Rp_ - ip;
            double r2 = vk - Rk_ * ip;
            const double e = 1e-4;
            double ipp = tube_.ip(0.0 - vk, vp + e - vk);
            double ipk = tube_.ip(0.0 - (vk + e), vp - (vk + e));
            double J11 = -1.0 / Rp_ - (ipp - ip) / e;
            double J12 = -(ipk - ip) / e;
            double J21 = -Rk_ * (ipp - ip) / e;
            double J22 = 1.0 - Rk_ * (ipk - ip) / e;
            double det = J11 * J22 - J12 * J21;
            double dvp = (r1 * J22 - r2 * J12) / det;
            double dvk = (J11 * r2 - J21 * r1) / det;
            vp -= std::clamp(dvp, -30.0, 30.0);
            vk -= std::clamp(dvk, -10.0, 10.0);
            if (std::fabs(dvp) < 1e-10 && std::fabs(dvk) < 1e-10) break;
        }
        vp_ = vp; vk_ = vk;
        qTop_ = vp; qBot_ = vk;
    }

    void step(double vg, double& outT, double& outB, double B = -1.0) {
        if (B < 0.0) B = B_;
        double vp = vp_, vk = vk_;
#ifdef PA_FAST_JACOBIAN
        const double cc = 2.0 * Cc_ / T_;
        const double diCdv = cc / (1.0 + cc * RL_);
        for (int it = 0; it < 40; ++it) {
            double r1, r2, ip, iCt, iCb;
            residuals(vp, vk, vg, B, r1, r2, ip, iCt, iCb);
            double i0, dvpk, dvgk;
            tube_.ipd(vg - vk, vp - vk, i0, dvpk, dvgk);
            double dipdvp = dvpk, dipdvk = -dvpk - dvgk;
            double J11 = -1.0 / Rp_ - dipdvp - diCdv;
            double J12 = -dipdvk;
            double J21 = dipdvp;
            double J22 = dipdvk - 1.0 / Rk_ - diCdv;
            double det = J11 * J22 - J12 * J21;
            if (det == 0.0) break;
            double dvp = (r1 * J22 - r2 * J12) / det;
            double dvk = (J11 * r2 - J21 * r1) / det;
            vp -= std::clamp(dvp, -25.0, 25.0);
            vk -= std::clamp(dvk, -10.0, 10.0);
            if (std::fabs(dvp) < 1e-7 && std::fabs(dvk) < 1e-7) break;
        }
#else
        for (int it = 0; it < 40; ++it) {
            double r1, r2, ip, iCt, iCb;
            residuals(vp, vk, vg, B, r1, r2, ip, iCt, iCb);
            const double e = 1e-5;
            double r1p, r2p, r1k, r2k, d1, d2, d3;
            residuals(vp + e, vk, vg, B, r1p, r2p, d1, d2, d3);
            residuals(vp, vk + e, vg, B, r1k, r2k, d1, d2, d3);
            double J11 = (r1p - r1) / e, J12 = (r1k - r1) / e;
            double J21 = (r2p - r2) / e, J22 = (r2k - r2) / e;
            double det = J11 * J22 - J12 * J21;
            if (det == 0.0) break;
            double dvp = (r1 * J22 - r2 * J12) / det;
            double dvk = (J11 * r2 - J21 * r1) / det;
            vp -= std::clamp(dvp, -25.0, 25.0);
            vk -= std::clamp(dvk, -10.0, 10.0);
            if (std::fabs(dvp) < 1e-9 && std::fabs(dvk) < 1e-9) break;
        }
#endif
        vp = std::clamp(vp, -20.0, B + 100.0);   // physical bounds (anti-NaN)
        vk = std::clamp(vk, -20.0, 200.0);
        double r1, r2, ip, iCt, iCb;
        residuals(vp, vk, vg, B, r1, r2, ip, iCt, iCb);
        outT = std::clamp(RL_ * iCt, -400.0, 400.0);
        outB = std::clamp(RL_ * iCb, -400.0, 400.0);
        qTop_ = vp - outT; iCtPrev_ = iCt;
        qBot_ = vk - outB; iCbPrev_ = iCb;
        vp_ = vp; vk_ = vk;
    }

private:
    void residuals(double vp, double vk, double vg, double B,
                   double& r1, double& r2, double& ip,
                   double& iCt, double& iCb) const {
        ip = tube_.ip(vg - vk, vp - vk);
        double c = 2.0 * Cc_ / T_;
        iCt = (c * (vp - qTop_) - iCtPrev_) / (1.0 + c * RL_);
        iCb = (c * (vk - qBot_) - iCbPrev_) / (1.0 + c * RL_);
        r1 = (B - vp) / Rp_ - ip - iCt;
        r2 = ip - vk / Rk_ - iCb;
    }
    Triode tube_;
    double T_, B_, Rp_, Rk_, Cc_, RL_;
    double vp_ = 0, vk_ = 0, qTop_ = 0, qBot_ = 0;
    double iCtPrev_ = 0, iCbPrev_ = 0;
};

// ---- cathode follower (Marshall V2B tone-stack buffer) -------------------------
// Triode with the output taken from the cathode: gain ~0.9, low output Z, and
// an ASYMMETRIC soft compression -- on a big positive swing the grid pulls the
// cathode up easily, but a big negative swing runs the tube toward cutoff and
// the cathode can't follow, so transients get gently squashed on one side.
// That one-sided squash is a documented part of Marshall crunch feel.
// 1-unknown Newton on the cathode voltage vk: iP(vg-vk, B-vk) = vk/Rk.
class CathodeFollower {
public:
    CathodeFollower(Triode tube, double fs, double B, double Rk,
                    double Cc = 0.022e-6, double RL = 470e3)
        : tube_(tube), T_(1.0 / fs), B_(B), Rk_(Rk), Cc_(Cc), RL_(RL) {
        double vk = 1.0;
        for (int it = 0; it < 200; ++it) {
            double ip = tube_.ip(0.0 - vk, B_ - vk);
            double f = ip - vk / Rk_;
            double e = 1e-4;
            double ipe = tube_.ip(0.0 - (vk + e), B_ - (vk + e));
            double df = (ipe - ip) / e - 1.0 / Rk_;
            vk -= f / df;
        }
        vkDc_ = vk;
        vk_ = vk;
        q_ = vk;                    // coupling cap: output DC = 0
    }
    double step(double vg) {
        double vk = vk_;
        for (int it = 0; it < 30; ++it) {
            double ip = tube_.ip(vg - vk, B_ - vk);
            double f = ip - vk / Rk_;
            double e = 1e-5;
            double ipe = tube_.ip(vg - (vk + e), B_ - (vk + e));
            double df = (ipe - ip) / e - 1.0 / Rk_;
            double dvk = f / df;
            vk -= std::clamp(dvk, -20.0, 20.0);
            if (std::fabs(dvk) < 1e-9) break;
        }
        vk = std::clamp(vk, -20.0, B_ + 50.0);     // physical bound (anti-NaN)
        vk_ = vk;
        // output coupling cap (trapezoidal) -> AC-coupled cathode signal
        double c = 2.0 * Cc_ / T_;
        double iC = (c * (vk - q_) - iCPrev_) / (1.0 + c * RL_);
        double vo = RL_ * iC;
        q_ = vk - vo;
        iCPrev_ = iC;
        return vo;
    }
private:
    Triode tube_;
    double T_, B_, Rk_, Cc_, RL_;
    double vkDc_ = 0, vk_ = 0, q_ = 0, iCPrev_ = 0;
};

// ---- 6V6 push-pull + OT (Lm, Cw, leakage) + speaker load ------------------------

class PowerStage {
public:
    explicit PowerStage(double fs, double Raa = 8000.0, double Lm = 15.0,
                        double mHalf = 15.8, double idleMa = 30.0,
                        Pentode tube = P6V6(), double vbSup = 400.0,
                        double vpSup = 410.0)
        : tube_(tube), T_(1.0 / fs), Lm_(Lm), m_(mHalf),
          vbSup_(vbSup), vpSup_(vpSup) {
        (void)Raa;
        const double Re = 6.4, Lvc = 0.8e-3, Rres = 20.0, Lces = 30e-3;
        const double Cmes = 1.0 / (std::pow(2 * kPi * 95.0, 2) * Lces);
        Re_ = Re; Lvc_ = Lvc; Rres_ = Rres; Lces_ = Lces; Cmes_ = Cmes;
        // A = [[2Lvc/T+Re, 1, 0], [-1, 2Cmes/T+1/Rres, 1], [0, -1, 2Lces/T]]
        double A[3][3] = {{2 * Lvc / T_ + Re, 1.0, 0.0},
                          {-1.0, 2 * Cmes / T_ + 1.0 / Rres, 1.0},
                          {0.0, -1.0, 2 * Lces / T_}};
        invert3(A, Ainv_);
        geq_ = Ainv_[0][0];
        // bias calibration to idle current
        double vg = -34.0;
        for (int it = 0; it < 200; ++it) {
            double ip, ig, ip2, ig2;
            tube_.currents(vg, vbSup_, vpSup_, ip, ig);
            double f = ip - idleMa * 1e-3;
            tube_.currents(vg + 1e-4, vbSup_, vpSup_, ip2, ig2);
            vg -= f / ((ip2 - ip) / 1e-4);
        }
        vbias_ = vg;
        iPlates_ = 2 * idleMa * 1e-3;
        // OT winding capacitance + leakage one-pole (20 kHz)
        Cw_ = 500e-12;
        double wl = 2 * kPi * 20e3;
        lkB_ = wl * T_ / (2.0 + wl * T_);
        lkA_ = (2.0 - wl * T_) / (2.0 + wl * T_);
    }

    double vbias() const { return vbias_; }
    double iPlates() const { return iPlates_; }
    double iScreens() const { return iScreens_; }

    double step(double vg1ac, double vg2ac, double vA, double vB,
                double biasMod = 0.0) {
        double bias = vbias_ + biasMod;
        double g1 = gclamp(bias + vg1ac);
        double g2 = gclamp(bias + vg2ac);
        // speaker history
        double c1 = (2 * Lvc_ / T_) * spk_[0]
                    + (vsPrev_ - Re_ * spk_[0] - spk_[1]);
        double c2 = (2 * Cmes_ / T_) * spk_[1]
                    + (spk_[0] - spk_[1] / Rres_ - spk_[2]);
        double c3 = (2 * Lces_ / T_) * spk_[2] + spk_[1];
        double ih0 = Ainv_[0][0] * c1 + Ainv_[0][1] * c2 + Ainv_[0][2] * c3;
        double vaaOld = vaa_;
        double vaa = vaaOld, lo = -2.2 * vA, hi = 2.2 * vA;
#ifdef PA_FAST_JACOBIAN
        // E1/core depend only on grid & screen voltages: once per sample
        auto coreOf = [&](double g) {
            double a = std::clamp(tube_.KP * (1.0 / tube_.MU + g / vB),
                                  -50.0, 50.0);
            double e1 = (vB / tube_.KP) * std::log1p(std::exp(a));
            return e1 <= 0.0 ? 0.0 : std::pow(e1, tube_.EX);
        };
        const double core1 = coreOf(g1), core2 = coreOf(g2);
        const double gLin = T_ / (2 * Lm_) + geq_ / (4 * m_ * m_)
                            + 2 * Cw_ / T_;
        auto Fres = [&](double v, double& dF) {
            double vp1 = std::max(vA - 0.5 * v, 0.5);
            double vp2 = std::max(vA + 0.5 * v, 0.5);
            double i1_ = (core1 / tube_.KG1) * std::atan(vp1 / tube_.KVB);
            double i2_ = (core2 / tube_.KG1) * std::atan(vp2 / tube_.KVB);
            double da1 = (core1 / tube_.KG1)
                / (tube_.KVB * (1.0 + (vp1 / tube_.KVB) * (vp1 / tube_.KVB)));
            double da2 = (core2 / tube_.KG1)
                / (tube_.KVB * (1.0 + (vp2 / tube_.KVB) * (vp2 / tube_.KVB)));
            dF = -0.5 * da1 - 0.5 * da2 - gLin;
            double iLn = iL_ + (T_ / (2 * Lm_)) * (v + vaaOld);
            double is = geq_ * v / (2 * m_) + ih0;
            double iCw = (2 * Cw_ / T_) * (v - vaaOld) - iCwPrev_;
            return (i1_ - i2_) - iLn - is / (2 * m_) - iCw;
        };
        for (int it = 0; it < 60; ++it) {
            double dF;
            double F = Fres(vaa, dF);
            if (F > 0.0) lo = std::max(lo, vaa); else hi = std::min(hi, vaa);
            double vNew = dF < 0.0 ? vaa - F / dF : 0.5 * (lo + hi);
            if (!(lo < vNew && vNew < hi)) vNew = 0.5 * (lo + hi);
            if (std::fabs(vNew - vaa) < 1e-7) { vaa = vNew; break; }
            vaa = vNew;
        }
        double i1 = (core1 / tube_.KG1)
                    * std::atan(std::max(vA - 0.5 * vaa, 0.5) / tube_.KVB);
        double i2 = (core2 / tube_.KG1)
                    * std::atan(std::max(vA + 0.5 * vaa, 0.5) / tube_.KVB);
        double s1 = core1 / tube_.KG2, s2 = core2 / tube_.KG2;
#else
        auto Fres = [&](double v) {
            double i1, s1, i2, s2;
            tube_.currents(g1, vB, std::max(vA - 0.5 * v, 0.5), i1, s1);
            tube_.currents(g2, vB, std::max(vA + 0.5 * v, 0.5), i2, s2);
            double iLn = iL_ + (T_ / (2 * Lm_)) * (v + vaaOld);
            double is = geq_ * v / (2 * m_) + ih0;
            double iCw = (2 * Cw_ / T_) * (v - vaaOld) - iCwPrev_;
            return (i1 - i2) - iLn - is / (2 * m_) - iCw;
        };
        for (int it = 0; it < 60; ++it) {
            double F = Fres(vaa);
            if (F > 0.0) lo = std::max(lo, vaa); else hi = std::min(hi, vaa);
            const double e = 1e-3;
            double dF = (Fres(vaa + e) - F) / e;
            double vNew = dF < 0.0 ? vaa - F / dF : 0.5 * (lo + hi);
            if (!(lo < vNew && vNew < hi)) vNew = 0.5 * (lo + hi);
            if (std::fabs(vNew - vaa) < 1e-8) { vaa = vNew; break; }
            vaa = vNew;
        }
        double i1, s1, i2, s2;
        tube_.currents(g1, vB, std::max(vA - 0.5 * vaa, 0.5), i1, s1);
        tube_.currents(g2, vB, std::max(vA + 0.5 * vaa, 0.5), i2, s2);
#endif
        iL_ += (T_ / (2 * Lm_)) * (vaa + vaaOld);
        iCwPrev_ = (2 * Cw_ / T_) * (vaa - vaaOld) - iCwPrev_;
        double vs = vaa / (2 * m_);
        double b1 = vs + c1;
        spk_[0] = Ainv_[0][0] * b1 + Ainv_[0][1] * c2 + Ainv_[0][2] * c3;
        spk_[1] = Ainv_[1][0] * b1 + Ainv_[1][1] * c2 + Ainv_[1][2] * c3;
        spk_[2] = Ainv_[2][0] * b1 + Ainv_[2][1] * c2 + Ainv_[2][2] * c3;
        vsPrev_ = vs;
        vaa_ = vaa;
        iPlates_ = i1 + i2;
        iScreens_ = s1 + s2;
        double vOut = lkB_ * (vs + lkX1_) + lkA_ * lkY1_;
        lkX1_ = vs; lkY1_ = vOut;
        return vOut;
    }

private:
    static double gclamp(double v) {
        return v - 0.15 * std::log1p(std::exp((v - 0.5) / 0.15));
    }
    static void invert3(const double A[3][3], double out[3][3]) {
        double det = A[0][0]*(A[1][1]*A[2][2]-A[1][2]*A[2][1])
                   - A[0][1]*(A[1][0]*A[2][2]-A[1][2]*A[2][0])
                   + A[0][2]*(A[1][0]*A[2][1]-A[1][1]*A[2][0]);
        out[0][0]=(A[1][1]*A[2][2]-A[1][2]*A[2][1])/det;
        out[0][1]=(A[0][2]*A[2][1]-A[0][1]*A[2][2])/det;
        out[0][2]=(A[0][1]*A[1][2]-A[0][2]*A[1][1])/det;
        out[1][0]=(A[1][2]*A[2][0]-A[1][0]*A[2][2])/det;
        out[1][1]=(A[0][0]*A[2][2]-A[0][2]*A[2][0])/det;
        out[1][2]=(A[0][2]*A[1][0]-A[0][0]*A[1][2])/det;
        out[2][0]=(A[1][0]*A[2][1]-A[1][1]*A[2][0])/det;
        out[2][1]=(A[0][1]*A[2][0]-A[0][0]*A[2][1])/det;
        out[2][2]=(A[0][0]*A[1][1]-A[0][1]*A[1][0])/det;
    }
    Pentode tube_;
    double T_, Lm_, m_;
    double Re_, Lvc_, Rres_, Lces_, Cmes_;
    double Ainv_[3][3], geq_;
    double vbias_, Cw_;
    double spk_[3] = {0, 0, 0}, vsPrev_ = 0;
    double iL_ = 0, iCwPrev_ = 0, vaa_ = 0;
    double iPlates_ = 0, iScreens_ = 0;
    double lkB_, lkA_, lkX1_ = 0, lkY1_ = 0;
    double vbSup_ = 400.0, vpSup_ = 410.0;
};

// ---- PSU with sag ---------------------------------------------------------------

class PSU {
public:
    explicit PSU(double fs, double mainsHz = 60.0)
        : T_(1.0 / fs), w_(2 * kPi * mainsHz) {}
    void step(double iA, double iB, double& vA, double& vB,
              double iCload = 0.002, double iDload = 0.0025) {
        double vr = Vpk_ * std::fabs(std::sin(w_ * t_));
        t_ += T_;
        double irect = std::max(0.0, (vr - Vth_ - vA_) / Rs_);
        double iAB = (vA_ - vB_) / 1e3;
        double iBC = (vB_ - vC_) / 18e3;
        double iCD = (vC_ - vD_) / 18e3;
        vA_ += T_ / C_ * (irect - iAB - iA);
        vB_ += T_ / C_ * (iAB - iBC - iB);
        vC_ += T_ / C_ * (iBC - iCD - iCload);
        vD_ += T_ / C_ * (iCD - iDload);
        vA = vA_; vB = vB_;
    }
private:
    double T_, w_, t_ = 0.0;
    double Vpk_ = 480.0, Vth_ = 20.0, Rs_ = 180.0, C_ = 20e-6;
    double vA_ = 420.0, vB_ = 410.0, vC_ = 320.0, vD_ = 240.0;
};

// ---- linear helpers --------------------------------------------------------------

class OnePoleHPc {                    // trapezoid form used by reverb send
public:
    OnePoleHPc(double a) : a_(a) {}
    double step(double x) {
        s_ = (s_ * (1.0 - a_) + a_ * (x + x1_)) / (1.0 + a_);
        double y = x - s_;
        x1_ = x;
        return y;
    }
private:
    double a_, s_ = 0, x1_ = 0;
};

class ReverbMixer {
public:
    ReverbMixer(double fs, double Rdry = 3.3e6, double Cbright = 10e-12,
                double Rwet = 470e3, double Rleak = 220e3)
        : g1_(1 / Rdry), g2_(1 / Rwet), g3_(1 / Rleak), Cb_(Cbright),
          k_(2.0 * fs) {}
    double step(double vdry, double vwet) {
        double gs = g1_ + g2_ + g3_;
        double num = g1_ * (vdry + x1_) + k_ * Cb_ * (vdry - x1_)
                     + g2_ * (vwet + w1_) - (gs - k_ * Cb_) * y1_;
        double y = num / (gs + k_ * Cb_);
        x1_ = vdry; w1_ = vwet; y1_ = y;
        return y;
    }
private:
    double g1_, g2_, g3_, Cb_, k_;
    double x1_ = 0, y1_ = 0, w1_ = 0;
};

// Fender tone stack: 8-node MNA compiled to x[n] = M x[n-1] + K (u[n]+u[n-1])
// (matrices computed offline; small dense mat-vec per sample)
class ToneStack {
public:
    ToneStack(double fs, double treble, double bass, double volume,
              double Rsrc = 38e3) {
        rebuild(fs, treble, bass, volume, Rsrc);
    }
    void rebuild(double fs, double treble, double bass, double volume,
                 double Rsrc = 38e3) {
        // mirrors mna.fender_tone_stack: nodes 1..8 (0-indexed 0..7), row 8 =
        // source current. Audio taper approximated as p^2.
        const double RT = 250e3, RB = 250e3, RV = 1e6;
        auto tap = [](double p) {
            p = std::clamp(p, 1e-3, 0.999);
            return p * p;
        };
        double tw = tap(treble), bw = tap(bass), vw = tap(volume);
        double G[8][8] = {}, C[8][8] = {};
        auto stampR = [&](int a, int b, double ohms) {
            double g = 1.0 / std::max(ohms, 1.0);
            if (a > 0) G[a - 1][a - 1] += g;
            if (b > 0) G[b - 1][b - 1] += g;
            if (a > 0 && b > 0) { G[a - 1][b - 1] -= g; G[b - 1][a - 1] -= g; }
        };
        auto stampC = [&](int a, int b, double f) {
            if (a > 0) C[a - 1][a - 1] += f;
            if (b > 0) C[b - 1][b - 1] += f;
            if (a > 0 && b > 0) { C[a - 1][b - 1] -= f; C[b - 1][a - 1] -= f; }
        };
        stampR(1, 2, Rsrc);
        stampC(2, 3, 250e-12);
        stampR(3, 6, (1.0 - tw) * RT);
        stampR(6, 5, tw * RT);
        stampR(2, 4, 100e3);
        stampC(4, 8, 0.1e-6);
        stampR(8, 5, bw * RB);
        stampC(4, 5, 0.047e-6);
        stampR(5, 0, 6800.0);
        stampR(6, 7, (1.0 - vw) * RV);
        stampR(7, 0, vw * RV);
        // A1 x[n] = A2 x[n-1] + B (u[n]+u[n-1]);  x = [v1..v8, iSrc]
        double k = 2.0 * fs;
        double A1[N][N] = {}, A2[N][N] = {};
        for (int r = 0; r < 8; ++r)
            for (int c = 0; c < 8; ++c) {
                A1[r][c] = G[r][c] + k * C[r][c];
                A2[r][c] = -G[r][c] + k * C[r][c];
            }
        A1[0][8] = 1.0;                      // source row: node 1
        A1[8][0] = 1.0;
        A2[8][0] = -1.0;
        double B[N] = {};
        B[8] = 1.0;
        // solve A1 * [M | K] = [A2 | B] via Gauss-Jordan
        double aug[N][2 * N + 1];
        for (int r = 0; r < N; ++r) {
            for (int c = 0; c < N; ++c) {
                aug[r][c] = A1[r][c];
                aug[r][N + c] = A2[r][c];
            }
            aug[r][2 * N] = B[r];
        }
        for (int col = 0; col < N; ++col) {
            int piv = col;
            for (int r = col + 1; r < N; ++r)
                if (std::fabs(aug[r][col]) > std::fabs(aug[piv][col])) piv = r;
            for (int c = 0; c <= 2 * N; ++c) std::swap(aug[col][c], aug[piv][c]);
            double d = aug[col][col];
            for (int c = 0; c <= 2 * N; ++c) aug[col][c] /= d;
            for (int r = 0; r < N; ++r) {
                if (r == col) continue;
                double f = aug[r][col];
                if (f == 0.0) continue;
                for (int c = 0; c <= 2 * N; ++c) aug[r][c] -= f * aug[col][c];
            }
        }
        // write into the INACTIVE buffer, then atomically flip. This makes
        // rebuild() safe to call from the UI thread without the audio lock:
        // the audio thread always reads a complete, consistent coeff set.
        // (Rebuilding under the audio lock while dragging a pot starved the
        // callback -> "mush" then ASIO overflow.)
        int inactive = 1 - active_.load(std::memory_order_relaxed);
        for (int r = 0; r < N; ++r) {
            for (int c = 0; c < N; ++c) M_[inactive][r][c] = aug[r][N + c];
            K_[inactive][r] = aug[r][2 * N];
        }
        active_.store(inactive, std::memory_order_release);
    }

    double step(double u) {
        int a = active_.load(std::memory_order_acquire);
        const auto& M = M_[a];
        const auto& K = K_[a];
        std::array<double, N> xn{};
        for (int r = 0; r < N; ++r) {
            double acc = K[r] * (u + u1_);
            for (int c = 0; c < N; ++c) acc += M[r][c] * x_[c];
            xn[r] = acc;
        }
        x_ = xn;
        u1_ = u;
        return x_[6];                        // node 7 (vol wiper), 0-indexed
    }
private:
    static constexpr int N = 9;              // 8 nodes + source current
    std::array<std::array<std::array<double, N>, N>, 2> M_{};
    std::array<std::array<double, N>, 2> K_{};
    std::atomic<int> active_{0};
    std::array<double, N> x_{};
    double u1_ = 0.0;
};

// ---- the amp ----------------------------------------------------------------------

struct AmpControls {
    double volume = 0.5, treble = 0.5, bass = 0.5;
    double reverb = 0.3, tremSpeed = 0.5, tremIntensity = 0.0;
};

class Amp {
public:
    using TankConv = std::function<void(const double*, double*, int)>;

    Amp(double fs, const AmpControls& c, TankConv tank)
        : fs_(fs), c_(c), tank_(std::move(tank)),
          v1a_(T12AX7(), fs, 240.0, 100e3, 1500.0, 25e-6),
          stack_(fs, c.treble, c.bass, c.volume),
          v1b_(T12AX7(), fs, 240.0, 100e3, 1500.0, 25e-6, 0.02e-6, 1e6),
          revHp_(1.0 / (2.0 * fs * 500e-12 * 1e6)),
          v2_(T12AT7(), fs, 410.0, 10e3, 2200.0, 25e-6),
          v3a_(T12AX7(), fs, 240.0, 220e3, 1500.0, 25e-6, 0.003e-6, 100e3),
          mixer_(fs),
          v3b_(T12AX7(), fs, 240.0, 100e3, 1500.0, 25e-6, 0.02e-6, 1e6),
          pi_(fs, 240.0),
          power_(fs),
          psu_(fs) {
        tremHz_ = 3.0 + 4.0 * c.tremSpeed;
        tremDepth_ = 4.0 * c.tremIntensity;
        // NFB feedback low-pass. The discrete one-sample loop delay plus the
        // OT/speaker model resonance (~8 kHz) make the closed loop oscillate
        // there unless the feedback is rolled off well below it (measured:
        // stable at both 48 k and 96 k for fc <= 300 Hz). 250 Hz gives margin.
        // NB: real amps keep NFB across the band; this low cutoff is a
        // stability compromise of the explicit (delayed) loop — an implicit
        // loop solve is the proper fix (tracked). Tone above 250 Hz runs
        // closer to open-loop (brighter), acceptable for a blackface voicing.
        setFbCutoff(250.0);
    }

    // live control changes (call between blocks)
    void setTone(double treble, double bass, double volume) {
        // rebuild (a 9x9 solve) only when a tone pot actually moved — called
        // on every control change, and rebuilding under the audio lock on
        // each event starves the callback (ASIO overflow while dragging).
        if (treble == c_.treble && bass == c_.bass && volume == c_.volume)
            return;
        c_.treble = treble; c_.bass = bass; c_.volume = volume;
        stack_.rebuild(fs_, treble, bass, volume);
    }
    void setReverb(double r) { c_.reverb = r; }
    void setTankReturn(double k) { tankRetK_ = k; }   // calibration hook
    void setNfb(double scale) { nfbScale_ = scale; }  // 0 = open loop
    void setFbCutoff(double fc) { fbA_ = 2.0*kPi*fc / (fs_ + 2.0*kPi*fc); }
    void setTremolo(double speed, double intensity) {
        c_.tremSpeed = speed; c_.tremIntensity = intensity;
        tremHz_ = 3.0 + 4.0 * speed;
        tremDepth_ = 4.0 * intensity;
    }
    const AmpControls& controls() const { return c_; }

    double tap[7] = {0};   // debug stage-peak taps (v1a,stack,v1b,vg,v3b,PI,spk)
    double dbgVA = 0, dbgVB = 0;   // last PSU rail voltages (debug)

    // process one block; scratch buffers sized to n by caller
    void processBlock(const double* in, double* out, int n,
                      double* v1bBuf, double* drvBuf, double* wetBuf) {
        for (int i = 0; i < n; ++i) {
            double a = v1a_.step(in[i]);
            double s = stack_.step(a);
            v1bBuf[i] = v1b_.step(s);
            tap[0] = std::max(tap[0], std::fabs(a));
            tap[1] = std::max(tap[1], std::fabs(s));
            tap[2] = std::max(tap[2], std::fabs(v1bBuf[i]));
        }
        for (int i = 0; i < n; ++i)
            drvBuf[i] = v2_.step(revHp_.step(v1bBuf[i])) * tankSendK_;
        tank_(drvBuf, wetBuf, n);                 // spring pan convolution
        for (int i = 0; i < n; ++i)
            wetBuf[i] = v3a_.step(wetBuf[i] * tankRetK_) * c_.reverb;
        for (int i = 0; i < n; ++i) {
            double vg = mixer_.step(v1bBuf[i], wetBuf[i]);
            tap[3] = std::max(tap[3], std::fabs(vg));
            const double Rt = 47.0, Rn = 2700.0;
            fbLp_ += fbA_ * (vspk_ - fbLp_);        // HF-stabilizing LP
            double vfb = fbLp_ * nfbScale_;
            v3b_.nfb = (Rt * v3b_.ikPrev() + Rt * vfb / Rn) / (1.0 + Rt / Rn);
            double v3bOut = v3b_.step(vg);
            double outT, outB;
            pi_.step(v3bOut, outT, outB);
            tap[4] = std::max(tap[4], std::fabs(v3bOut));
            tap[5] = std::max(tap[5], std::fabs(outT));
            double vA, vB;
            psu_.step(power_.iPlates(), power_.iScreens(), vA, vB);
            dbgVA = vA; dbgVB = vB;
            double trem = tremDepth_ > 0.0
                ? tremDepth_ * std::sin(2 * kPi * tremHz_ * tremT_) : 0.0;
            tremT_ += 1.0 / fs_;
            vspk2_ = vspk_;
            vspk_ = power_.step(outT, outB, vA, vB, trem);
            tap[6] = std::max(tap[6], std::fabs(vspk_));
            out[i] = vspk_;
        }
    }

private:
    double fs_;
    AmpControls c_;
    TankConv tank_;
    TriodeStage v1a_;
    ToneStack stack_;
    TriodeStage v1b_;
    OnePoleHPc revHp_;
    TriodeStage v2_;
    TriodeStage v3a_;
    ReverbMixer mixer_;
    TriodeStage v3b_;
    Cathodyne pi_;
    PowerStage power_;
    PSU psu_;
    double tremHz_, tremDepth_, tremT_ = 0.0;
    double vspk_ = 0.0, vspk2_ = 0.0, fbLp_ = 0.0, fbA_ = 0.1, nfbScale_ = 1.0;
    // tankRetK: PRODUCT VOICING, chosen by ear (user preference): the hot
    // return drives the V3A recovery stage into saturation and the wet path
    // dominates the mix — technically "wrong" vs the physically-calibrated
    // 4.0e-4 (grid search, test/calib_rev.cpp), but it is the sound the
    // player wants. Revisit after measuring a real pan's send/return levels.
    double tankSendK_ = 8.0e-3, tankRetK_ = 1.0e-4;   // wet/dry ~0.6 at pot 0.5
};

}  // namespace princeton

