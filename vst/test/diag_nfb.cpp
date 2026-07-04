// NFB loop stability probe (signal-independent).
// The comment in princeton.hpp claims the closed loop oscillates for feedback
// cutoff > ~300 Hz, forcing the 250 Hz compromise. But a real-program sweep
// showed stability to >12 kHz. Resolve it definitively: drive the amp with a
// HARD burst then go silent, and watch the silent tail. Stable -> the tail
// decays to ~0. Oscillating -> the tail sustains / grows a tone near fs/6.
// Test at several feedback cutoffs, both HQ (96k) and eco (48k), cranked.
#define MA_IMPLEMENTATION
#include "../src/standalone/engine_core.hpp"
#include <cstdio>
#include <vector>
#include <cmath>

static void probe(bool eco, double fc) {
    pa::Engine eng(pa::springTankIr(eco ? 48000.0 : 96000.0), {}, eco);
    pa::Controls c = eng.ctl;
    c.inTrim = 0.6;          // crank the input hard to stress the loop
    c.volume = 0.95;         // amp volume high
    c.delayOn = false; c.roomOn = false; c.chorusOn = false;  // isolate the amp
    c.eqOn = false;
    eng.apply(c);
    eng.amp.setFbCutoff(fc);

    const double fs = 48000;
    int nBurst = (int)(0.10 * fs), nTail = (int)(0.90 * fs);
    std::vector<float> out; out.reserve(nBurst + nTail);
    float cL[pa::kChunk48], cR[pa::kChunk48];
    float in[pa::kChunk48];
    int total = nBurst + nTail, done = 0;
    // burst = loud pick-like decaying 165 Hz (low E-ish) + harmonics
    while (done < total) {
        for (int j = 0; j < pa::kChunk48; ++j) {
            int i = done + j;
            double s = 0;
            if (i < nBurst) {
                double t = i / fs;
                double env = std::exp(-6.0 * t);
                s = 0.9 * env * (std::sin(2*3.14159265358979323846*165*t) + 0.5*std::sin(2*3.14159265358979323846*330*t));
            }
            in[j] = (float)s;
        }
        eng.processChunk(in, cL, cR);
        for (int j = 0; j < pa::kChunk48; ++j) out.push_back(cL[j]);
        done += pa::kChunk48;
    }
    // burst-window peak vs tail (last 0.3 s) peak/RMS
    double burstPk = 0, tailPk = 0, tailRms = 0; int t0 = (int)(0.6 * fs);
    for (int i = 0; i < nBurst; ++i) burstPk = std::max(burstPk, (double)std::fabs(out[i]));
    int nt = 0;
    for (int i = t0; i < (int)out.size(); ++i) {
        tailPk = std::max(tailPk, (double)std::fabs(out[i]));
        tailRms += (double)out[i]*out[i]; ++nt;
    }
    tailRms = std::sqrt(tailRms / std::max(1, nt));
    // also: does the very-late tail (last 0.1s) exceed the mid tail? growth = unstable
    double lateRms = 0; int nl = 0; int l0 = (int)(0.85 * fs);
    for (int i = l0; i < (int)out.size(); ++i) { lateRms += (double)out[i]*out[i]; ++nl; }
    lateRms = std::sqrt(lateRms / std::max(1, nl));
    bool finite = std::isfinite(tailRms) && std::isfinite(tailPk);
    std::printf("%s fc=%6.0f Hz | burstPk %.3f | tailPk %.5f | tailRMS %.6f | lateRMS %.6f | %s%s\n",
                eco ? "ECO" : "HQ ", fc, burstPk, tailPk, tailRms, lateRms,
                (lateRms > tailRms * 1.2) ? "GROWING(unstable!)" : "decaying(stable)",
                finite ? "" : " NONFINITE!");
}

int main() {
    std::printf("NFB tail-decay stability probe (hard burst -> silence):\n");
    for (bool eco : {false, true})
        for (double fc : {250.0, 1000.0, 2000.0, 5000.0, 8000.0, 20000.0})
            probe(eco, fc);
    return 0;
}
