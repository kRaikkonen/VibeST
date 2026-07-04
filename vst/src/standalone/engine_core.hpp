// Shared core for the practice-amp front-ends (console + Win32 GUI):
// the white-box OD-1 + Princeton engine wiring, rate ladder, FIFOs,
// audio callback and device bring-up with low-latency options.
// The including .cpp must `#define MA_IMPLEMENTATION` before this header
// (exactly one TU per binary).
#pragma once
#include "../miniaudio.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>
#include <xmmintrin.h>

// NOTE: PA_FAST_JACOBIAN (analytic-J fast path) is deliberately OFF: under
// hard clipping it can latch a stage onto a wrong Newton branch — audibly the
// output "pops and collapses" when drive/master are pushed (field report).
// The FD solver below is the one validated against the Radau references.
#include "../engine/od1.hpp"
#include "../engine/pedals.hpp"
#include "../engine/princeton.hpp"
#include "../engine/plexi.hpp"
#include "../engine/dsp.hpp"

namespace pa {

constexpr int kChunk48 = 128;               // processing chunk at device rate
constexpr int kChunk96 = 2 * kChunk48;      // = spring-conv partition size
constexpr int kChunk192 = 4 * kChunk48;
constexpr int kChunk384 = 8 * kChunk48;     // OD-1 runs here (its valid rate)

struct Controls {
    // two stackable pedal slots (A feeds B feeds the amp)
    bool aOn = false, bOn = false;
    int aKind = 0, bKind = 2;       // 0 OD-1, 1 SD-1, 2 TS-808, 3 Mad Red
    double aDrive = 0.6, aTone = 0.5, aLevel = 0.35;
    double bDrive = 0.5, bTone = 0.5, bLevel = 0.35;
    int ampKind = 0;                // 0 Princeton Reverb, 1 Marshall Plexi
    int cabKind = 0;                // 0 C10R 1x10, 1 Greenback 2x12, 2 GB 4x12
    double volume = 0.4, treble = 0.55, bass = 0.5, reverb = 0.25;
    double tremSpeed = 0.45, tremIntensity = 0.0;
    // full-scale digital -> volts at the input jack. A real guitar sits at
    // ~0.1-0.25 V; 0.15 mapped a typical picked note to only ~30-60 mV, far
    // too weak to drive the OD-1 into real clipping (it barely worked, so it
    // read as "linear + a little"). 0.4 puts it in the real ~0.1-0.2 V range.
    double inTrim = 0.4;
    // master maps speaker VOLTS to digital full scale. The amp puts out
    // ~12-20 V when driven; 0.06 makes a cranked amp reach ~full scale so the
    // amp's OWN power-stage breakup is the tone. (0.5 was ~8x too hot and
    // slammed the output limiter on every note = a 2nd, non-blending clip
    // layer — the "two layers" the user heard.)
    double master = 0.045;
                                            // (matched to the hot-reverb
                                            // voicing's output level)
};

struct Engine {
    // eco: amp core at 48 kHz, OD-1 at 96 kHz (halves CPU). Physically
    // defensible: the OT winding capacitance + leakage + speaker roll off
    // everything above ~10 kHz anyway. HQ: amp 96 kHz, OD-1 192 kHz.
    Engine(std::vector<double> tankIr, std::vector<double> cabIr,
           bool eco_ = false)
        : eco(eco_),
          ampChunk(eco_ ? kChunk48 : kChunk96),
          // OD-1 pedal at 192 k with its stiff stages (clipper + filter op-amp)
          // internally substepping 2x -> effective 384 k where it matters, at
          // roughly half the cost of running the whole pedal at 384 k. Verified
          // to match the 384 k reference sample-for-sample (diag_od1).
          pedalA(192000.0, 0.5, 0.8, od1::RC3403A(), od1::Params{}, 2),
          pedalB(192000.0, 0.5, 0.8, od1::RC3403A(), od1::Params{}, 2),
          fxA(pedals::Kind::SD1, 192000.0, 0.6, 0.35, 0.5, 2),
          fxB(pedals::Kind::TS808, 192000.0, 0.5, 0.35, 0.5, 2),
          plexiAmp(eco_ ? 48000.0 : 96000.0, 0.6, 0.6, 0.4, 0.5, 1.0),
          amp(eco_ ? 48000.0 : 96000.0, princeton::AmpControls{},
              makeTank()) {
        if (!tankIr.empty()) conv.init(tankIr, ampChunk);
        else tankBypass = true;
        if (!cabIr.empty()) {
            cabConv.init(cabIr, kChunk48);
            useCabIr = true;
        }
        // synthetic C10R voicing (only used when no cab IR is loaded). The
        // NFB runs open-loop above 250 Hz (stability compromise) so the raw
        // amp is bright; a real 10" guitar speaker rolls off hard past ~4 kHz
        // and dips the 3 kHz "ice pick", which also tames that brightness.
        // amp input coupling / DC blocker: the OD-1's asymmetric clipping
        // leaves a DC + subsonic component that shifts the amp bias and makes
        // it fart / "broken speaker". Real amps AC-couple their input jack;
        // 18 Hz is below the lowest guitar note so it is tonally transparent.
        ampInHp.highpass(18.0, 0.707, eco_ ? 48000.0 : 96000.0);
        rebuildCab(0);
        apply(Controls{});
    }

