// Post-amp effects: Boss-style digital delay, a stereo studio room-mic
// (early-reflection based, NOT a reverb pedal), and a 9-band graphic EQ.
// These are signal-processing blocks (delay lines, biquads) not analog
// circuits, so they are honestly "DSP effects", not white-box circuit sims.
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>
#include "dsp.hpp"      // dsp::Biquad

namespace fx {

inline constexpr double kPi = 3.14159265358979323846;

// ---- noise gate (front of chain) -------------------------------------------
// Envelope-following gate: when the guitar signal falls below threshold, fade
// the level down (kills hiss/hum between notes). Fast attack (opens instantly
// on a note), slower release (doesn't chatter on decays).
class NoiseGate {
public:
    void init(double fs) {
        fs_ = fs;
        atkA_ = 1.0 - std::exp(-1.0 / (0.002 * fs));   // 2 ms open
        relA_ = 1.0 - std::exp(-1.0 / (0.080 * fs));   // 80 ms close
    }
    void setThreshold(double t01) {
        // 0..1 -> -70..-20 dBFS threshold
        thr_ = std::pow(10.0, (-70.0 + 50.0 * std::clamp(t01, 0.0, 1.0)) / 20.0);
    }
    void setDecay(double d01) {
        // gate close (release) time: 0..1 -> 30..600 ms (higher = slower close)
        double relS = 0.03 + 0.57 * std::clamp(d01, 0.0, 1.0);
        relA_ = 1.0 - std::exp(-1.0 / (relS * fs_));
    }
    double process(double x) {
        double e = std::fabs(x);
        env_ += (e > env_ ? atkA_ : relA_) * (e - env_);
        double target = env_ > thr_ ? 1.0 : 0.0;
        gain_ += (target > gain_ ? atkA_ : relA_) * (target - gain_);
        return x * gain_;
    }
private:
    double fs_ = 48000, atkA_ = 0.1, relA_ = 0.001, thr_ = 0.003;
    double env_ = 0, gain_ = 0;
};

// ---- Boss CE-2 chorus (BBD, mono) ------------------------------------------
// A chorus is a short (~5-20 ms) delay whose time is swept by an LFO, mixed
// 50/50 with dry -> the pitch-wobbling "shimmer/thicken". The CE-2 is a mono
// BBD chorus with Rate + Depth. Repeats are slightly dark (BBD bandwidth).
class CE2Chorus {
public:
    void init(double fs) {
        fs_ = fs;
        buf_.assign(static_cast<size_t>(fs * 0.05), 0.0);   // up to 50 ms
        w_ = 0; ph_ = 0; lp_ = 0;
        setRate(0.4); setDepth(0.6);
        baseMs_ = 9.0;   // BBD nominal delay
    }
    void setRate(double r01) {
        rateHz_ = 0.1 + 3.4 * std::clamp(r01, 0.0, 1.0);    // 0.1..3.5 Hz
    }
    void setDepth(double d01) { depth_ = std::clamp(d01, 0.0, 1.0); }
    void setMix(double m) { mix_ = std::clamp(m, 0.0, 1.0); }

