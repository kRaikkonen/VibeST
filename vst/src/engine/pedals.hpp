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

// ---- Klon Centaur: full real topology (Aion Refractor parts list + Chowdhury
// KlonCentaur repo, values double-sourced; 铁律一) --------------------------------
//   in (IC1A buffer, unity)
//     ├─ DIRTY: IC1B non-inv gain (R10 2k + gang1(1-g)*100k + R11 15k||C7 82n lower
//     │         leg, R12 422k||C8 390p feedback; +13dB..+40dB) -> R13 1k ->
//     │         2x1N34A Ge SHUNT clipper (Newton) -> C10 1u -> R16 47k -> IC2A
//     ├─ FF1 (clean bass restore): R7 1.5k -> C16 1u shunt -> R19 15k -> IC2A
//     └─ FF2 (gain-faded clean mids): R5 5.1k||C4 68n -> R8 1.5k / C6+R9 shunts ->
//               gang2 fade -> R17 27k||(C12 27n+R18 12k) -> C11 2.2n+R15 22k -> J
//   IC2A inverting sum, feedback R20 392k||C13 820p (495 Hz lid)
//   -> IC2B tone (R22/R24 100k, R21 1.8k, R23 4.7k, Treble 10kB, C14 3.9n; exact MNA)
//   -> C15 4.7u -> 560R -> Output pot 10kB (linear)
// FF2 + the C10/R16 join at node J are solved EXACTLY as two passive MNA networks
// (superposition: the network is linear; each source solved with the other shorted;
// the IC2A virtual ground is a 1R sense node whose voltage reads the sum current).
class KlonStage {
public:
    explicit KlonStage(double fs = 48000.0) : fs_(fs) {
        // IC2A feedback lid: Zf = R20||C13 applied to the summed current
        // (transimpedance: volts out per amp in -> the R20 factor is the gain).
        double w = 1.0 / (R20 * C13), k = 2.0 * fs_;
        fA_ = (w - k) / (k + w); fB_ = R20 * w / (k + w);
        // output high-pass C15 into (560R + 10k pot)
        double wo = 1.0 / (C15 * (560.0 + 10e3));
        oB_ = k / (k + wo); oA_ = (wo - k) / (k + wo);
        setGain(0.5); setTreble(0.5); setLevel(0.5);
    }
    void setGain(double g) {
        g = std::clamp(g, 0.0, 0.999);
        if (g == g_ && built_) return;      // guard: rebuild only on a real change
        g_ = g;
        // IC1B lower leg: i = vin/Zl, Zl = Ra + R11||C7 (1-zero-1-pole, bilinear)
        double Ra = 2e3 + (1.0 - g_) * 100e3;              // R10 + gang1
        double t7 = R11 * C7, k = 2.0 * fs_;
        double d = (Ra + R11) + Ra * t7 * k;
        lB0_ = (1.0 + t7 * k) / d; lB1_ = (1.0 - t7 * k) / d;
        lA1_ = ((Ra + R11) - Ra * t7 * k) / d;
        // feedback Zf = R12||C8 one-pole on the leg current
        double w8 = 1.0 / (R12 * C8);
        gA_ = (w8 - k) / (k + w8); gB_ = R12 * w8 / (k + w8);
        rebuild();
    }
    void setTreble(double t) {
        t = std::clamp(t, 0.0, 0.999);
        if (t == treble_ && built_) return;
        treble_ = t; rebuild();
    }
    void setLevel(double l) { level_ = std::clamp(l, 0.0, 1.0); }
    double step(double x) {
        // IC1B: leg current (1z1p) -> Zf one-pole -> vo = vin + vf (non-inverting)
        double i = lB0_ * x + lB1_ * lX1_ - lA1_ * lY1_; lX1_ = x; lY1_ = i;
        double vf = gB_ * (i + gX1_) - gA_ * gY1_; gX1_ = i; gY1_ = vf;
        double v1b = x + vf;
        // R13 1k -> 2x1N34A Ge anti-parallel shunt (Newton, warm start)
        double v = vc_;
        for (int it = 0; it < 40; ++it) {
            double a = std::clamp(v / NVT, -60.0, 60.0);
            double id = IS * (std::exp(a) - std::exp(-a));
            double dv = ((v - v1b) / R13 + id)
                        / (1.0 / R13 + IS * (std::exp(a) + std::exp(-a)) / NVT);
            v -= dv;
            if (std::fabs(dv) < 1e-12) break;
        }
        vc_ = v;
        // IC2A: exact passive-network currents into the virtual ground (1R sense)
        double iD = netD_.step(v);        // dirty: C10 -> J (FF2 loading) -> R16
        double iC = netC_.step(x);        // clean: FF1 + FF2 -> J -> R16
        double isum = iD + iC;
        double vs = fB_ * (isum + fX1_) - fA_ * fY1_;      // x R20||C13 (inverting)
        fX1_ = isum; fY1_ = vs; vs = -vs;
        double vt = tone_.step(vs);                         // IC2B (exact op-amp MNA)
        double hp = oB_ * (vt - oX1_) - oA_ * oY1_;        // C15 high-pass
        oX1_ = vt; oY1_ = hp;
        return hp * (10e3 / 10.56e3) * level_;             // 560R + 10kB divider
    }
private:
    void rebuild() {
        built_ = true;
        // FF2 network + the J junction, solved exactly (passive MNA, 1R sense node).
        // nodes: 1 src | 2 after R5||C4 | 3 C6/R9 midpoint | 4 gang2 tap |
        //        5 after R17||(C12+R18) | 6 C12/R18 midpoint | 7 after C11 |
        //        8 = J | 9 = virtual-ground sense (1R)
        auto ff2 = [&](pedal::MnaTone& n) {
            n.R(1, 2, 5.1e3); n.Cap(1, 2, 68e-9);            // R5 || C4
            n.R(2, 0, 1.5e3);                                 // R8
            n.Cap(2, 3, 390e-9); n.R(3, 0, 1e3);              // C6 + R9
            n.R(2, 4, std::max(g_, 1e-3) * 100e3);            // gang2 series (fades clean OUT as g rises)
            n.R(4, 0, std::max(1.0 - g_, 1e-3) * 100e3);      // gang2 shunt
            n.R(4, 5, 27e3);                                  // R17
            n.Cap(4, 6, 27e-9); n.R(6, 5, 12e3);              // C12 + R18
            n.Cap(5, 7, 2.2e-9); n.R(7, 8, 22e3);             // C11 + R15 -> J
        };
        {   // clean net: src drives FF1 + FF2; dirty source (via C10) shorted at J
            pedal::MnaTone n(10, fs_); n.Vsrc(1);
            ff2(n);
            n.R(1, 10, 1.5e3); n.Cap(10, 0, 1e-6);            // FF1: R7 -> C16 shunt
            n.R(10, 9, 15e3);                                 // R19 -> virtual ground
            n.Cap(8, 0, 1e-6);                                // C10 to shorted dirty src
            n.R(8, 9, 47e3);                                  // R16 J -> virtual ground
            n.R(9, 0, 1.0);                                   // 1R sense: V(9) = i_sum
            n.compile(9); netC_ = n;
        }
        {   // dirty net: src -> C10 -> J; FF2+FF1 hang off with their source shorted
            pedal::MnaTone n(10, fs_); n.Vsrc(1);
            // FF2 with node1 = shorted clean source -> its elements go to ground:
            n.R(0, 2, 5.1e3); n.Cap(0, 2, 68e-9);
            n.R(2, 0, 1.5e3);
            n.Cap(2, 3, 390e-9); n.R(3, 0, 1e3);
            n.R(2, 4, std::max(g_, 1e-3) * 100e3);
            n.R(4, 0, std::max(1.0 - g_, 1e-3) * 100e3);
            n.R(4, 5, 27e3);
            n.Cap(4, 6, 27e-9); n.R(6, 5, 12e3);
            n.Cap(5, 7, 2.2e-9); n.R(7, 8, 22e3);
            n.Cap(1, 8, 1e-6);                                // C10 from the clipper
            n.R(8, 9, 47e3);                                  // R16
            n.R(0, 10, 1.5e3); n.Cap(10, 0, 1e-6); n.R(10, 9, 15e3);   // FF1, src shorted
            n.R(9, 0, 1.0);                                   // 1R sense
            n.compile(9); netD_ = n;
        }
        {   // IC2B tone: exact MNA with the op-amp primitive.
            // nodes: 1 src | 2 inv in | 3 pot top | 5 wiper | 6 pot bottom |
            //        4 out | 7 = grounded v+ (1R tie)
            pedal::MnaTone n(7, fs_); n.Vsrc(1);
            n.R(1, 2, 100e3);                                 // R22
            n.R(2, 4, 100e3);                                 // R24 feedback
            n.R(1, 3, 1.8e3);                                 // R21
            n.R(3, 5, std::max(1.0 - treble_, 1e-3) * 10e3);  // Treble 10kB upper
            n.R(5, 6, std::max(treble_, 1e-3) * 10e3);        // Treble 10kB lower
            n.R(4, 6, 4.7e3);                                 // R23
            n.Cap(5, 2, 3.9e-9);                              // C14 wiper -> inv
            n.R(7, 0, 1.0);                                   // v+ tied to ground
            n.Opamp(7, 2, 4);
            n.compile(4); tone_ = n;
        }
    }
    static constexpr double R11 = 15e3, C7 = 82e-9, R12 = 422e3, C8 = 390e-12;
    static constexpr double R13 = 1e3, IS = 2e-6, NVT = 1.3 * 0.02585;   // 1N34A pair
    static constexpr double R20 = 392e3, C13 = 820e-12, C15 = 4.7e-6;
    double fs_, g_ = 0.5, treble_ = 0.5, level_ = 0.5;
    bool built_ = false;
    double lB0_ = 0, lB1_ = 0, lA1_ = 0, lX1_ = 0, lY1_ = 0;   // IC1B lower leg
    double gA_ = 0, gB_ = 0, gX1_ = 0, gY1_ = 0;               // Zf one-pole
    double vc_ = 0;                                             // clipper state
    double fA_ = 0, fB_ = 0, fX1_ = 0, fY1_ = 0;               // R20||C13
    double oA_ = 0, oB_ = 0, oX1_ = 0, oY1_ = 0;               // C15 HP
    pedal::MnaTone netC_, netD_, tone_;
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
        if (kind_ == Kind::Klon) {
            klon_.setGain(drive); klon_.setTreble(tone); klon_.setLevel(level);
        } else if (whitebox_) buildTone(tone);
        double wc = 2.0 * kPi * 14000.0, kk = 2.0 * fs;    // output-coupling top rolloff
        cpB_ = wc / (kk + wc); cpA_ = (wc - kk) / (kk + wc);
    }
    void setDrive(double d) {
        if (kind_ == Kind::Klon) klon_.setGain(d); else drive_.setDrive(d);
    }
    void setLevel(double l) { level_ = l; if (kind_ == Kind::Klon) klon_.setLevel(l); }
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
        if (kind_ == Kind::Klon)   // level = the real 560R + 10kB pot inside KlonStage
            return coupled(hpOut_.step(klon_.step(x)));
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