    princeton::Amp::TankConv makeTank() {
        return [this](const double* in, double* out, int n) {
            if (tankBypass || n != ampChunk) {
                std::memset(out, 0, n * sizeof(double));
                return;
            }
            conv.process(in, out);
        };
    }

    void setCab(std::vector<double> ir) {
        if (ir.empty()) { useCabIr = false; return; }
        cabConv.init(ir, kChunk48);
        useCabIr = true;
    }

    void applySlot(od1::Pedal& od, pedals::Pedal& fx, int kind, int oldKind,
                   double drive, double tone, double level) {
        static const pedals::Kind kinds[] = {
            pedals::Kind::OD1, pedals::Kind::SD1,
            pedals::Kind::TS808, pedals::Kind::MadRed};
        if (kind != oldKind && kind != 0)
            fx = pedals::Pedal(kinds[kind], 192000.0, drive, level, tone, 2);
        if (kind == 0) {
            od.setDrive(drive);
            od.setLevel(level);
        } else {
            fx.setDrive(drive);
            fx.setLevel(level);
            fx.setTone(tone);
        }
    }

    void apply(const Controls& c) {
        applySlot(pedalA, fxA, c.aKind, ctl.aKind, c.aDrive, c.aTone, c.aLevel);
        applySlot(pedalB, fxB, c.bKind, ctl.bKind, c.bDrive, c.bTone, c.bLevel);
        amp.setTone(c.treble, c.bass, c.volume);
        amp.setReverb(c.reverb);
        amp.setTremolo(c.tremSpeed, c.tremIntensity);
        // Marshall Plexi control map: Volume->Gain, Reverb->Middle,
        // TremSpeed->Presence, TremIntensity->High Treble (bright)
        plexiAmp.setGain(c.volume);
        plexiAmp.setTone(c.treble, c.bass, c.reverb);
        plexiAmp.setPresence(c.tremSpeed);
        plexiAmp.setBright(c.tremIntensity);
        if (c.cabKind != ctl.cabKind) rebuildCab(c.cabKind);
        ctl = c;
    }

    void rebuildCab(int k) {
        if (k == 1) {          // Celestion Greenback G12M 2x12
            cabHp.highpass(75.0, 0.707, 48000.0);
            cabBump.peaking(120.0, 1.2, 2.5, 48000.0);
            cabPres.peaking(2400.0, 1.4, 3.0, 48000.0);   // cone breakup
            cabLp1.lowpass(5200.0, 0.75, 48000.0);
            cabLp2.lowpass(5200.0, 0.75, 48000.0);
        } else if (k == 2) {   // G12M 4x12 closed back
            cabHp.highpass(65.0, 0.707, 48000.0);
            cabBump.peaking(100.0, 1.0, 5.0, 48000.0);    // big low bump
            cabPres.peaking(2300.0, 1.4, 3.0, 48000.0);
            cabLp1.lowpass(4600.0, 0.75, 48000.0);
            cabLp2.lowpass(4600.0, 0.75, 48000.0);
        } else {               // Jensen C10R 1x10 (Princeton)
            cabHp.highpass(80.0, 0.707, 48000.0);
            cabBump.peaking(110.0, 1.1, 0.0, 48000.0);
            cabPres.peaking(3200.0, 1.4, -4.0, 48000.0);
            cabLp1.lowpass(4300.0, 0.707, 48000.0);
            cabLp2.lowpass(4300.0, 0.707, 48000.0);
        }
    }

