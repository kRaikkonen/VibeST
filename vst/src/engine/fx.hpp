// Post-amp effects: Boss-style digital delay, a stereo studio room-mic
// (early-reflection based, NOT a reverb pedal), and a 9-band graphic EQ.
// These are signal-processing blocks (delay lines, biquads) not analog
// circuits, so they are honestly "DSP effects", not white-box circuit sims.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include "dsp.hpp"      // dsp::Biquad

namespace fx {

inline constexpr double kPi = 3.14159265358979323846;

// ---- Boss-style digital delay (DD-3/DD-7 voicing) --------------------------
// Digital delay line + feedback, with the classic slightly-dark repeats
// (each repeat rolls off highs a touch, like the real bucket of bits).
class BossDelay {
public:
    void init(double fs) {
        fs_ = fs;
        buf_.assign(static_cast<size_t>(fs * 2.5), 0.0);  // up to 2.5 s
        w_ = 0;
        fbLp_ = 0.0;
        setTimeMs(400.0);
    }
    void setTimeMs(double ms) { delaySamp_ = std::clamp(ms, 20.0, 2000.0)
                                             * fs_ / 1000.0; }
    void setFeedback(double f) { fb_ = std::clamp(f, 0.0, 0.95); }
    void setMix(double m) { mix_ = std::clamp(m, 0.0, 1.0); }
    void setToneHz(double hz) { toneA_ = 1.0 - std::exp(-2.0 * kPi * hz / fs_); }

    double process(double x) {
        size_t n = buf_.size();
        double rp = static_cast<double>(w_) - delaySamp_;
        while (rp < 0) rp += n;
        size_t i0 = static_cast<size_t>(rp);
        double frac = rp - i0;
        double a = buf_[i0];
        double b = buf_[(i0 + 1) % n];
        double d = a + (b - a) * frac;             // fractional read
        fbLp_ += toneA_ * (d - fbLp_);             // darken the repeats
        buf_[w_] = x + fb_ * fbLp_;                // write input + feedback
        w_ = (w_ + 1) % n;
        return x * (1.0 - mix_) + d * mix_;
    }

private:
    double fs_ = 48000, delaySamp_ = 19200, fb_ = 0.35, mix_ = 0.25;
    double fbLp_ = 0.0, toneA_ = 0.3;
    std::vector<double> buf_;
    size_t w_ = 0;
};

// ---- stereo studio room mic (early reflections) ----------------------------
// The sound of the cab in a room picked up by a pair of ambient mics: the
// direct sound plus a handful of wall/floor/ceiling reflections arriving at
// slightly different times L vs R -> a wide, 3-D "in the room" image. This is
// NOT a reverb tank; it is a short (<60 ms) reflection pattern + light
// diffusion, the way a room actually sounds on a close-ish room pair.
class RoomMic {
public:
    void init(double fs) {
        fs_ = fs;
        line_.assign(static_cast<size_t>(fs * 0.12), 0.0);
        w_ = 0;
        // reflection taps: {delay ms, gain, pan(-1..1)}. Values picked to
        // sound like a medium studio live room with the amp a few feet back.
        taps_ = {
            {  7.0, 0.62, -0.7}, { 11.0, 0.55,  0.8}, { 17.0, 0.44,  0.4},
            { 23.0, 0.40, -0.5}, { 29.0, 0.33,  0.6}, { 37.0, 0.28, -0.8},
            { 43.0, 0.24,  0.3}, { 53.0, 0.20, -0.4},
        };
        dampL_ = dampR_ = 0.0;
    }
    void setAmount(double a) { amount_ = std::clamp(a, 0.0, 1.0); }
    void setWidth(double w) { width_ = std::clamp(w, 0.0, 1.0); }

    void process(double x, double& outL, double& outR) {
        size_t n = line_.size();
        line_[w_] = x;
        double refL = 0, refR = 0;
        for (auto& t : taps_) {
            size_t d = static_cast<size_t>(t.ms * fs_ / 1000.0);
            double s = line_[(w_ + n - d) % n];
            double gl = t.gain * (0.5 - 0.5 * t.pan);   // pan law
            double gr = t.gain * (0.5 + 0.5 * t.pan);
            refL += s * gl;
            refR += s * gr;
        }
        w_ = (w_ + 1) % n;
        // gentle HF damping of the reflections (air + soft room)
        dampL_ += 0.35 * (refL - dampL_);
        dampR_ += 0.35 * (refR - dampR_);
        double wet = amount_;
        double dry = 1.0 - 0.35 * amount_;          // keep the direct sound
        double midL = x * dry + dampL_ * wet;
        double midR = x * dry + dampR_ * wet;
        // width: blend toward/away from mono
        double mid = 0.5 * (midL + midR);
        outL = mid + width_ * (midL - mid);
        outR = mid + width_ * (midR - mid);
    }

private:
    struct Tap { double ms, gain, pan; };
    double fs_ = 48000, amount_ = 0.3, width_ = 0.8;
    std::vector<double> line_;
    std::vector<Tap> taps_;
    size_t w_ = 0;
    double dampL_ = 0, dampR_ = 0;
};

// ---- 9-band graphic EQ + HPF/LPF (the pedal in the reference image) ---------
// Bands: 75 150 250 400 800 1.5k 4.5k 8k 12k Hz. Each is a peaking biquad.
// HPF/LPF are switchable (0 = off). Runs per stereo channel.
class GraphicEQ {
public:
    static constexpr int NB = 9;
    static constexpr double kFreq[NB] = {75, 150, 250, 400, 800,
                                         1500, 4500, 8000, 12000};

    void init(double fs) {
        fs_ = fs;
        for (int c = 0; c < 2; ++c)
            for (int b = 0; b < NB; ++b) {
                gains_[b] = 0.0;
                bands_[c][b].peaking(kFreq[b], 1.2, 0.0, fs);
            }
        hpfOn_ = lpfOn_ = false;
        gains_[8] = -0.4;                           // per the image (12 kHz)
        rebuildBand(8);
    }
    void setGainDb(int b, double db) {
        if (b < 0 || b >= NB) return;
        gains_[b] = std::clamp(db, -12.0, 12.0);
        rebuildBand(b);
    }
    void setHpf(bool on, double hz) {
        hpfOn_ = on;
        for (int c = 0; c < 2; ++c) hpf_[c].highpass(hz, 0.707, fs_);
    }
    void setLpf(bool on, double hz) {
        lpfOn_ = on;
        for (int c = 0; c < 2; ++c) lpf_[c].lowpass(hz, 0.707, fs_);
    }
    double step(int ch, double x) {
        double y = x;
        if (hpfOn_) y = hpf_[ch].step(y);
        for (int b = 0; b < NB; ++b) y = bands_[ch][b].step(y);
        if (lpfOn_) y = lpf_[ch].step(y);
        return y;
    }

private:
    void rebuildBand(int b) {
        for (int c = 0; c < 2; ++c)
            bands_[c][b].peaking(kFreq[b], 1.2, gains_[b], fs_);
    }
    double fs_ = 48000;
    double gains_[NB] = {};
    dsp::Biquad bands_[2][NB];
    dsp::Biquad hpf_[2], lpf_[2];
    bool hpfOn_ = false, lpfOn_ = false;
};

}  // namespace fx
