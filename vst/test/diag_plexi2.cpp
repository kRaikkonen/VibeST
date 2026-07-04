#include <cstdio>
#include <cmath>
#include <vector>
#include "../src/engine/plexi.hpp"
int main(){
    double fs=96000;
    for(double g:{0.1,0.4,0.7,1.0}) for(double pk:{0.03,0.1}){
        plexi::Amp a(fs,g,0.6,0.4,0.5,0.05);
        int m=(int)(0.4*fs); std::vector<double> in(m),out(m);
        for(int i=0;i<m;++i) in[i]=pk*std::sin(2*3.14159265*220*i/fs);
        a.processBlock(in.data(),out.data(),m);
        int from=m/2; auto H=[&](int k){double re=0,im=0;for(int i=from;i<m;++i){double aa=2*3.14159265*k*220*i/fs;re+=out[i]*cos(aa);im-=out[i]*sin(aa);}return std::sqrt(re*re+im*im)*2/(m-from);};
        double h1=H(1),hn=0;for(int k=2;k<=12;++k)hn+=H(k)*H(k);
        double rms=0;for(int i=from;i<m;++i)rms+=out[i]*out[i];
        std::printf("gain %.1f pick %.2f: THD %2.0f%% RMS %.3f\n",g,pk,100*std::sqrt(hn)/h1,std::sqrt(rms/(m-from)));
    }
    return 0;
}