    // one chunk: 128 mono samples in/out at 48k
    void processChunk(const float* in, float* out) {
        double x48[kChunk48], x96[kChunk96], x192[kChunk192], x384[kChunk384];
        double b1[kChunk96], b2[kChunk96], b3[kChunk96], y96[kChunk96];
        double y48[kChunk48];
        float ipk = inPeak.load() * 0.95f;
        for (int i = 0; i < kChunk48; ++i)
            ipk = std::max(ipk, std::fabs(in[i]));
        inPeak.store(ipk);
        for (int i = 0; i < kChunk48; ++i)
            x48[i] = static_cast<double>(in[i]) * ctl.inTrim;

        // ---- pedal slots A -> B (192 k, stiff stages substep internally) ---
        (void)x384;
        double* ampIn;
        int ampN;
        if (ctl.aOn || ctl.bOn) {
            odUp1.process(x48, x96, kChunk48);
            odUp2.process(x96, x192, kChunk96);
            if (ctl.aOn) {
                if (ctl.aKind == 0)
                    for (int i = 0; i < kChunk192; ++i) x192[i] = pedalA.step(x192[i]);
                else
                    for (int i = 0; i < kChunk192; ++i) x192[i] = fxA.step(x192[i]);
            }
            if (ctl.bOn) {
                if (ctl.bKind == 0)
                    for (int i = 0; i < kChunk192; ++i) x192[i] = pedalB.step(x192[i]);
                else
                    for (int i = 0; i < kChunk192; ++i) x192[i] = fxB.step(x192[i]);
            }
            odDn2.process(x192, x96, kChunk96);
            if (eco) { odDn3.process(x96, x48, kChunk48); ampIn = x48; ampN = kChunk48; }
            else     { ampIn = x96; ampN = kChunk96; }
        } else if (eco) {
            ampIn = x48; ampN = kChunk48;
        } else {
            ampUp.process(x48, x96, kChunk48);
            ampIn = x96; ampN = kChunk96;
        }

        // ---- power amp (Princeton or Plexi), then down to 48 k -------------
        for (int i = 0; i < ampN; ++i) ampIn[i] = ampInHp.step(ampIn[i]);
        double* ampOut = (ampN == kChunk48) ? y48 : y96;
        if (ctl.ampKind == 0)
            amp.processBlock(ampIn, ampOut, ampN, b1, b2, b3);
        else
            plexiAmp.processBlock(ampIn, ampOut, ampN);
        if (ampN == kChunk96) ampDn.process(y96, y48, kChunk48);

        // ---- cab (IR or synthetic voicing) + master ------------------------
        if (useCabIr) {
            double yc[kChunk48];
            cabConv.process(y48, yc);
            std::memcpy(y48, yc, sizeof(y48));
        } else {
            for (int i = 0; i < kChunk48; ++i)
                y48[i] = cabLp2.step(cabLp1.step(
                    cabPres.step(cabBump.step(cabHp.step(y48[i])))));
        }
        // soft output ceiling with a linear region: a hard clamp turns an
        // over-driven signal into a harsh digital square ("broken speaker").
        // Below 0.9 it is exactly linear (the approved clean tone is
        // untouched); above 0.9 it saturates smoothly to a 1.0 ceiling.
        float opk = outPeak.load() * 0.95f;
        for (int i = 0; i < kChunk48; ++i) {
            double a = y48[i] * ctl.master;
            double s = std::fabs(a);
            if (s > 0.95)   // safety only — the amp should do the clipping
                a = std::copysign(0.95 + 0.05 * std::tanh((s - 0.95) / 0.05), a);
            out[i] = static_cast<float>(a);
            opk = std::max(opk, static_cast<float>(std::fabs(a)));
        }
        outPeak.store(opk);
    }

