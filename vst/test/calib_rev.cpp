#define MA_IMPLEMENTATION
#include "../src/standalone/engine_core.hpp"
#include <cstdio>
#include <cmath>
int main(){
    auto measure = [](double retK, double pot){
        auto ir = pa::loadIr("../tank_ir_96k.f64");
        pa::Engine e(std::move(ir), {});
        e.amp.setTankReturn(retK);
        pa::Controls c; c.reverb = pot; e.apply(c);
        float in[pa::kChunk48], out[pa::kChunk48];
        double rms=0;
        for (int blk=0; blk<200; ++blk){
            for (int i=0;i<pa::kChunk48;++i)
                in[i]=0.4f*std::sin(2*3.14159265*220.0*(blk*pa::kChunk48+i)/48000.0);
            e.processChunk(in,out);
            if (blk >= 100)
                for (int i=0;i<pa::kChunk48;++i) rms+=out[i]*out[i];
        }
        return std::sqrt(rms/(100*pa::kChunk48));
    };
    double dry = measure(0.0, 0.0);
    std::printf("dry only: %.5f\n", dry);
    for (double k : {4.5e-3, 1.5e-3, 5e-4, 2e-4, 1e-4}) {
        double tot = measure(k, 1.0);
        double wet = std::sqrt(std::max(tot*tot - dry*dry, 0.0));
        std::printf("retK %.1e: pot-max total %.5f wet %.5f wet/dry %.2f\n",
                    k, tot, wet, wet/dry);
    }
    return 0;
}
