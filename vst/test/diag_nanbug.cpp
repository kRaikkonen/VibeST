#define MA_IMPLEMENTATION
#include "../src/standalone/engine_core.hpp"
#include <cstdio>
#include <cmath>
#include <vector>
int main(){
    pa::Engine e(pa::springTankIr(96000.0),{},false);
    pa::Controls c;   // starts at the user preset (defaults)
    e.apply(c);
    std::vector<float> in(pa::kChunk48),oL(pa::kChunk48),oR(pa::kChunk48);
    unsigned seed=99; auto rnd=[&](){seed=seed*1664525+1013904223;return (seed>>8)/16777216.0;};
    int nanBlocks=0, latched=-1;
    for(int b=0;b<12000;++b){
        // brutal abuse: random vol every block, extreme drive/trim/master swings
        if(b<9000){ c.volume=rnd(); c.aDrive=rnd(); c.bDrive=rnd();
            c.inTrim=rnd(); c.master=0.001+0.15*rnd(); e.apply(c); }
        else if(b==9000){ c.volume=0.5; c.inTrim=0.3; c.master=0.03; e.apply(c); }
        // hard chord + noise
        for(int i=0;i<pa::kChunk48;++i){ double t=(b*pa::kChunk48+i)/48000.0;
            double x = 0.7*(std::sin(2*3.14159265*110*t)+std::sin(2*3.14159265*165*t)
                            +std::sin(2*3.14159265*220*t)) + 0.1*(rnd()-0.5);
            in[i]=(float)x; }
        e.processChunk(in.data(),oL.data(),oR.data());
        bool nan=false;
        for(int i=0;i<pa::kChunk48;++i) if(!std::isfinite(oL[i])||!std::isfinite(oR[i])) nan=true;
        if(nan){ ++nanBlocks; }
        if(b>10500 && nan) latched=b;   // still NaN long after abuse stopped?
    }
    std::printf("brutal stress: %d NaN blocks; after abuse stopped, latched=%s\n",
        nanBlocks, latched<0? "NO -- recovers/never NaN (GOOD)" : "YES (BUG)");
    // final sanity: is the output sane after everything?
    double rms=0; for(int i=0;i<pa::kChunk48;++i) rms+=oL[i]*oL[i];
    std::printf("final block out RMS %.4f (should be a real number)\n", std::sqrt(rms/pa::kChunk48));
    return 0;
}
