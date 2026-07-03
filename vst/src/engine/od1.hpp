// Boss OD-1 (1977) white-box circuit engine — C++ port of proto/od1sim.py.
// Every class mirrors the validated Python implementation 1:1 (same states,
// same trapezoidal discretization, same Newton-Raphson layout) so the C++
// output can be compared sample-by-sample against the Python golden dumps.
#pragma once
#include <algorithm>
#include <cmath>

namespace od1 {

inline constexpr double kPi = 3.14159265358979323846;

struct Params {
    // S1 input buffer
    double R101 = 1e3, C101 = 47e-9, R102 = 470e3, R103 = 10e3;
    // S2 drive stage
    double C102 = 4.7e-9, R104 = 100e3, R105 = 33e3, VR101_max = 1e6;
    double R106 = 4.7e3, C104 = 47e-9, C103 = 100e-12;
    // 1N4148
    double diode_Is = 2.52e-9, diode_N = 1.752, VT = 0.02585;
    // S3 filter stage
    double R107 = 10e3, R108 = 10e3, C105 = 18e-9;
    // S4 level
    double C108 = 1e-6, R110 = 4.7e3, VR103 = 10e3;
    // S5 output buffer
    double C109 = 100e-9, R112 = 470e3, R113 = 10e3;
    double C110 = 1e-6, R115 = 100e3;
    // BJT
    double bjt_Is = 1e-14, bjt_beta = 300.0;

    double Rf(double drive) const {                    // log-taper pot
        double taper = std::expm1(std::log(51.0) * drive) / 50.0;
        return R105 + VR101_max * taper;
    }
};

struct OpampMacro {
    double A0, GBW, SR, Vdz, Vsat;
    double wu() const { return 2.0 * kPi * GBW; }
    double wp() const { return wu() / A0; }
    double out(double vo) const {
        return Vdz <= 0.0 ? vo : vo - Vdz * std::tanh(vo / Vdz);
    }
    double dout(double vo) const {
        if (Vdz <= 0.0) return 1.0;
        double th = std::tanh(vo / Vdz);
        return th * th;
    }
};

inline OpampMacro TL072()   { return {2e5, 3e6, 13e6, 0.0,  3.2}; }
inline OpampMacro RC3403A() { return {2e5, 1e6, 0.5e6, 0.05, 3.2}; }
inline OpampMacro JRC4558() { return {1e5, 3e6, 1.7e6, 0.0,  3.2}; }

class ClipperDiodes {
public:
    explicit ClipperDiodes(const Params& p)
        : Is(p.diode_Is), nvtPair(2.0 * p.diode_N * p.VT),
          nvtSingle(p.diode_N * p.VT) {}
    double i(double v) const {
        double a = std::clamp(v / nvtPair, -80.0, 80.0);
        double b = std::clamp(-v / nvtSingle, -80.0, 80.0);
        return Is * std::expm1(a) - Is * std::expm1(b);
    }
    double didv(double v) const {
        double a = std::clamp(v / nvtPair, -80.0, 80.0);
        double b = std::clamp(-v / nvtSingle, -80.0, 80.0);
        return (Is / nvtPair) * std::exp(a) + (Is / nvtSingle) * std::exp(b);
    }
private:
    double Is, nvtPair, nvtSingle;
};

class OnePoleHP {                       // bilinear C-into-R highpass
public:
    OnePoleHP(double R, double C, double fs) {
        double wc = 1.0 / (R * C), k = 2.0 * fs;
        b0 = k / (k + wc);
        a1 = (wc - k) / (k + wc);
    }
    double step(double x) {
        double y = b0 * (x - x1) - a1 * y1;
        x1 = x; y1 = y;
        return y;
    }
private:
    double b0, a1, x1 = 0.0, y1 = 0.0;
};

// S2: 4 states (vC102, vC104, vd, vo), 2-unknown NR per sample
class DriveStage {
public:
    DriveStage(const Params& p, double fs, double drive, OpampMacro oa)
        : p_(p), d_(p), oa_(oa), T_(1.0 / fs), Rf_(p.Rf(drive)) {}

