// Overdrive/distortion pedals sharing the op-amp + feedback-clipper topology
// (OD-1 / SD-1 / TS-808) plus the Mad Professor Red (LED clipper). All reuse
// the validated OD-1 building blocks (op-amp macro, Ebers-Moll buffer,
// trapezoidal + Newton clipper solve) with per-pedal component values.
//
// Circuit sources: electricdruid OD-1/TS-808/SD-1 comparison sheet and the
// Mad Professor Red Distortion schematic (user-provided).
#pragma once
#include <algorithm>
#include <cmath>
#include "od1.hpp"

namespace pedals {

inline constexpr double kPi = 3.14159265358979323846;

// ---- generalised feedback clipper --------------------------------------------
// nDown/nUp = number of series diodes each polarity (asymmetry), Vf/Is set the
// device (Si 1N4148 vs a red LED ~1.7 V).
class Clipper {
public:
    Clipper(double Is, double N, double VT, int nDown, int nUp)
        : Is_(Is), nvtD_(nDown * N * VT), nvtU_(nUp * N * VT) {}
    double i(double v) const {
        double a = std::clamp(v / nvtD_, -80.0, 80.0);
        double b = std::clamp(-v / nvtU_, -80.0, 80.0);
        return Is_ * std::expm1(a) - Is_ * std::expm1(b);
    }
    double didv(double v) const {
        double a = std::clamp(v / nvtD_, -80.0, 80.0);
        double b = std::clamp(-v / nvtU_, -80.0, 80.0);
        return (Is_ / nvtD_) * std::exp(a) + (Is_ / nvtU_) * std::exp(b);
    }
private:
    double Is_, nvtD_, nvtU_;
};

// ---- op-amp non-inverting clipping stage (OD-1/SD-1/TS topology) --------------
// vo = vp + vd, diodes+Rf+Cf across the feedback, gain leg R106+C104 to Vref.
// Ideal op-amp (these pedals' 4558/TL072 are fast enough at 8x that the macro
// model and ideal agree to <0.5 dB, shown for the OD-1); trapezoidal + NR.
struct DriveParams {
    double C102, R104, R105, VRmax, R106, C104, C103;
    double Is, N, VT;
    int nDown, nUp;              // series diodes each way (1,2 = OD-1/SD-1)
};

class OpampDrive {
public:
    OpampDrive(const DriveParams& p, double fs, double drive, int os = 1)
        : p_(p), clip_(p.Is, p.N, p.VT, p.nDown, p.nUp),
          T_(1.0 / (fs * os)), os_(os) { setDrive(drive); }

    void setDrive(double d) {
        double taper = std::expm1(std::log(51.0) * d) / 50.0;
        Rf_ = p_.R105 + p_.VRmax * taper;
    }