    double readAt(double ms) {
        double dsamp = ms * fs_ / 1000.0;
        size_t n = buf_.size();
        double rp = static_cast<double>(w_) - dsamp;
        while (rp < 0) rp += n;
        size_t i0 = static_cast<size_t>(rp);
        double frac = rp - i0;
        double a = buf_[i0], b = buf_[(i0 + 1) % n];
        return a + (b - a) * frac;
    }
    double process(double x) {
        ph_ += 2.0 * kPi * rateHz_ / fs_;
        if (ph_ > 2.0 * kPi) ph_ -= 2.0 * kPi;
        buf_[w_] = x;
        // GENTLE modulation: base 7 ms, +/- (depth*2.5) ms. Deeper than this
        // turns the pitch wobble into seasick warble / "spring boing". Two
        // voices in quadrature (sin and cos LFO) give the lush CE-2 shimmer
        // and average out the single-voice comb notch, so it never sounds
        // like a static flanger at low depth.
        double m1 = 7.0 + depth_ * 2.5 * std::sin(ph_);
        double m2 = 8.0 + depth_ * 2.5 * std::cos(ph_);   // quadrature voice
        double d = 0.5 * (readAt(m1) + readAt(m2));
        lp_ += 0.5 * (d - lp_);             // BBD bandwidth darkening
        w_ = (w_ + 1) % buf_.size();
        return x * (1.0 - 0.5 * mix_) + lp_ * (0.5 * mix_);
    }
private:
    double fs_ = 48000, rateHz_ = 1.0, depth_ = 0.55, mix_ = 1.0, baseMs_ = 7;
    double ph_ = 0, lp_ = 0;
    std::vector<double> buf_;
    size_t w_ = 0;
};

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
    // Boss "E.Level" — the repeat volume added on top of the (always full) dry
    void setLevel(double m) { level_ = std::clamp(m, 0.0, 1.5); }
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
        double wr = x + fb_ * fbLp_;               // input + feedback
        // anti-runaway ONLY: the delay runs in the amp-voltage domain (~±25 V),
        // so the safety bound must be at the amp's physical limit, not ±1.
        // (An ±8 bound here zeroed the buffer on every loud sample -> chopped,
        //  "digital"/dirty echoes.)
        if (!std::isfinite(wr)) wr = 0.0;
        wr = std::clamp(wr, -2000.0, 2000.0);
        buf_[w_] = wr;
        w_ = (w_ + 1) % n;
        return x + level_ * d;                     // dry always full + echoes
    }

private:
    double fs_ = 48000, delaySamp_ = 19200, fb_ = 0.35, level_ = 0.6;
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

// ---- Keeley-style feed-forward compressor (front of chain, before the drives) ----
// Sustain lowers threshold + raises ratio; Level = makeup. Musical/fast.
class Compressor {
public:
    void init(double fs) {
        fs_ = fs; env_ = 0.0; lp_ = 0.0;
        atk_ = std::exp(-1.0 / (0.004 * fs));   // ~4 ms attack
        rel_ = std::exp(-1.0 / (0.18 * fs));    // ~180 ms release
        toneA_ = 1.0 - std::exp(-2.0 * kPi * 800.0 / fs);   // tilt pivot ~800 Hz
    }
    void setSustain(double s) {
        s = std::clamp(s, 0.0, 1.0);
        thr_ = 0.5 - 0.46 * s;                  // more sustain -> squash sooner
        ratio_ = 2.0 + 8.0 * s;                 // and harder
    }
    void setLevel(double l) { makeup_ = 0.4 + 1.8 * std::clamp(l, 0.0, 1.0); }
    void setBlend(double b) { blend_ = std::clamp(b, 0.0, 1.0); }   // dry <-> compressed
    void setTone(double t) { tone_ = std::clamp(t, 0.0, 1.0); }     // 0.5 = flat, up = brighter
    double process(double x) {
        double a = std::fabs(x);
        env_ = a > env_ ? atk_ * env_ + (1 - atk_) * a : rel_ * env_ + (1 - rel_) * a;
        double g = 1.0;
        if (env_ > thr_ && env_ > 1e-9)
            g = std::pow(env_ / thr_, (1.0 / ratio_) - 1.0);   // gain reduction above thr
        double wet = x * g * makeup_;
        double y = x * (1.0 - blend_) + wet * blend_;          // parallel (NY) blend
        lp_ += toneA_ * (y - lp_);                             // split ~800 Hz
        double hi = y - lp_;
        return lp_ * (2.0 - 2.0 * tone_) + hi * (2.0 * tone_); // tilt: flat at 0.5
    }
private:
    double fs_ = 48000, env_ = 0, atk_ = 0.99, rel_ = 0.999;
    double thr_ = 0.3, ratio_ = 4.0, makeup_ = 1.0;
    double blend_ = 1.0, tone_ = 0.5, lp_ = 0.0, toneA_ = 0.1;
};

