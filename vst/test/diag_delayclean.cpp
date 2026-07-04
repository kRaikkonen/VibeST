#include <cstdio>
#include <cmath>
#include <vector>
#include "../src/engine/fx.hpp"
int main(){
    double fs=48000;
    fx::BossDelay dl; dl.init(fs); dl.setTimeMs(300); dl.setFeedback(0.4); dl.setLevel(0.6);
    // feed an amp-domain signal (~15 V peak decaying pluck) and check the
    // echo is a clean copy, not chopped/distorted
    std::vector<double> out;
    for(int i=0;i<48000;++i){
        double t=i/fs; double env = (t<0.3)? std::exp(-8.0*t) : 0.0;
        double x = 15.0 * env * std::sin(2*3.14159265*220*t);   // amp-voltage domain
        out.push_back(dl.process(x));
    }
    // the echo lands ~300ms (14400 samples) after the note. Measure its THD:
    // a clean echo of a 220Hz sine should have low added harmonics.
    int e0=14400, e1=e0+4000;
    auto H=[&](int k){double re=0,im=0;for(int i=e0;i<e1;++i){double a=2*3.14159265*k*220*i/fs;re+=out[i]*cos(a);im-=out[i]*sin(a);}return std::sqrt(re*re+im*im)*2/(e1-e0);};
    double h1=H(1),hn=0;for(int k=2;k<=10;++k)hn+=H(k)*H(k);
    double thd = h1>1e-6? std::sqrt(hn)/h1 : 0;
    std::printf("echo of a clean 220Hz sine: THD %.1f%% (want <2%% = clean copy;\n"
                "  the old >8V-clamp chopped it into >30%% garbage)\n", 100*thd);
    double pk=0; for(int i=e0;i<e1;++i) pk=std::max(pk,std::fabs(out[i]));
    std::printf("echo peak %.2f V (a real, non-zeroed repeat)\n", pk);
    return 0;
}