    void setDrive(double drive) { Rf_ = p_.Rf(drive); }

    struct SolveState {
        double vd, vo, vC104, vmPrev, fdPrev, foPrev;
    };

    double step(double vin) {
        double a102 = T_ / (2.0 * p_.R104 * p_.C102);
        vC102 = (vC102 * (1.0 - a102) + a102 * (vin + vinPrev)) / (1.0 + a102);
        double vp = vin - vC102;
        vinPrev = vin;

        SolveState st{vd, vo, vC104, vmPrev, fdPrev, foPrev};
        SolveState nw;
        bool conv = nrSolve(vp, T_, st, nw);
        if (!conv) {                 // substep 4x through the fast traverse
            nw = st;
            for (int k = 0; k < 4; ++k) {
                SolveState tmp;
                nrSolve(vp, T_ / 4.0, nw, tmp);
                nw = tmp;
            }
        }
        vd = nw.vd; vo = nw.vo; vC104 = nw.vC104;
        vmPrev = nw.vmPrev; fdPrev = nw.fdPrev; foPrev = nw.foPrev;
        return oa_.out(vo);
    }

private:
    // one trapezoidal step of size T; damped Newton with residual-norm
    // backtracking — mirrors od1sim.DriveStageRT._nr_solve exactly
    bool nrSolve(double vp, double T, const SolveState& s0,
                 SolveState& out) const {
        const double a104 = T / (2.0 * p_.R106 * p_.C104);
        const double s4 = a104 / (1.0 + a104);
        const double cC = 2.0 * p_.C103 / T;
        const double base = (s0.vC104 * (1.0 - a104) + a104 * s0.vmPrev)
                            / (1.0 + a104);
        auto residuals = [&](double vd_, double vo_, double& F1, double& F2,
                             double& th, double& nrm) {
            double vout = oa_.out(vo_);
            double vm = vout - vd_;
            double vC104n = base + s4 * vm;
            double fd = (vm - vC104n) / p_.R106 - vd_ / Rf_ - d_.i(vd_);
            F1 = cC * (vd_ - s0.vd) - fd - s0.fdPrev;
            th = std::tanh(oa_.wu() * (vp - vm) / oa_.SR);
            double fo = oa_.SR * th - oa_.wp() * vo_;
            F2 = (2.0 / T) * (vo_ - s0.vo) - fo - s0.foPrev;
            nrm = (F1 / cC) * (F1 / cC) + (F2 * T / 2.0) * (F2 * T / 2.0);
        };
        double vd_ = s0.vd, vo_ = s0.vo;
        bool conv = false;
        for (int it = 0; it < 80; ++it) {
            double F1, F2, th, n0;
            residuals(vd_, vo_, F1, F2, th, n0);
            if (n0 < 1e-24) { conv = true; break; }
            double Xp = oa_.dout(vo_);
            double dfdm = (1.0 - s4) / p_.R106;
            double J11 = cC + dfdm + 1.0 / Rf_ + d_.didv(vd_);
            double J12 = -dfdm * Xp;
            double sech2 = oa_.wu() * (1.0 - th * th);
            double J21 = -sech2;
            double J22 = 2.0 / T + oa_.wp() + sech2 * Xp;
            double det = J11 * J22 - J12 * J21;
            double dvd = std::clamp((F1 * J22 - F2 * J12) / det, -0.3, 0.3);
            double dvo = std::clamp((J11 * F2 - J21 * F1) / det, -2.0, 2.0);
            double alpha = 1.0;
            double vdN = vd_, voN = vo_, n1 = n0;
            for (int ls = 0; ls < 7; ++ls) {
                double cd = vd_ - alpha * dvd;
                double co = std::min(std::max(vo_ - alpha * dvo, -oa_.Vsat),
                                     oa_.Vsat);
                double f1c, f2c, thc, nc;
                residuals(cd, co, f1c, f2c, thc, nc);
                if (nc < n0) { vdN = cd; voN = co; n1 = nc; break; }
                alpha *= 0.5;
            }
            if (n1 >= n0) {
                vdN = vd_ - alpha * dvd;
                voN = std::min(std::max(vo_ - alpha * dvo, -oa_.Vsat),
                               oa_.Vsat);
            }
            double stepSz = std::max(std::fabs(vdN - vd_),
                                     std::fabs(voN - vo_));
            vd_ = vdN; vo_ = voN;
            if (stepSz < 1e-11) {
                double f1f, f2f, thf, nf;
                residuals(vd_, vo_, f1f, f2f, thf, nf);
                conv = nf < 1e-18;
                break;
            }
        }
        double vout = oa_.out(vo_);
        double vm = vout - vd_;
        double vC104n = base + s4 * vm;
        out.vd = vd_;
        out.vo = vo_;
        out.vC104 = vC104n;
        out.vmPrev = vm;
        out.fdPrev = (vm - vC104n) / p_.R106 - vd_ / Rf_ - d_.i(vd_);
        out.foPrev = oa_.SR * std::tanh(oa_.wu() * (vp - vm) / oa_.SR)
                     - oa_.wp() * vo_;
        return conv;
    }

