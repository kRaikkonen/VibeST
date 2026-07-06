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
#include "pedal_tone.hpp"      // PURE WHITE-BOX op-amp MNA tone circuits

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

// ---- Tone control (TS/SD Tone stage, MP Treble) ------------------------------
// The TS/SD active tone stage sweeps between "treble cut" (the signal through a
// low-pass around the tone corner) and "flat" as the pot turns up. Modelled as
// a crossfade between the low-passed and the unfiltered signal -> at tone 0 you
// hear the low-passed (dark) path, at tone 1 the flat path. No magic gain: the
// only numbers are the RC corner (fc) and the pot position (0..1). fc for the
// TS Tone leg ~723 Hz; the SD is similar.
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
        return (1.0 - tone_) * lp + tone_ * x;     // dark low-pass <-> flat
    }
private:
    double b0_, a1_, x1_ = 0, lp1_ = 0, tone_ = 0.5;
};

// ---- Klon Centaur: transparent germanium-feedback overdrive ------------------
// Schematic-first (Aion/ElectroSmash, 铁律一): IC1B non-inverting gain
// A = 1 + 422k/lower, lower = 15k+2k+(1-drive)*100k -> A 4.6..25.8x. Two 1N34A Ge
// diodes anti-parallel across the feedback clip only the EXCESS (A-1)*vin toward the
// ~0.35V germanium knee; vin passes straight through -> transparent. Active tilt Treble.
class KlonStage {
public:
    explicit KlonStage(double fs = 48000.0) : fs_(fs) { setGain(0.5); setTreble(0.5); }
    void setGain(double g) {
        g = std::clamp(g, 0.0, 0.999);
        lower_ = 15e3 + 2e3 + (1.0 - g) * 100e3;         // R11+R10+GAIN(100k) feedback lower leg
    }
    void setTreble(double t) {
        double wc = 2.0 * kPi * 1600.0, k = 2.0 * fs_;   // tilt pivot ~1.6 kHz
        b0_ = wc / (k + wc); a1_ = (wc - k) / (k + wc);
        tilt_ = (t - 0.5) * 2.0;                          // -1 dark .. +1 bright
    }
    // IC1B non-inverting stage, TWO 1N34A germanium diodes anti-parallel across R12.
    // Op-amp holds v- = vin; KCL at v-: vd/R12 + 2*Is*sinh(vd/nVT) = vin/lower, vd = vo-vin.
    // Newton for vd; vo = vin + vd -> clean vin passes through (transparent) + soft Ge clip.
    double step(double x) {
        double vin = x * 0.6;
        double drive = vin / lower_;
        double vd = vd_;
        for (int k = 0; k < 40; ++k) {
            double a = std::clamp(vd / NVT, -60.0, 60.0);
            double id = IS * (std::exp(a) - std::exp(-a));       // 2*Is*sinh (anti-parallel Ge)
            double dv = (vd / R12 + id - drive)
                        / (1.0 / R12 + IS * (std::exp(a) + std::exp(-a)) / NVT);
            vd -= dv;
            if (std::fabs(dv) < 1e-11) break;
        }
        vd_ = vd;
        double vo = vin + vd;
        double lp = b0_ * (vo + x1_) - a1_ * y1_;
        x1_ = vo; y1_ = lp;
        return lp + (1.0 + tilt_) * (vo - lp);            // active tilt treble
    }
private:
    static constexpr double R12 = 422e3, IS = 2e-6, NVT = 1.3 * 0.02585;   // 1N34A
    double fs_, lower_ = 57e3, tilt_ = 0.0, b0_ = 1.0, a1_ = 0.0, x1_ = 0.0, y1_ = 0.0, vd_ = 0.0;
};

// ---- pedal presets -----------------------------------------------------------

enum class Kind { OD1, SD1, TS808, MadRed, Klon, BlueBreaker };

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
        case Kind::MadRed:  // Mad Professor "Red" = BJFE Dyna Red Distortion (schematic-verified):
            // 2x red LED (Vf ~1.8V, corrected from 1.7) in the feedback loop; DIST 500k pot,
            // gain leg 3k. SIMPLIFIED: the real 2nd-stage 1N4001 shunt-to-gnd clipper + the
            // CA3130/2N5458-JFET topology are collapsed into this framework. Not NAM-validated.
            return {10e-9, 47e3, 1e3, 500e3, 3e3, 1e-6,
                    100e-12, 1e-18, 2.0, 0.02585, 1, 1};
        case Kind::BlueBreaker:  // Marshall Bluesbreaker — REAL values (AionFX Cerulean stock):
            // input C1 10n / R1 1M pulldown; gain leg R2 3k3 + C3 10n; feedback R3 4k7 +
            // Drive 100kB; C2 47pF fb cap; clip = 4x 1N914 (2-in-series each way = symmetric
            // soft, ~1.2V knee). Schematic-real; NOT NAM-validated (no Bluesbreaker capture).
            return {10e-9, 1e6, 4.7e3, 100e3, 3.3e3, 10e-9,
                    47e-12, 2.52e-9, 1.752, 0.02585, 2, 2};
        case Kind::Klon:                           // unused (Klon uses KlonStage); valid dummy
            return {10e-9, 51e3, 51e3, 500e3, 4.7e3, 51e-9,
                    51e-12, 2.52e-9, 1.752, 0.02585, 1, 1};
    }
    return {};
}