    bool eco;
    int ampChunk;
    od1::Pedal pedalA, pedalB;     // deep-white-box OD-1 (kind 0), per slot
    pedals::Pedal fxA, fxB;        // SD-1 / TS-808 / Mad Red per slot
    princeton::Amp amp;
    plexi::Amp plexiAmp;           // Marshall Super Lead Plexi
    dsp::PartConv conv, cabConv;
    bool tankBypass = false;
    bool useCabIr = false;
    dsp::Up2 odUp1, odUp2, odUp3;      // 48->96->192->384 into the OD-1
    dsp::Down2 odDn1, odDn2, odDn3;    // 384->192->96->48 out of the OD-1
    dsp::Up2 ampUp;                    // 48->96 amp input (HQ, OD-1 off)
    dsp::Down2 ampDn;                  // 96->48 amp output (HQ)
    dsp::Biquad ampInHp;               // amp input DC blocker (18 Hz)
    dsp::Biquad cabHp, cabBump, cabPres, cabLp1, cabLp2;
    Controls ctl;
    std::mutex mtx;
    // O(1) ring FIFOs (the old vector-erase FIFOs were O(n^2) and, worse,
    // unbounded: one missed deadline grew input backlog — and latency —
    // permanently)
    struct Ring {
        static constexpr size_t N = 1 << 15;    // 32768 floats (~0.7 s)
        float buf[N] = {};
        size_t head = 0, tail = 0;              // pop at head, push at tail
        size_t avail() const { return (tail - head + N) % N; }
        void push(float v) { buf[tail] = v; tail = (tail + 1) % N; }
        float pop() { float v = buf[head]; head = (head + 1) % N; return v; }
        void drop(size_t n) { head = (head + n) % N; }
    };
    Ring inRing, outRing;
    std::atomic<int> loadPct{0};    // EMA of engine time / realtime budget
    std::atomic<float> inPeak{0.0f}, outPeak{0.0f};   // level meters
    std::atomic<int> drops{0};      // resync events (audible clicks) counter
    double dbgX96 = 0, dbgY96 = 0;  // debug: amp in/out peak
    double dbgY96sq = 0, dbgXdc = 0, dbgY48 = 0, dbgYcab = 0, dbgYcabSq = 0;
    double dbgY48Sq = 0;
    long dbgN = 0;
    std::vector<double> dbgCap;   // capture of y48 for offline FFT
};

inline void dataCallback(ma_device* dev, void* pOut, const void* pIn,
                         ma_uint32 frames) {
    _mm_setcsr(_mm_getcsr() | 0x8040);   // FTZ/DAZ: denormals cause CPU
                                         // spikes -> dropped chunks (fizz)
    auto* e = static_cast<Engine*>(dev->pUserData);
    auto* out = static_cast<float*>(pOut);
    auto* in = static_cast<const float*>(pIn);
    std::lock_guard<std::mutex> lk(e->mtx);
    for (ma_uint32 i = 0; i < frames; ++i)
        e->inRing.push(in ? in[i * dev->capture.channels] : 0.0f);
    int chunksDone = 0;
    auto t0 = std::chrono::steady_clock::now();
    while (e->inRing.avail() >= kChunk48) {
        float chunkIn[kChunk48], chunkOut[kChunk48];
        for (int i = 0; i < kChunk48; ++i) chunkIn[i] = e->inRing.pop();
        e->processChunk(chunkIn, chunkOut);
        for (int i = 0; i < kChunk48; ++i) e->outRing.push(chunkOut[i]);
        ++chunksDone;
    }
    if (chunksDone > 0) {
        double sec = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - t0).count();
        double budget = chunksDone * kChunk48 / 48000.0;
        int pct = static_cast<int>(100.0 * sec / budget);
        e->loadPct.store((e->loadPct.load() * 7 + pct) / 8);
    }
    // backlog governor: if we fell behind (missed deadline), resync instead
    // of letting the latency grow permanently
    if (e->inRing.avail() > 2 * kChunk48) {
        e->inRing.drop(e->inRing.avail() - kChunk48);
        e->drops.fetch_add(1);
    }
    if (e->outRing.avail() > 6 * kChunk48)
        e->outRing.drop(e->outRing.avail() - 2 * kChunk48);
    for (ma_uint32 i = 0; i < frames; ++i) {
        float s = e->outRing.avail() ? e->outRing.pop() : 0.0f;
        for (ma_uint32 c = 0; c < dev->playback.channels; ++c)
            out[i * dev->playback.channels + c] = s;
    }
}

// spring tank IR generated from the physical model at startup (port of
// proto/princeton.py spring_tank_ir) — no external file needed
inline std::vector<double> springTankIr(double fs = 96000.0,
                                        double dur = 1.6) {
    const double kPi2 = 6.283185307179586;
    const size_t n = static_cast<size_t>(dur * fs);
    size_t nfft = 1;
    while (nfft < 2 * n) nfft <<= 1;
    std::vector<dsp::cplx> H(nfft, dsp::cplx(0.0, 0.0));
    // stronger dispersion + 3 springs + longer decay: weak dispersion made
    // echoes read as discrete slapbacks instead of a diffuse spring wash
    // (field report: "reverb pot only changes size, not depth")
    const double springs[3][2] = {{0.027, 0.70}, {0.031, 0.66},
                                  {0.0365, 0.68}};
    const double fc = 4200.0, dampF = 4500.0;
    const double wc = kPi2 * fc;
    for (const auto& s : springs) {
        double Td = s[0], r = s[1];
        for (size_t k = 0; k <= nfft / 2; ++k) {
            double f = k * fs / static_cast<double>(nfft);
            double w = kPi2 * f;
            double phi = Td * (w + 1.1 * w * w / wc);
            double L = std::exp(-(f / dampF) * (f / dampF));
            dsp::cplx G = r * L * dsp::cplx(std::cos(phi), -std::sin(phi));
            dsp::cplx rt2 = (r * L) * (r * L)
                * dsp::cplx(std::cos(2 * phi), -std::sin(2 * phi));
            H[k] += G / (dsp::cplx(1.0, 0.0) - rt2);
        }
    }
    for (size_t k = 1; k < nfft / 2; ++k)
        H[nfft - k] = std::conj(H[k]);
    H[nfft / 2] = dsp::cplx(H[nfft / 2].real(), 0.0);
    dsp::fft(H, true);
    std::vector<double> h(n);
    double pk = 1e-12;
    for (size_t i = 0; i < n; ++i) {
        h[i] = H[i].real();
        pk = std::max(pk, std::fabs(h[i]));
    }
    for (double& v : h) v /= pk;
    return h;
}

