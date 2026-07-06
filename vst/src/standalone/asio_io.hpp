// ASIO backend (via RtAudio) for the practice amp. ASIO is inherently
// duplex-single-device: input and output live on the same driver, which is
// exactly the Focusrite use case and the low-latency path on Windows.
#pragma once
#include "RtAudio.h"
#include "engine_core.hpp"

#include <string>
#include <vector>

namespace pa {

// 4-point 3rd-order (Catmull-Rom) cubic interpolation, t in [0,1] between x0..x1.
inline float hermite4(float xm, float x0, float x1, float x2, double t) {
    double c1 = 0.5 * (x1 - xm);
    double c2 = xm - 2.5 * x0 + 2.0 * x1 - 0.5 * x2;
    double c3 = 0.5 * (x2 - xm) + 1.5 * (x0 - x1);
    return static_cast<float>((((c3 * t + c2) * t + c1) * t) + x0);
}

// The engine core is fixed at 48 kHz. When the ASIO device runs at another rate
// (e.g. 44.1 kHz to match music playback) these convert at the device boundary.
// Both are driven by the single audio callback clock, so the fixed 48/device
// ratio is exact and drift-free — no async clock chasing.
struct UpResampler {          // device rate -> 48 kHz (push each input, emit N)
    double step_ = 1.0, phase_ = 0.0;
    float xm_ = 0, x0_ = 0, x1_ = 0, x2_ = 0;
    void config(double devRate) { step_ = devRate / 48000.0; phase_ = 0.0; xm_ = x0_ = x1_ = x2_ = 0; }
    template <class Ring> void push(float xin, Ring& ring) {
        xm_ = x0_; x0_ = x1_; x1_ = x2_; x2_ = xin;
        while (phase_ < 1.0) { ring.push(hermite4(xm_, x0_, x1_, x2_, phase_)); phase_ += step_; }
        phase_ -= 1.0;
    }
};
struct DownResampler {        // 48 kHz -> device rate (pull each output frame)
    double step_ = 1.0, phase_ = 0.0;
    float lm_ = 0, l0_ = 0, l1_ = 0, l2_ = 0, rm_ = 0, r0_ = 0, r1_ = 0, r2_ = 0;
    void config(double devRate) { step_ = 48000.0 / devRate; phase_ = 0.0;
        lm_ = l0_ = l1_ = l2_ = rm_ = r0_ = r1_ = r2_ = 0; }
    template <class Ring> void produce(float& oL, float& oR, Ring& ring) {
        while (phase_ >= 1.0) {
            float nl = 0, nr = 0;
            if (ring.avail() >= 2) { nl = ring.pop(); nr = ring.pop(); }
            lm_ = l0_; l0_ = l1_; l1_ = l2_; l2_ = nl;
            rm_ = r0_; r0_ = r1_; r1_ = r2_; r2_ = nr;
            phase_ -= 1.0;
        }
        oL = hermite4(lm_, l0_, l1_, l2_, phase_);
        oR = hermite4(rm_, r0_, r1_, r2_, phase_);
        phase_ += step_;
    }
};

struct AsioCtx {
    Engine* e = nullptr;
    int inCh = 1;         // interleaved input stride
    int chSel = 0;        // which channel carries the guitar (Solo: ch1 = the
                          // 1/4" instrument jack, ch0 = the XLR mic input)
    int devRate = 48000;  // ASIO device rate; 48000 = no resampling (direct path)
    UpResampler inRes;    // device -> 48 kHz (only used when devRate != 48000)
    DownResampler outRes; // 48 kHz -> device
};

inline int asioCallback(void* pOut, void* pIn, unsigned nFrames, double,
                        RtAudioStreamStatus, void* user) {
    _mm_setcsr(_mm_getcsr() | 0x8040);   // FTZ/DAZ (see engine_core)
    auto* ctx = static_cast<AsioCtx*>(user);
    auto* e = ctx->e;
    auto* out = static_cast<float*>(pOut);
    auto* in = static_cast<float*>(pIn);
    std::lock_guard<std::mutex> lk(e->mtx);
    if (ctx->devRate == 48000)
        for (unsigned i = 0; i < nFrames; ++i)
            e->inRing.push(in ? in[i * ctx->inCh + ctx->chSel] : 0.0f);
    else
        for (unsigned i = 0; i < nFrames; ++i)     // resample device rate -> 48 kHz
            ctx->inRes.push(in ? in[i * ctx->inCh + ctx->chSel] : 0.0f, e->inRing);
    int chunksDone = 0;
    auto t0 = std::chrono::steady_clock::now();
    while (e->inRing.avail() >= kChunk48) {
        float ci[kChunk48], cL[kChunk48], cR[kChunk48];
        for (int i = 0; i < kChunk48; ++i) ci[i] = e->inRing.pop();
        e->processChunk(ci, cL, cR);
        for (int i = 0; i < kChunk48; ++i) {   // interleaved stereo
            e->outRing.push(cL[i]);
            e->outRing.push(cR[i]);
        }
        ++chunksDone;
    }
    if (chunksDone > 0) {
        double sec = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - t0).count();
        int pct = static_cast<int>(100.0 * sec
                                   / (chunksDone * kChunk48 / 48000.0));
        e->loadPct.store((e->loadPct.load() * 7 + pct) / 8);
    }
    if (e->inRing.avail() > 2 * kChunk48) {
        e->inRing.drop(e->inRing.avail() - kChunk48);
        e->drops.fetch_add(1);
    }
    if (e->outRing.avail() > 12 * kChunk48)
        e->outRing.drop(e->outRing.avail() - 4 * kChunk48);
    if (ctx->devRate == 48000) {
        for (unsigned i = 0; i < nFrames; ++i) {
            float l = 0, r = 0;
            if (e->outRing.avail() >= 2) { l = e->outRing.pop(); r = e->outRing.pop(); }
            out[2 * i] = l;
            out[2 * i + 1] = r;
        }
    } else {
        for (unsigned i = 0; i < nFrames; ++i) {   // resample 48 kHz -> device rate
            float l, r;
            ctx->outRes.produce(l, r, e->outRing);
            out[2 * i] = l;
            out[2 * i + 1] = r;
        }
    }
    return 0;
}

