#define MA_IMPLEMENTATION
#include "../src/standalone/engine_core.hpp"
#include <cstdio>
#include <cmath>
#include <vector>
int main(){
    for(int amp=0;amp<2;++amp){
        pa::Engine e(pa::springTankIr(96000.0),{},false);
        pa::Controls c; c.aOn=true; c.bOn=true; c.aKind=1; c.bKind=2;
        c.ampKind=amp; c.cabKind=1; c.volume=0.6; c.master=0.045; c.inTrim=0.4;
        c.delayOn=true; c.delayMs=350; c.roomAmount=0.3;   // FX on
        e.apply(c);
        std::vector<float> in(pa::kChunk48),oL(pa::kChunk48),oR(pa::kChunk48);
        double dLR=0,dtot=0,mx=0; bool fin=true;
        for(int b=0;b<800;++b){ for(int i=0;i<pa::kChunk48;++i){
            double t=(b*pa::kChunk48+i)/48000.0;
            in[i]=(float)(0.3*std::sin(2*3.14159265*220*t));}
            e.processChunk(in.data(),oL.data(),oR.data());
            for(int i=0;i<pa::kChunk48;++i){ if(!std::isfinite(oL[i])||!std::isfinite(oR[i]))fin=false;
                if(b>400){double l=oL[i],r=oR[i]; dLR+=(l-r)*(l-r); dtot+=l*l+r*r;
                    mx=std::max(mx,(double)std::fabs(l));}} }
        const char* an[]={"Princeton","Plexi"};
        std::printf("%-10s SD1->TS +delay+room: peak %.3f stereoWidth %.1f%% finite=%d %s\n",
            an[amp],mx,100*dLR/(dtot+1e-30),fin,fin?"OK":"BAD");
    }
    return 0;
}
