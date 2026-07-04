#include <cstdio>
#include <cmath>
#include <vector>
#include "../src/engine/plexi.hpp"
int main(){
    double fs=96000;
    for(double g:{0.3,0.6,0.9}){
        plexi::Amp a(fs,g,0.6,0.4,0.5,0.05);
        int m=(int)(0.4*fs); std::vector<double> in(m),out(m);
        for(int i=0;i<m;++i) in[i]=0.05*std::sin(2*3.14159265*220*i/fs);
        a.processBlock(in.data(),out.data(),m);
        int from=m/2,zc=0; double mean=0,ac=0,pk=0; bool fin=true;
        for(int i=from;i<m;++i){mean+=out[i]; if(!std::isfinite(out[i]))fin=false;} mean/=(m-from);
        for(int i=from+1;i<m;++i) if((out[i-1]-mean)*(out[i]-mean)<0)++zc;
        for(int i=from;i<m;++i){ac+=(out[i]-mean)*(out[i]-mean);pk=std::max(pk,std::fabs(out[i]));}
        double freq=zc*fs/(2.0*(m-from));
        auto H=[&](int k){double re=0,im=0;for(int i=from;i<m;++i){double aa=2*3.14159265*k*220*i/fs;re+=out[i]*cos(aa);im-=out[i]*sin(aa);}return std::sqrt(re*re+im*im)*2/(m-from);};
        double h1=H(1),hn=0;for(int k=2;k<=10;++k)hn+=H(k)*H(k);
        std::printf("gain %.1f: RMS %.3f peak %.3f freq ~%.0fHz THD %.0f%% finite=%d %s\n",
            g,std::sqrt(ac/(m-from)),pk,freq,100*std::sqrt(hn)/h1,fin,(freq>1000||!fin)?"BAD":"OK");
    }
    return 0;
}
