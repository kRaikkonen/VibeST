#define MA_IMPLEMENTATION
#include "../src/standalone/engine_core.hpp"
#include <cstdio>
#include <cmath>
int main(){
    for(int amp=0;amp<2;++amp) for(int pk=0;pk<4;++pk){
        pa::Engine e(pa::springTankIr(96000.0),{},false);
        pa::Controls c; c.odOn=true; c.pedalKind=pk; c.ampKind=amp;
        c.odDrive=0.6; c.volume=0.6; c.master=0.045; c.inTrim=0.4; c.reverb=0.3;
        e.apply(c);
        std::vector<float> in(pa::kChunk48),out(pa::kChunk48); double mx=0; bool fin=true;
        for(int b=0;b<600;++b){ for(int i=0;i<pa::kChunk48;++i){double t=(b*pa::kChunk48+i)/48000.0;
            in[i]=(float)(0.3*std::sin(2*3.14159265*220*t));}
            e.processChunk(in.data(),out.data());
            for(int i=0;i<pa::kChunk48;++i){ if(!std::isfinite(out[i]))fin=false; if(b>300)mx=std::max(mx,(double)std::fabs(out[i]));} }
        const char* an[]={"Princeton","Plexi"}; const char* pn[]={"OD-1","SD-1","TS-808","MadRed"};
        std::printf("%-10s + %-7s: peak %.3f finite=%d %s\n",an[amp],pn[pk],mx,fin,fin?"OK":"BAD");
    }
    return 0;
}
