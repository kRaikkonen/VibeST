// Engine-level: with OD-1 engaged at high drive, is the output sane
// (not collapsed, finite, dominant at 220 Hz)?
#define MA_IMPLEMENTATION
#include "../src/standalone/engine_core.hpp"
#include <cstdio>
#include <cmath>
#include <vector>

static void run(bool eco, double drive) {
    pa::Engine e(pa::springTankIr(eco ? 48000.0 : 96000.0), {}, eco);
    pa::Controls c; c.odOn = true; c.odDrive = drive; c.odLevel = 0.8;
    c.volume = 0.35; c.reverb = 0.0; c.master = 0.5; c.inTrim = 0.15;
    e.apply(c);
    std::vector<float> in(pa::kChunk48), out(pa::kChunk48), y;
    for (int cix = 0; cix < 48000 / pa::kChunk48; ++cix) {
        for (int i = 0; i < pa::kChunk48; ++i) {
            double t = (cix * pa::kChunk48 + i) / 48000.0;
            in[i] = (float)(0.3 * std::sin(2 * 3.14159265 * 220 * t));
        }
        e.processChunk(in.data(), out.data());
        for (int i = 0; i < pa::kChunk48; ++i) y.push_back(out[i]);
    }
    int from = y.size()/2, zc=0; bool fin=true;
    double mean=0; for(size_t i=from;i<y.size();++i){mean+=y[i]; if(!std::isfinite(y[i]))fin=false;}
    mean/=(y.size()-from);
    for(size_t i=from+1;i<y.size();++i) if((y[i-1]-mean)*(y[i]-mean)<0)++zc;
    double freq=zc*48000.0/(2.0*(y.size()-from));
    double ac=0; for(size_t i=from;i<y.size();++i)ac+=(y[i]-mean)*(y[i]-mean);
    std::printf("  %s drive=%.1f: out RMS %.4f  freq ~%.0f Hz  finite=%d  %s\n",
                eco?"ECO":"HQ ", drive, std::sqrt(ac/(y.size()-from)), freq, fin,
                (freq>1000||!fin)?"<-- BAD":"OK");
}

#include <chrono>
static void bench(bool eco, bool od) {
    pa::Engine e(pa::springTankIr(eco ? 48000.0 : 96000.0), {}, eco);
    pa::Controls c; c.odOn = od; c.odDrive = 0.6; c.volume = 0.4;
    e.apply(c);
    int chunks = 48000 * 2 / pa::kChunk48;
    std::vector<float> in(pa::kChunk48), out(pa::kChunk48);
    for (int i = 0; i < pa::kChunk48; ++i) in[i] = 0.1f;
    auto t0 = std::chrono::high_resolution_clock::now();
    for (int cix = 0; cix < chunks; ++cix) e.processChunk(in.data(), out.data());
    double sec = std::chrono::duration<double>(
        std::chrono::high_resolution_clock::now() - t0).count();
    std::printf("  %s OD-1 %s: %.1f%% core (%.2fx realtime)\n",
                eco ? "ECO" : "HQ ", od ? "ON " : "off",
                sec / 2.0 * 100.0, 2.0 / sec);
}

int main() {
    std::printf("engine with OD-1 engaged (correctness):\n");
    for (double d : {0.0, 0.5, 1.0}) run(false, d);
    std::printf("CPU cost:\n");
    bench(false, false); bench(false, true);
    bench(true, false); bench(true, true);
    return 0;
}
