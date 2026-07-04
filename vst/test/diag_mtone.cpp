#include <cstdio>
#include <cmath>
#include "../src/engine/plexi.hpp"
int main(){
    double fs=96000;
    plexi::MarshallTone t(fs,0.6,0.4,0.5);
    double mx=0; bool fin=true;
    for(int i=0;i<48000;++i){ double y=t.step(std::sin(2*3.14159265*440*i/fs));
        if(!std::isfinite(y))fin=false; if(i>24000)mx=std::max(mx,std::fabs(y)); }
    std::printf("MarshallTone: peak %.4f finite=%d\n",mx,fin);
    // test triode stage alone at high gain
    princeton::TriodeStage v(princeton::T12AX7(),fs,320,100e3,820,330e-6,0.022e-6,1e6);
    mx=0;fin=true;
    for(int i=0;i<48000;++i){ double y=v.step(0.05*std::sin(2*3.14159265*220*i/fs));
        if(!std::isfinite(y))fin=false; if(i>24000)mx=std::max(mx,std::fabs(y)); }
    std::printf("TriodeStage(820R): peak %.3f finite=%d\n",mx,fin);
    return 0;
}