// A complete pedal: input buffer -> op-amp clipper -> tone -> level.
class Pedal {
public:
    // whitebox=true -> PURE WHITE-BOX tone (real op-amp active RC circuit, MNA);
    // false -> HYBRID Tilt crossfade (kept for reference / A-B).
    Pedal(Kind kind, double fs, double drive = 0.5, double level = 0.7,
          double tone = 0.5, int driveOS = 2, bool whitebox = false)
        : kind_(kind), drive_(driveParamsFor(kind), fs, drive, driveOS),
          tilt_(kind == Kind::MadRed ? 1500.0 : 723.0, fs), klon_(fs),
          hpOut_(100e3, 1e-6, fs), level_(level), fs_(fs), whitebox_(whitebox) {
        tilt_.set(tone);
        // OD-1 has a fixed 884 Hz filter instead of a tone knob
        od1Filt_ = (kind == Kind::OD1);
        if (kind_ == Kind::Klon) { klon_.setGain(drive); klon_.setTreble(tone); }
        else if (whitebox_) buildTone(tone);
        double wc = 2.0 * kPi * 14000.0, kk = 2.0 * fs;    // output-coupling top rolloff
        cpB_ = wc / (kk + wc); cpA_ = (wc - kk) / (kk + wc);
    }
    void setDrive(double d) {
        if (kind_ == Kind::Klon) klon_.setGain(d); else drive_.setDrive(d);
    }
    void setLevel(double l) { level_ = l; }
    void setTone(double t) {
        if (kind_ == Kind::Klon) klon_.setTreble(t);
        else if (whitebox_) { if (t != lastTone_) { buildTone(t); lastTone_ = t; } }
        else tilt_.set(t);
    }

    // output-impedance coupling: the level-pot output Z + cable / amp-input capacitance
    // softens the very top going into the amp — the "series coupling" black-box models
    // miss (they model pedal and amp separately). Gentle ~14 kHz one-pole.
    double coupled(double o) { cpY1_ = cpB_ * (o + cpX1_) - cpA_ * cpY1_; cpX1_ = o; return cpY1_; }
    double step(double x) {
        if (kind_ == Kind::Klon) return coupled(hpOut_.step(klon_.step(x) * level_));
        double y = drive_.step(x);
        if (kind_ == Kind::MadRed) y = shuntClip(y);        // Dyna Red 2nd-stage 1N4001 shunt
        y = whitebox_ ? toneNet_.step(y) : tilt_.step(y);   // WB: real MNA tone
        y *= level_;
        return coupled(hpOut_.step(y));
    }

private:
    void buildTone(double t) {
        if (kind_ == Kind::SD1 || kind_ == Kind::OD1)
            toneNet_ = pedal::sd1_tone(fs_, t);
        else if (kind_ == Kind::BlueBreaker)
            toneNet_ = pedal::bb_tone(fs_, t);          // white-box passive treble-cut
        else if (kind_ == Kind::MadRed)
            toneNet_ = pedal::dynared_tone(fs_, t);     // white-box Dyna Red "Treble"
        else                                            // TS808 -> TS tone
            toneNet_ = pedal::ts808_tone(fs_, t);
    }
    // Dyna Red 2nd clip stage: 2x 1N4001 anti-parallel to ground after ~1k series.
    // Solve (vout - v)/Rs = 2*Is*sinh(v/nVT) -> soft silicon shunt clip (~0.6V).
    static double shuntClip(double vout) {
        const double Rs = 1e3, Is = 1.4e-8, nvt = 1.8 * 0.02585;
        double v = vout;
        for (int k = 0; k < 25; ++k) {
            double a = std::clamp(v / nvt, -60.0, 60.0);
            double id = Is * (std::exp(a) - std::exp(-a));
            double dv = ((vout - v) / Rs - id) / (-1.0 / Rs - Is * (std::exp(a) + std::exp(-a)) / nvt);
            v -= dv;
            if (std::fabs(dv) < 1e-10) break;
        }
        return v;
    }
    Kind kind_;
    OpampDrive drive_;
    Tilt tilt_;                    // HYBRID crossfade tone (marked; kept for A-B)
    KlonStage klon_;               // Klon Centaur (Ge-feedback transparent overdrive)
    pedal::MnaTone toneNet_;       // PURE WHITE-BOX tone (op-amp active RC, MNA)
    od1::OnePoleHP hpOut_;
    double level_, fs_, lastTone_ = -1.0;    // guard: rebuild MnaTone only on change
    double cpB_ = 1.0, cpA_ = 0.0, cpX1_ = 0.0, cpY1_ = 0.0;  // output-coupling LP
    bool od1Filt_ = false, whitebox_ = false;
};

}  // namespace pedals
