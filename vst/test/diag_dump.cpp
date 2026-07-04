#define MA_IMPLEMENTATION
#include "../src/standalone/engine_core.hpp"
#include <cstdio>
#include <cmath>
int main() {
    pa::Engine e(pa::springTankIr(), {}, false);
    pa::Controls c; c.odOn=false; c.volume=0.35; c.reverb=0.0;
    c.master=1.0; c.inTrim=1.0;
    e.apply(c);
    std::vector<float> in(pa::kChunk48), out(pa::kChunk48);
    for (int cix=0; cix<2000; ++cix) {
        for (int i=0;i<pa::kChunk48;++i){
            double t=(cix*pa::kChunk48+i)/48000.0;
            in[i]=(float)(0.015*std::sin(2*3.14159265*220*t));
        }
        e.processChunk(in.data(), out.data());
        if (e.dbgCap.size() >= 8192) break;
    }
    FILE* f=std::fopen("y48_capture.f64","wb");
    std::fwrite(e.dbgCap.data(), 8, e.dbgCap.size(), f);
    std::fclose(f);
    std::printf("dumped %zu y48 samples @48k\n", e.dbgCap.size());
    return 0;
}
