#define MA_IMPLEMENTATION
#include "../src/standalone/engine_core.hpp"
#include <cstdio>
#include <cmath>
int main(){
    // stacked pedals (A=SD1 -> B=TS808) into each amp, each cab
    for(int amp=0;amp<2;++amp) for(int cab=0;cab<3;++cab){
        pa::Engine e(pa::springTankIr(96000.0),{},false);
        pa::Controls c; c.aOn=true; c.bOn=true; c.aKind=1; c.bKind=2;
        c.ampKind=amp; c.cabKind=cab; c.volume=0.6;
        c.tremSpeed=0.7; c.tremIntensity=0.5;   // Plexi: presence/bright
        c.master=0.045; c.inTrim=0.4; c.reverb=0.4;
        e.apply(c);
        std::vector<float> in(pa::kChunk48),out(pa::kChunk48);
        double mx=0; bool fin=true;
        for(int b=0;b<400;++b){ for(int i=0;i<pa::kChunk48;++i){
            double t=(b*pa::kChunk48+i)/48000.0;
            in[i]=(float)(0.3*std::sin(2*3.14159265*220*t));}
            e.processChunk(in.data(),out.data());
            for(int i=0;i<pa::kChunk48;++i){ if(!std::isfinite(out[i]))fin=false;
                if(b>200)mx=std::max(mx,(double)std::fabs(out[i]));} }
        const char* an[]={"Princeton","Plexi"};
        const char* cn[]={"C10R","GB212","GB412"};
        std::printf("%-9s %-5s stacked SD1->TS: peak %.3f %s\n",
                    an[amp],cn[cab],mx,fin?"OK":"BAD");
    }
    return 0;
}