inline std::vector<double> loadIr(const char* path) {
    std::vector<double> v;
    if (FILE* f = std::fopen(path, "rb")) {
        std::fseek(f, 0, SEEK_END);
        long bytes = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        v.resize(static_cast<size_t>(bytes) / sizeof(double));
        if (std::fread(v.data(), sizeof(double), v.size(), f) != v.size())
            v.clear();
        std::fclose(f);
    }
    return v;
}

// cab IR loader via miniaudio's decoder (wav/flac/mp3, resampled to 48k mono)
inline std::vector<double> loadCabWav(const char* path) {
    std::vector<double> v;
    ma_decoder_config dc = ma_decoder_config_init(ma_format_f32, 1, 48000);
    ma_decoder dec;
    if (ma_decoder_init_file(path, &dc, &dec) != MA_SUCCESS) return v;
    std::vector<float> tmp(4096);
    ma_uint64 got = 0;
    for (;;) {
        if (ma_decoder_read_pcm_frames(&dec, tmp.data(), tmp.size(), &got)
                != MA_SUCCESS || got == 0)
            break;
        for (ma_uint64 i = 0; i < got; ++i) v.push_back(tmp[i]);
        if (v.size() > 48000 * 4) break;
    }
    ma_decoder_uninit(&dec);
    double pk = 1e-12;
    for (double s : v) pk = std::max(pk, std::fabs(s));
    for (double& s : v) s /= pk;
    return v;
}

// device bring-up: requests small periods; exclusive mode when asked.
// Returns MA result; fills out actual period info string.
inline ma_result startDevice(ma_context* ctx, ma_device* dev, Engine* engine,
                             const ma_device_id* inId,
                             const ma_device_id* outId,
                             bool exclusive, char* info, size_t infoLen,
                             ma_uint32 periodFrames = kChunk48) {
    ma_device_config cfg = ma_device_config_init(ma_device_type_duplex);
    cfg.sampleRate = 48000;
    cfg.capture.format = ma_format_f32;
    cfg.capture.channels = 1;
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = 2;
    cfg.capture.pDeviceID = inId;
    cfg.playback.pDeviceID = outId;
    cfg.periodSizeInFrames = periodFrames;
    cfg.periods = 2;
    cfg.performanceProfile = ma_performance_profile_low_latency;
    if (exclusive) {
        cfg.capture.shareMode = ma_share_mode_exclusive;
        cfg.playback.shareMode = ma_share_mode_exclusive;
    }
    cfg.dataCallback = dataCallback;
    cfg.pUserData = engine;
    ma_result r = ma_device_init(ctx, &cfg, dev);
    if (r != MA_SUCCESS && exclusive) {
        // exclusive refused (device busy / unsupported): fall back to shared
        cfg.capture.shareMode = ma_share_mode_shared;
        cfg.playback.shareMode = ma_share_mode_shared;
        r = ma_device_init(ctx, &cfg, dev);
        exclusive = false;
    }
    if (r != MA_SUCCESS) return r;
    ma_device_start(dev);
    if (info) {
        double ms = (dev->capture.internalPeriodSizeInFrames
                     * dev->capture.internalPeriods
                     + dev->playback.internalPeriodSizeInFrames
                     * dev->playback.internalPeriods
                     + kChunk48) * 1000.0 / 48000.0;
        std::snprintf(info, infoLen,
                      "%s | cap %u x%u, pb %u x%u | ~%.1f ms round trip",
                      exclusive ? "WASAPI exclusive" : "WASAPI shared",
                      dev->capture.internalPeriodSizeInFrames,
                      dev->capture.internalPeriods,
                      dev->playback.internalPeriodSizeInFrames,
                      dev->playback.internalPeriods, ms);
    }
    return MA_SUCCESS;
}

}  // namespace pa