    Params p_;
    ClipperDiodes d_;
    OpampMacro oa_;
    double T_, Rf_;
    double vC102 = 0, vC104 = 0, vd = 0, vo = 0;
    double vinPrev = 0, vmPrev = 0, fdPrev = 0, foPrev = 0;
};

// S3: inverting one-pole LP with op-amp macro (2-unknown NR)
class FilterStage {
public:
    FilterStage(const Params& p, double fs, OpampMacro oa)
        : p_(p), oa_(oa), T_(1.0 / fs) {}

    double step(double xn) {
        double cC = 2.0 * p_.C105 / T_;
        double gsum = 1.0 / p_.R108 + 1.0 / p_.R107;
        double vc = vC105, von = vo;
        for (int it = 0; it < 30; ++it) {
            double vout = oa_.out(von), Xp = oa_.dout(von);
            double vm = vout - vc;
            double fc = -(xn - vm) / p_.R108 - (vout - vm) / p_.R107;
            double F1 = cC * (vc - vC105) - fc - fcPrev;
            double arg = -oa_.wu() * vm / oa_.SR;
            double th = std::tanh(arg);
            double fo = oa_.SR * th - oa_.wp() * von;
            double F2 = (2.0 / T_) * (von - vo) - fo - foPrev;
            double J11 = cC + gsum;
            double J12 = -Xp / p_.R108;
            double sech2 = oa_.wu() * (1.0 - th * th);
            double J21 = -sech2;
            double J22 = 2.0 / T_ + oa_.wp() + sech2 * Xp;
            double det = J11 * J22 - J12 * J21;
            double dvc = (F1 * J22 - F2 * J12) / det;
            double dvo = (J11 * F2 - J21 * F1) / det;
            vc -= dvc;
            von -= dvo;
            von = std::clamp(von, -oa_.Vsat, oa_.Vsat);
            if (std::fabs(dvc) < 1e-12 && std::fabs(dvo) < 1e-12) break;
        }
        double vout = oa_.out(von);
        double vm = vout - vc;
        fcPrev = -(xn - vm) / p_.R108 - (vout - vm) / p_.R107;
        foPrev = oa_.SR * std::tanh(-oa_.wu() * vm / oa_.SR) - oa_.wp() * von;
        vC105 = vc; vo = von;
        return vout;
    }

private:
    Params p_;
    OpampMacro oa_;
    double T_;
    double vC105 = 0, vo = 0, fcPrev = 0, foPrev = 0;
};

// S1/S5: emitter follower, Ebers-Moll, scalar NR on u = vBE
class EmitterFollower {
public:
    EmitterFollower(const Params& p, double fs, double Rs, double Cc,
                    double Rb, double Re)
        : T_(1.0 / fs), Rs_(Rs), Cc_(Cc), Rb_(Rb), Re_(Re),
          Is_(p.bjt_Is), beta_(p.bjt_beta), VT_(p.VT), VBB_(4.5) {
        // DC operating point (no current through Cc)
        double uu = 0.6;
        for (int it = 0; it < 200; ++it) {
            double iC = ic(uu);
            double ve = Re_ * (1.0 + 1.0 / beta_) * iC;
            double vb = ve + uu;
            double f = (VBB_ - vb) / Rb_ - iC / beta_;
            double diC = dic(uu);
            double dvb = Re_ * (1.0 + 1.0 / beta_) * diC + 1.0;
            double df = -dvb / Rb_ - diC / beta_;
            double du = f / df;
            uu -= std::clamp(du, -0.05, 0.05);
            if (std::fabs(du) < 1e-14) break;
        }
        u_ = uu;
        veDc_ = Re_ * (1.0 + 1.0 / beta_) * ic(uu);
        vC101_ = 0.0 - (veDc_ + uu);
    }

