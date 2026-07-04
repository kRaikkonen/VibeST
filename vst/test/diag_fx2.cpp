#include <cstdio>
#include <cmath>
#include <vector>
#include "../src/engine/fx.hpp"
int main(){
    double fs=48000;
    // delay: E.Level 0.6, feedback 0.35 -> echoes should be clearly audible
    fx::BossDelay dl; dl.init(fs); dl.setTimeMs(300); dl.setFeedback(0.35);
    dl.setLevel(0.6);
    double echoPk=0;
    for(int i=0;i<30000;++i){ double x=(i==0)?1.0:0.0; double y=dl.process(x);
        if(i>1000) echoPk=std::max(echoPk,std::fabs(y)); }
    std::printf("delay: first echo peak %.3f (was ~0.25 w/ old mix; want ~0.6)\n",echoPk);
    // chorus: check the modulation isn't extreme (no 'spring' = pitch swing < ~5%%)
    fx::CE2Chorus ch; ch.init(fs); ch.setRate(0.4); ch.setDepth(0.55); ch.setMix(1.0);
    // measure how far a steady tone's phase wanders (proxy for pitch warble)
    std::vector<double> y(24000);
    for(int i=0;i<24000;++i) y[i]=ch.process(std::sin(2*3.14159265*440*i/fs));
    // zero-cross count -> effective freq; deviation from 440 = warble depth
    int zc=0; for(int i=12001;i<24000;++i) if(y[i-1]*y[i]<0)++zc;
    double f=zc*fs/(2.0*(24000-12001));
    std::printf("chorus: 440Hz in -> effective %.0fHz (small deviation = smooth, not spring)\n",f);
    double rms=0; for(int i=12000;i<24000;++i) rms+=y[i]*y[i];
    std::printf("chorus: output RMS %.3f (dry sine RMS ~0.707, chorus ~similar = audible not gone)\n",std::sqrt(rms/12000));
    // noise gate: below threshold -> silence, above -> pass
    fx::NoiseGate g; g.init(fs); g.setThreshold(0.5);   // ~-45dBFS
    double loud=0,quiet=0;
    for(int i=0;i<24000;++i){ double x=0.3*std::sin(2*3.14159265*220*i/fs);
        double o=g.process(x); if(i>12000)loud+=o*o; }
    for(int i=0;i<24000;++i){ double x=0.001*std::sin(2*3.14159265*220*i/fs);
        double o=g.process(x); if(i>12000)quiet+=o*o; }
    std::printf("gate thr=0.5: loud sig passes RMS %.4f, quiet sig gated to %.6f\n",
        std::sqrt(loud/12000),std::sqrt(quiet/12000));
    return 0;
}
