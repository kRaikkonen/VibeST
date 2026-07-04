#define MA_IMPLEMENTATION
#include "../src/standalone/engine_core.hpp"
#include <cstdio>
#include <cmath>
#include <vector>
int main(){
    pa::Engine e(pa::springTankIr(96000.0),{},false);
    pa::Controls c;   // default preset
    e.apply(c);
    std::vector<float> in(pa::kChunk48),oL(pa::kChunk48),oR(pa::kChunk48);
    double rms=0,dLR=0,dtot=0,mx=0; bool fin=true;
    int n=48000*2/pa::kChunk48;
    for(int b=0;b<n;++b){
        for(int i=0;i<pa::kChunk48;++i){ double t=(b*pa::kChunk48+i)/48000.0;
            // realistic pluck: decaying, medium level
            double env=std::exp(-2.0*std::fmod(t,0.8));
            in[i]=(float)(0.4*env*std::sin(2*3.14159265*196*t)); }
        e.processChunk(in.data(),oL.data(),oR.data());
        for(int i=0;i<pa::kChunk48;++i){ if(!std::isfinite(oL[i]))fin=false;
            if(b>n/4){rms+=oL[i]*oL[i]; dLR+=(oL[i]-oR[i])*(oL[i]-oR[i]);
                dtot+=oL[i]*oL[i]+oR[i]*oR[i]; mx=std::max(mx,(double)std::fabs(oL[i]));}} }
    int cnt=(n-n/4)*pa::kChunk48;
    std::printf("PRESET clean pluck: out RMS %.4f peak %.3f stereoWidth %.1f%% finite=%d\n",
        std::sqrt(rms/cnt),mx,100*dLR/(dtot+1e-30),fin);
    std::printf("  -> %s\n", (std::sqrt(rms/cnt)>0.005 && fin)? "AUDIBLE, OK" : "TOO QUIET / BROKEN");
    return 0;
}
