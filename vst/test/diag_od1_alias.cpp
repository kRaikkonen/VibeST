// Capture engine output with OD-1 on, dump for spectral analysis.
// "two sounds / broken speaker" = classic aliasing signature.
#define MA_IMPLEMENTATION
#include "../src/standalone/engine_core.hpp"
#include <cstdio>
#include <cmath>
#include <vector>

int main(int argc, char** argv) {
    bool eco = argc > 1 && std::string(argv[1]) == "eco";
    pa::Engine e(pa::springTankIr(eco ? 48000.0 : 96000.0), {}, eco);
    bool od = !(argc > 2 && std::string(argv[2]) == "clean");
    pa::Controls c;
    c.odOn = od; c.odDrive = 0.4; c.odLevel = 0.35;   // defaults
    c.volume = 0.4; c.reverb = 0.0; c.master = 0.5; c.inTrim = 0.15;
    e.apply(c);
    std::vector<float> in(pa::kChunk48), out(pa::kChunk48);
    std::vector<double> y;
    for (int cix = 0; cix < 48000 * 2 / pa::kChunk48; ++cix) {
        for (int i = 0; i < pa::kChunk48; ++i) {
            double t = (cix * pa::kChunk48 + i) / 48000.0;
            in[i] = (float)(0.25 * std::sin(2 * 3.14159265 * 220 * t));
        }
        e.processChunk(in.data(), out.data());
        for (int i = 0; i < pa::kChunk48; ++i) y.push_back(out[i]);
    }
    double m=0,mx=0,rms=0; int n=y.size()/2;
    for(int i=n;i<(int)y.size();++i){double v=std::fabs(y[i]);m+=v;mx=std::max(mx,v);rms+=y[i]*y[i];}
    std::printf("  %s OD-1 %s: out RMS %.3f mean|.| %.3f peak %.3f %s\n",
        eco?"ECO":"HQ ", od?"ON ":"off", std::sqrt(rms/(y.size()-n)), m/(y.size()-n), mx,
        mx>0.98? "<-- pinned/clipping":"");
    FILE* f = std::fopen(eco ? "eng_od1_eco.f64" : "eng_od1_hq.f64", "wb");
    std::fwrite(y.data() + y.size()/2, 8, y.size()/2, f);
    std::fclose(f);
    std::printf("dumped %zu samples (odOn, drive 0.4)\n", y.size()/2);
    return 0;
}
