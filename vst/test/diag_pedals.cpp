#include <cstdio>
#include <cmath>
#include <vector>
#include "../src/engine/pedals.hpp"
static double thd(pedals::Pedal& p, double amp, double fs){
    int m=(int)(0.1*fs); std::vector<double> y(m);
    for(int i=0;i<m;++i) y[i]=p.step(amp*std::sin(2*3.14159265*220*i/fs));
    int from=m/2,N=m-from; auto H=[&](int k){double re=0,im=0;for(int i=from;i<m;++i){double a=2*3.14159265*k*220*i/fs;re+=y[i]*cos(a);im-=y[i]*sin(a);}return std::sqrt(re*re+im*im)*2/N;};
    double h1=H(1),hn=0,he=0,ho=0;for(int k=2;k<=10;++k){double h=H(k);hn+=h*h;if(k%2)ho+=h*h;else he+=h*h;}
    std::printf(" THD %2.0f%% (even %2.0f%% odd %2.0f%%) out %.3f",100*std::sqrt(hn)/h1,100*std::sqrt(he)/h1,100*std::sqrt(ho)/h1,H(1));
    return std::sqrt(hn)/h1;
}
int main(){
    double fs=192000;
    const char* nm[]={"SD-1","TS-808","MadRed"};
    pedals::Kind ks[]={pedals::Kind::SD1,pedals::Kind::TS808,pedals::Kind::MadRed};
    for(int j=0;j<3;++j){ for(double dr:{0.3,0.7}){
        pedals::Pedal p(ks[j],fs,dr,0.7,0.5,2);
        std::printf("%-7s drive %.1f 160mV:",nm[j],dr); thd(p,0.16,fs); std::printf("\n");
    }}
    return 0;
}