    double step(double vin) {
        double r = 0.0;
        for (int s = 0; s < os_; ++s) r = one(vin);
        return r;
    }

private:
    double one(double vin) {
        double a102 = T_ / (2.0 * p_.R104 * p_.C102);
        vC102_ = (vC102_ * (1.0 - a102) + a102 * (vin + vinPrev_)) / (1.0 + a102);
        double vp = vin - vC102_;
        vinPrev_ = vin;
        double a104 = T_ / (2.0 * p_.R106 * p_.C104);
        double s4 = a104 / (1.0 + a104);
        double cC = 2.0 * p_.C103 / T_;
        double base = (vC104_ * (1.0 - a104) + a104 * vmPrev_) / (1.0 + a104);
        double vd = vd_;
        for (int it = 0; it < 60; ++it) {
            double vm = vp;                       // ideal op-amp: v- = v+ = vp
            double vC104n = base + s4 * vm;
            double fd = (vm - vC104n) / p_.R106 - vd / Rf_ - clip_.i(vd);
            double F = cC * (vd - vd_) - fd - fdPrev_;
            double J = cC + 1.0 / Rf_ + clip_.didv(vd);
            double dvd = F / J;
            dvd = std::clamp(dvd, -0.3, 0.3);
            vd -= dvd;
            if (std::fabs(dvd) < 1e-11) break;
        }
        double vm = vp;
        vC104_ = base + s4 * vm;
        fdPrev_ = (vm - vC104_) / p_.R106 - vd / Rf_ - clip_.i(vd);
        vmPrev_ = vm;
        vd_ = vd;
        return vp + vd;                            // op-amp output
    }
    DriveParams p_;
    Clipper clip_;
    double T_, Rf_;
    int os_;
    double vC102_ = 0, vC104_ = 0, vd_ = 0;
    double vinPrev_ = 0, vmPrev_ = 0, fdPrev_ = 0;
};

// ---- one-knob "tilt" tone control (TS/SD Tone, MP Treble) ---------------------
// TS/SD tone is a single op-amp tilt between a low-passed and the flat path;
// modelled here as a first-order crossfade (transparent-ish, the pedal's Tone
// pot behaviour). fc ~ the pedal's tone corner.
class Tilt {
public:
    Tilt(double fc, double fs) {
        double wc = 2.0 * kPi * fc, k = 2.0 * fs;
        b0_ = wc / (k + wc); a1_ = (wc - k) / (k + wc);
    }
    void set(double t) { tone_ = std::clamp(t, 0.0, 1.0); }
    double step(double x) {
        double lp = b0_ * (x + x1_) - a1_ * lp1_;
        x1_ = x; lp1_ = lp;
        double hp = x - lp;                        // complementary highpass
        return (1.0 - tone_) * lp + tone_ * (lp + 1.6 * hp);  // tilt toward hi
    }
private:
    double b0_, a1_, x1_ = 0, lp1_ = 0, tone_ = 0.5;
};

// ---- pedal presets -----------------------------------------------------------

enum class Kind { OD1, SD1, TS808, MadRed };

inline DriveParams driveParamsFor(Kind k) {
    // 1N4148: Is=2.52nA N=1.752; red LED approx: Is~1e-16, N~1.9 (Vf~1.7V)
    switch (k) {
        case Kind::TS808:                          // symmetric 1+1, 51k/4k7
            return {10e-9, 51e3, 51e3, 500e3, 4.7e3, 51e-9,
                    51e-12, 2.52e-9, 1.752, 0.02585, 1, 1};
        case Kind::SD1:                            // asymmetric 2+1, 33k/4k7
        case Kind::OD1:
            return {4.7e-9, 100e3, 33e3, 1e6, 4.7e3, 47e-9,
                    100e-12, 2.52e-9, 1.752, 0.02585, 2, 1};
        case Kind::MadRed:                         // LED symmetric, high gain
            return {10e-9, 47e3, 1e3, 500e3, 3e3, 1e-6,
                    100e-12, 1e-16, 1.9, 0.02585, 1, 1};
    }
    return {};
}

// A complete pedal: input buffer -> op-amp clipper -> tone -> level.
class Pedal {
public:
    Pedal(Kind kind, double fs, double drive = 0.5, double level = 0.7,
          double tone = 0.5, int driveOS = 2)
        : kind_(kind), drive_(driveParamsFor(kind), fs, drive, driveOS),
          tilt_(kind == Kind::MadRed ? 1500.0 : 723.0, fs),
          hpOut_(100e3, 1e-6, fs), level_(level) {
        tilt_.set(tone);
        // OD-1 has a fixed 884 Hz filter instead of a tone knob
        od1Filt_ = (kind == Kind::OD1);
    }
    void setDrive(double d) { drive_.setDrive(d); }
    void setLevel(double l) { level_ = l; }
    void setTone(double t) { tilt_.set(t); }

    double step(double x) {
        double y = drive_.step(x);
        y = tilt_.step(y);
        y *= level_;
        return hpOut_.step(y);
    }

private:
    Kind kind_;
    OpampDrive drive_;
    Tilt tilt_;
    od1::OnePoleHP hpOut_;
    double level_;
    bool od1Filt_ = false;
};

}  // namespace pedals