// ---- digital reverb (Freeverb: 8 comb + 4 allpass per channel, stereo) ----------
class Reverb {
public:
    void init(double fs) {
        double sc = fs / 44100.0;
        const int cT[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
        const int aT[4] = {556, 441, 341, 225};
        const int spread = 23;
        for (int i = 0; i < 8; ++i) {
            cL_[i].assign((size_t)(cT[i] * sc) + 1, 0.0); ciL_[i] = 0; dL_[i] = 0;
            cR_[i].assign((size_t)((cT[i] + spread) * sc) + 1, 0.0); ciR_[i] = 0; dR_[i] = 0;
        }
        for (int i = 0; i < 4; ++i) {
            aL_[i].assign((size_t)(aT[i] * sc) + 1, 0.0); aiL_[i] = 0;
            aR_[i].assign((size_t)((aT[i] + spread) * sc) + 1, 0.0); aiR_[i] = 0;
        }
    }
    void setDecay(double d) { fb_ = 0.70 + 0.288 * std::clamp(d, 0.0, 1.0); }  // room size
    void setMix(double m) { mix_ = std::clamp(m, 0.0, 1.0); }
    void process(double x, double& outL, double& outR) {
        double in = x * 0.045;                 // input gain (audible wet at mix ~0.3)
        double wl = 0.0, wr = 0.0;
        for (int i = 0; i < 8; ++i) { wl += comb(cL_[i], ciL_[i], dL_[i], in);
                                      wr += comb(cR_[i], ciR_[i], dR_[i], in); }
        for (int i = 0; i < 4; ++i) { wl = allp(aL_[i], aiL_[i], wl);
                                      wr = allp(aR_[i], aiR_[i], wr); }
        outL = x * (1.0 - mix_) + wl * mix_;
        outR = x * (1.0 - mix_) + wr * mix_;
    }
private:
    double comb(std::vector<double>& b, size_t& i, double& damp, double in) {
        double y = b[i];
        damp = y * (1.0 - damp_) + damp * damp_;      // HF damping in the loop
        b[i] = in + damp * fb_;
        i = (i + 1) % b.size();
        return y;
    }
    double allp(std::vector<double>& b, size_t& i, double in) {
        double y = b[i];
        b[i] = in + y * 0.5;
        i = (i + 1) % b.size();
        return y - in;
    }
    std::vector<double> cL_[8], cR_[8], aL_[4], aR_[4];
    size_t ciL_[8]{}, ciR_[8]{}, aiL_[4]{}, aiR_[4]{};
    double dL_[8]{}, dR_[8]{}, fb_ = 0.84, mix_ = 0.3, damp_ = 0.3;
};

// ---- chromatic tuner: decimating autocorrelation pitch detector ------------
// Taps the input (parallel, does not alter the audio). Decimates to ~6kHz
// (guitar fundamentals are <1.4kHz) then runs a normalized autocorrelation
// (NSDF) to find the period; parabolic interpolation gives sub-sample accuracy
// so cents are meaningful. The GUI turns the raw Hz into note + cents using the
// user's reference A (which is display-only; it doesn't touch the signal).
class Tuner {
public:
    void init(double fs) {
        decim_ = std::max(1, (int)std::lround(fs / 6000.0));   // -> ~6 kHz internal
        dfs_ = fs / decim_;
        buf_.assign(N, 0.0f); lin_.assign(N, 0.0f); nsdf_.assign(N, 0.0);
        w_ = 0; dcnt_ = 0; hop_ = 0; acc_ = 0.0f; freq_.store(0.0f);
    }
    void process(float x) {                         // one input sample
        acc_ += x;                                  // box-average anti-alias
        if (++dcnt_ >= decim_) {
            buf_[w_] = acc_ / decim_; acc_ = 0.0f; dcnt_ = 0;
            w_ = (w_ + 1) % N;
            if (++hop_ >= HOP) { hop_ = 0; detect(); }
        }
    }
    float freq() const { return freq_.load(std::memory_order_relaxed); }
private:
    void detect() {
        double e0 = 0.0;
        for (int i = 0; i < N; ++i) { lin_[i] = buf_[(w_ + i) % N]; e0 += (double)lin_[i] * lin_[i]; }
        if (std::sqrt(e0 / N) < 3e-3) { freq_.store(0.0f); return; }   // too quiet -> no note
        int minLag = std::max(2, (int)(dfs_ / 1400.0));
        int maxLag = std::min(N - 1, (int)(dfs_ / 60.0));
        double best = 0.0; int bestLag = 0;
        for (int lag = minLag; lag <= maxLag; ++lag) {
            double ac = 0.0, e = 0.0;
            for (int i = 0; i < N - lag; ++i) { ac += (double)lin_[i] * lin_[i + lag]; e += (double)lin_[i + lag] * lin_[i + lag]; }
            double nsdf = 2.0 * ac / (e0 + e + 1e-12);   // -1..1, 1 = perfect period
            nsdf_[lag] = nsdf;
            if (nsdf > best) { best = nsdf; bestLag = lag; }
        }
        if (bestLag < minLag + 1 || bestLag > maxLag - 1 || best < 0.4) { freq_.store(0.0f); return; }
        double a = nsdf_[bestLag - 1], b = nsdf_[bestLag], c = nsdf_[bestLag + 1];   // parabolic interp
        double den = a - 2.0 * b + c;
        double delta = std::fabs(den) > 1e-12 ? 0.5 * (a - c) / den : 0.0;
        if (delta < -1.0 || delta > 1.0) delta = 0.0;
        freq_.store((float)(dfs_ / (bestLag + delta)));
    }
    static constexpr int N = 1024, HOP = 512;
    int decim_ = 8, w_ = 0, dcnt_ = 0, hop_ = 0;
    double dfs_ = 6000.0;
    float acc_ = 0.0f;
    std::vector<float> buf_, lin_;
    std::vector<double> nsdf_;
    std::atomic<float> freq_{0.0f};
};

// ---- "shift pose" pitch shifter: 2-tap crossfaded delay line ----------------
// Classic H910/granular real-time shifter. Two read taps 180deg apart sweep the
// delay at rate (ratio-1); each is windowed by sin^2 so as one tap approaches a
// wrap discontinuity the other (mid-window) takes over -- the two windows are
// complementary (sin^2 + cos^2 = 1), so no amplitude ripple. Good for the small
// shifts we need (440->451Hz = ~1.04 semitone), low latency, no FFT.
class PitchShift {
public:
    void init(double fs) {
        fs_ = fs; win_ = std::max(64, (int)(fs * 0.045));   // ~45 ms grain
        buf_.assign((size_t)win_ * 2 + 4, 0.0f); w_ = 0; ph_ = 0.0; ratio_ = 1.0;
    }
    void setRatio(double r) { ratio_ = std::min(std::max(r, 0.45), 2.2); }  // out/in pitch (±1 oct + fine)
    double process(double x) {
        int n = (int)buf_.size();
        buf_[w_] = (float)x;
        ph_ += (1.0 - ratio_);                       // delay sweep: ratio>1 -> delay shrinks -> pitch up
        while (ph_ >= win_) ph_ -= win_;
        while (ph_ < 0.0)   ph_ += win_;
        double d1 = ph_;
        double d2 = ph_ + win_ * 0.5; if (d2 >= win_) d2 -= win_;
        double s1 = std::sin(kPi * d1 / win_), g1 = s1 * s1;   // sin^2 window
        double s2 = std::sin(kPi * d2 / win_), g2 = s2 * s2;   // = cos^2 -> g1+g2=1
        double y = g1 * read(w_ - d1, n) + g2 * read(w_ - d2, n);
        w_ = (w_ + 1) % n;
        return y;
    }
private:
    double read(double pos, int n) const {
        while (pos < 0.0) pos += n;
        int i = (int)pos; double f = pos - i;
        int j = (i + 1) % n; i %= n;
        return buf_[i] * (1.0 - f) + buf_[j] * f;
    }
    double fs_ = 48000.0, ratio_ = 1.0, ph_ = 0.0;
    int win_ = 2160, w_ = 0;
    std::vector<float> buf_;
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