struct AsioIO {
    RtAudio rt{RtAudio::WINDOWS_ASIO};
    std::vector<unsigned> ids;
    std::vector<std::string> names;
    AsioCtx ctx;

    void refresh() {
        ids.clear();
        names.clear();
        for (unsigned id : rt.getDeviceIds()) {
            RtAudio::DeviceInfo di = rt.getDeviceInfo(id);
            ids.push_back(id);
            names.push_back(di.name);
        }
    }

    bool start(int idx, unsigned period, Engine* e, char* info, size_t len,
               int chSel = 0, unsigned devRate = 48000) {
        if (ids.empty()) {
            if (info) std::snprintf(info, len, "no ASIO drivers");
            return false;
        }
        unsigned id = (idx >= 0 && idx < static_cast<int>(ids.size()))
                          ? ids[idx] : ids[0];
        RtAudio::DeviceInfo di = rt.getDeviceInfo(id);
        // some drivers behave oddly when opening fewer input channels than
        // the hardware exposes; open up to 2, pick the requested channel
        int inCh = di.inputChannels >= 2 ? 2 : 1;
        if (chSel >= inCh) chSel = inCh - 1;
        ctx = {e, inCh, chSel};
        if (devRate != 48000 && devRate != 44100) devRate = 48000;
        ctx.devRate = static_cast<int>(devRate);
        ctx.inRes.config(devRate);      // device -> 48 kHz engine
        ctx.outRes.config(devRate);     // 48 kHz engine -> device
        RtAudio::StreamParameters ip, op;
        ip.deviceId = id; ip.nChannels = inCh; ip.firstChannel = 0;
        op.deviceId = id; op.nChannels = 2; op.firstChannel = 0;
        unsigned bf = period;
        if (rt.openStream(&op, &ip, RTAUDIO_FLOAT32, devRate, &bf,
                          &asioCallback, &ctx) != RTAUDIO_NO_ERROR) {
            if (info)
                std::snprintf(info, len, "ASIO open failed: %s",
                              rt.getErrorText().c_str());
            return false;
        }
        if (rt.startStream() != RTAUDIO_NO_ERROR) {
            if (info)
                std::snprintf(info, len, "ASIO start failed: %s",
                              rt.getErrorText().c_str());
            rt.closeStream();
            return false;
        }
        if (info)
            std::snprintf(info, len,
                          "ASIO %u Hz%s | buffer %u frames | input ch %d/%d | "
                          "~%.1f ms round trip",
                          devRate, devRate == 48000 ? "" : " (resampled to 48k)",
                          bf, chSel + 1, inCh,
                          (2.0 * bf + kChunk48) * 1000.0 / devRate);
        return true;
    }

    void stop() {
        if (rt.isStreamOpen()) {
            if (rt.isStreamRunning()) rt.stopStream();
            rt.closeStream();
        }
    }
};

}  // namespace pa
