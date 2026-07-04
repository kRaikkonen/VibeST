// ASIO backend (via RtAudio) for the practice amp. ASIO is inherently
// duplex-single-device: input and output live on the same driver, which is
// exactly the Focusrite use case and the low-latency path on Windows.
#pragma once
#include "RtAudio.h"
#include "engine_core.hpp"

#include <string>
#include <vector>

namespace pa {

struct AsioCtx {
    Engine* e = nullptr;
    int inCh = 1;         // interleaved input stride
    int chSel = 0;        // which channel carries the guitar (Solo: ch1 = the
                          // 1/4" instrument jack, ch0 = the XLR mic input)
};

inline int asioCallback(void* pOut, void* pIn, unsigned nFrames, double,
                        RtAudioStreamStatus, void* user) {
    _mm_setcsr(_mm_getcsr() | 0x8040);   // FTZ/DAZ (see engine_core)
    auto* ctx = static_cast<AsioCtx*>(user);
    auto* e = ctx->e;
    auto* out = static_cast<float*>(pOut);
    auto* in = static_cast<float*>(pIn);
    std::lock_guard<std::mutex> lk(e->mtx);
    for (unsigned i = 0; i < nFrames; ++i)
        e->inRing.push(in ? in[i * ctx->inCh + ctx->chSel] : 0.0f);
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
    for (unsigned i = 0; i < nFrames; ++i) {
        float l = 0, r = 0;
        if (e->outRing.avail() >= 2) { l = e->outRing.pop(); r = e->outRing.pop(); }
        out[2 * i] = l;
        out[2 * i + 1] = r;
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
               int chSel = 0) {
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
        RtAudio::StreamParameters ip, op;
        ip.deviceId = id; ip.nChannels = inCh; ip.firstChannel = 0;
        op.deviceId = id; op.nChannels = 2; op.firstChannel = 0;
        unsigned bf = period;
        if (rt.openStream(&op, &ip, RTAUDIO_FLOAT32, 48000, &bf,
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
                          "ASIO | buffer %u frames | input ch %d/%d | "
                          "~%.1f ms round trip",
                          bf, chSel + 1, inCh,
                          (2.0 * bf + kChunk48) * 1000.0 / 48000.0);
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
