#include <cstdio>
#include <cmath>
#include <vector>
#include "../src/engine/fx.hpp"
int main(){
    double fs=48000;
    auto resp=[&](double f){ int n=9600; double ri=0,ro=0;
        fx::GraphicEQ e2; e2.init(fs); e2.setGainDb(4,6.0);
        for(int i=0;i<n;++i){double x=std::sin(2*3.14159265*f*i/fs);
            double y=e2.step(0,x); if(i>n/2){ri+=x*x;ro+=y*y;}} return 10*std::log10(ro/ri); };
    std::printf("(1) GEQ +6dB@800Hz: ");
    for(double f:{400.0,800.0,1500.0}) std::printf("%.0fHz %+.1fdB ",f,resp(f));
    std::printf("\n");
    fx::BossDelay dl; dl.init(fs); dl.setTimeMs(300); dl.setFeedback(0.5); dl.setMix(0.5);
    int echoAt=-1;
    for(int i=0;i<48000;++i){ double x=(i==0)?1.0:0.0; double y=dl.process(x);
        if(i>100 && std::fabs(y)>0.1 && echoAt<0) echoAt=i; }
    std::printf("(2) delay first echo at %d samples (want ~%d for 300ms)\n",echoAt,(int)(0.3*fs));
    fx::RoomMic rm; rm.init(fs); rm.setAmount(0.5); rm.setWidth(0.9);
    double dL=0,dR=0;
    for(int i=0;i<24000;++i){ double x=std::sin(2*3.14159265*220*i/fs); double l,r;
        rm.process(x,l,r); if(i>12000){dL+=(l-r)*(l-r);dR+=l*l+r*r;} }
    std::printf("(3) room mic stereo width: L-R energy %.1f%% of total (0=mono)\n",100*dL/dR);
    return 0;
}
