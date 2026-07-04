#include <cstdio>
#include <cmath>
#include <vector>
#include "../src/engine/od1.hpp"
int main(){
    double fs=192000; int m=(int)(0.1*fs);
    for(double dr : {0.2,0.5}){
        od1::Pedal p(fs,dr,0.8,od1::RC3403A(),od1::Params{},2);
        std::vector<double> y(m);
        for(int i=0;i<m;++i) y[i]=p.step(0.08*std::sin(2*3.14159265*220*i/fs));
        // harmonic amplitudes 1..8 via DFT on last half
        int f0=220,from=m/2,N=m-from;
        auto H=[&](int k){double re=0,im=0;for(int i=from;i<m;++i){double a=2*3.14159265*k*f0*i/fs;re+=y[i]*cos(a);im-=y[i]*sin(a);}return sqrt(re*re+im*im)*2/N;};
        double h1=H(1);
        std::printf("drive=%.1f harmonics re H1(dB): ",dr);
        for(int k=2;k<=8;++k) std::printf("H%d %+.0f ",k,20*log10(H(k)/h1+1e-9));
        std::printf("\n");
    }
    return 0;
}
