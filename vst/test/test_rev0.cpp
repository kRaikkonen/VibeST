#define MA_IMPLEMENTATION
#include "../src/standalone/engine_core.hpp"
#include <cstdio>
#include <cmath>
int main(){
    auto ir = pa::loadIr("tank_ir_96k.f64");
    pa::Engine e(std::move(ir), {});
    pa::Controls c;                      // defaults, reverb 0.25
    float in[pa::kChunk48], out[pa::kChunk48];
    auto run = [&](double rev, const char* tag){
        c.reverb = rev; e.apply(c);
        double rms=0; bool fin=true;
        for (int blk=0; blk<200; ++blk){
            for (int i=0;i<pa::kChunk48;++i)
                in[i]=0.4f*std::sin(2*3.14159265*220.0*(blk*pa::kChunk48+i)/48000.0);
            e.processChunk(in,out);
            for (int i=0;i<pa::kChunk48;++i){
                rms+=out[i]*out[i];
                if(!std::isfinite(out[i])) fin=false;
            }
        }
        std::printf("%s: RMS %.5f finite=%d\n", tag,
                    std::sqrt(rms/(200*pa::kChunk48)), fin);
    };
    run(0.25, "reverb 0.25");
    run(0.0,  "reverb 0.00");
    run(0.25, "reverb back to 0.25");
    return 0;
}

