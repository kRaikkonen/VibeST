// Honest diagnostic: measure the amp through the ACTUAL runtime signal path
// (generated spring IR, not the stale file), at the settings the user hits.
#define MA_IMPLEMENTATION
#include "../src/standalone/engine_core.hpp"
#include <cstdio>
#include <cmath>

static double rms(const std::vector<float>& y, int from) {
    double a = 0;
    for (int i = from; i < (int)y.size(); ++i) a += y[i] * y[i];
    return std::sqrt(a / (y.size() - from));
}
static double db(double x) { return x > 1e-9 ? 20 * std::log10(x) : -99; }

int main() {
    const int chunks = 48000 * 3 / pa::kChunk48;
    auto sweep = [&](bool odOn, double vol, double rev, double master,
                     double amp_in) {
        pa::Engine e(pa::springTankIr(), {}, false);
        pa::Controls c;
        c.odOn = odOn; c.volume = vol; c.reverb = rev; c.master = master;
        c.inTrim = 0.15;
        e.apply(c);
        std::vector<float> in(pa::kChunk48), out(pa::kChunk48), y;
        for (int cix = 0; cix < chunks; ++cix) {
            for (int i = 0; i < pa::kChunk48; ++i) {
                double t = (cix * pa::kChunk48 + i) / 48000.0;
                in[i] = (float)(amp_in * std::sin(2*3.14159265*220*t));
            }
            e.processChunk(in.data(), out.data());
            for (int i = 0; i < pa::kChunk48; ++i) y.push_back(out[i]);
        }
        return rms(y, y.size() / 3);
    };

    std::printf("=== calibrate tankRetK: dry(rev0) vs wet(rev0.5) ===\n");
    double dry = sweep(false, 0.35, 0.0, 0.12, 0.02);
    for (double k : {4e-4, 5e-5, 1e-5, 4e-6, 2e-6}) {
        pa::Engine e(pa::springTankIr(), {}, false);
        e.amp.setTankReturn(k);
        pa::Controls c; c.odOn=false; c.volume=0.35; c.reverb=0.5;
        c.master=0.12; c.inTrim=0.15;
        e.apply(c);
        std::vector<float> in(pa::kChunk48), out(pa::kChunk48), y;
        for (int cix=0; cix<48000*3/pa::kChunk48; ++cix){
            for (int i=0;i<pa::kChunk48;++i){ double t=(cix*pa::kChunk48+i)/48000.0;
                in[i]=(float)(0.02*std::sin(2*3.14159265*220*t)); }
            e.processChunk(in.data(), out.data());
            for (int i=0;i<pa::kChunk48;++i) y.push_back(out[i]);
        }
        double tot=rms(y, y.size()/3);
        double wet=std::sqrt(std::max(tot*tot-dry*dry,0.0));
        std::printf("  retK %.1e: total %.4f wet/dry %.2f\n", k, tot,
                    dry>1e-9? wet/dry : 0);
    }
    std::printf("=== clean headroom: out RMS vs input (rev 0, vol 0.35) ===\n");
    std::printf("    (1.0 = full-scale clip; want < ~0.5 for clean)\n");
    for (double ai : {0.005, 0.01, 0.02, 0.05, 0.1}) {
        double v = sweep(false, 0.35, 0.0, 0.12, ai);
        std::printf("  in %.3f -> out RMS %.4f (%.1f dB)\n", ai, v, db(v));
    }
    return 0;
}