    double step(double vin) {
        double kap = T_ / (2.0 * Rs_ * Cc_);
        double c0 = vC101_ + (T_ / 2.0) * dvcPrev_;
        double uu = u_, lo = -2.0, hi = 0.9;
        for (int it = 0; it < 60; ++it) {
            double iC = ic(uu);
            double ve = Re_ * (1.0 + 1.0 / beta_) * iC;
            double vb = ve + uu;
            double vC101n = (c0 + kap * (vin - vb)) / (1.0 + kap);
            double h = (vin - vb - vC101n) / Rs_ - (vb - VBB_) / Rb_
                       - iC / beta_;
            if (h > 0.0) lo = std::max(lo, uu); else hi = std::min(hi, uu);
            double diC = dic(uu);
            double dvb = Re_ * (1.0 + 1.0 / beta_) * diC + 1.0;
            double dh = (-dvb * (1.0 - kap / (1.0 + kap))) / Rs_
                        - dvb / Rb_ - diC / beta_;
            double un = uu - h / dh;
            if (!(lo < un && un < hi)) un = 0.5 * (lo + hi);
            if (std::fabs(un - uu) < 1e-13) { uu = un; break; }
            uu = un;
        }
        double iC = ic(uu);
        double ve = Re_ * (1.0 + 1.0 / beta_) * iC;
        double vb = ve + uu;
        double vC101n = (c0 + kap * (vin - vb)) / (1.0 + kap);
        dvcPrev_ = (vin - vb - vC101n) / (Rs_ * Cc_);
        vC101_ = vC101n;
        u_ = uu;
        return ve - veDc_;
    }

private:
    double ic(double u) const {
        return Is_ * std::expm1(std::clamp(u / VT_, -200.0, 200.0));
    }
    double dic(double u) const {
        return (Is_ / VT_) * std::exp(std::clamp(u / VT_, -200.0, 200.0));
    }
    double T_, Rs_, Cc_, Rb_, Re_, Is_, beta_, VT_, VBB_;
    double u_, veDc_, vC101_, dvcPrev_ = 0.0;
};

// Full pedal
class Pedal {
public:
    Pedal(double fs, double drive = 0.5, double level = 1.0,
          OpampMacro oa = RC3403A(), Params p = Params{})
        : bufIn(p, fs, p.R101, p.C101, p.R102, p.R103),
          driveStage(p, fs, drive, oa),
          filt(p, fs, oa),
          hpLvl(p.R110 + p.VR103, p.C108, fs),
          bufOut(p, fs, 1.0, p.C109, p.R112, p.R113),
          hpOut(p.R115, p.C110, fs),
          kLevel(level * p.VR103 / (p.R110 + p.VR103)) {}

    double step(double x) {
        double y = bufIn.step(x);
        y = driveStage.step(y);
        y = filt.step(y);
        y = hpLvl.step(y) * kLevel;
        y = bufOut.step(y);
        return hpOut.step(y);
    }

    void setDrive(double d) { driveStage.setDrive(d); }
    void setLevel(double l) {
        Params p;
        kLevel = l * p.VR103 / (p.R110 + p.VR103);
    }

    EmitterFollower bufIn;
    DriveStage driveStage;
    FilterStage filt;
    OnePoleHP hpLvl;
    EmitterFollower bufOut;
    OnePoleHP hpOut;
    double kLevel;
};

}  // namespace od1
