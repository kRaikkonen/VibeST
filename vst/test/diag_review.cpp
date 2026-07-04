#define MA_IMPLEMENTATION
#include "../src/standalone/engine_core.hpp"
#include <cstdio>
#include <cmath>
#include <vector>
// (a) aliasing probe: high note, cranked, HQ(96k amp) vs ECO(48k amp)
static void aliasTest(bool eco){
    pa::Engine e(pa::springTankIr(eco?48000.0:96000.0),{},eco);
    pa::Controls c; c.aOn=false; c.volume=0.9; c.reverb=0; c.master=0.045;
    c.inTrim=0.6; c.cabKind=1; e.apply(c);
    std::vector<float> in(pa::kChunk48),out(pa::kChunk48); std::vector<double> y;
    double f0=1175.0;  // D6, high fret
    for(int b=0;b<48000*2/pa::kChunk48;++b){ for(int i=0;i<pa::kChunk48;++i){
        double t=(b*pa::kChunk48+i)/48000.0; in[i]=(float)(0.5*std::sin(2*3.14159265*f0*t));}
        e.processChunk(in.data(),out.data());
        for(int i=0;i<pa::kChunk48;++i) y.push_back(out[i]); }
    int n=(int)y.size()/2; std::vector<double> seg(y.begin()+n,y.end());
    // DFT bins: harmonic vs inharmonic energy above -60dB
    int N=(int)seg.size(); double inh=0,tot=0;
    for(int k=1;k<N/2;++k){ double f=48000.0*k/N; if(f<100||f>20000)continue;
        double re=0,im=0; for(int i=0;i<N;i+=4){double a=2*3.14159265*f*i/48000.0;
            re+=seg[i]*cos(a); im-=seg[i]*sin(a);} double p=re*re+im*im;
        double h=f/f0; bool harm=std::fabs(h-std::round(h))<0.03;
        tot+=p; if(!harm)inh+=p; }
    std::printf("(a) alias %s: inharmonic energy %.2f%% of total (100Hz-20k)\n",
        eco?"ECO(amp48k)":"HQ (amp96k)", 100.0*inh/(tot+1e-30));
}
int main(){
    aliasTest(false); aliasTest(true);
    // (b) cab voicing response
    for(int cab=0;cab<3;++cab){
        std::printf("(b) cab %d: ",cab);
        for(double f:{100.0,300.0,1000.0,2500.0,4000.0,6000.0}){
            dsp::Biquad hp,bu,pr,l1,l2;
            // replicate rebuildCab
            if(cab==1){hp.highpass(75,0.707,48000);bu.peaking(120,1.2,2.5,48000);
                pr.peaking(2400,1.4,3.0,48000);l1.lowpass(5200,0.75,48000);l2.lowpass(5200,0.75,48000);}
            else if(cab==2){hp.highpass(65,0.707,48000);bu.peaking(100,1.0,5.0,48000);
                pr.peaking(2300,1.4,3.0,48000);l1.lowpass(4600,0.75,48000);l2.lowpass(4600,0.75,48000);}
            else{hp.highpass(80,0.707,48000);bu.peaking(110,1.1,0.0,48000);
                pr.peaking(3200,1.4,-4.0,48000);l1.lowpass(4300,0.707,48000);l2.lowpass(4300,0.707,48000);}
            int n=9600; double rin=0,rout=0;
            for(int i=0;i<n;++i){ double x=std::sin(2*3.14159265*f*i/48000.0);
                double yy=l2.step(l1.step(pr.step(bu.step(hp.step(x)))));
                if(i>n/2){rin+=x*x;rout+=yy*yy;} }
            std::printf("%.0fHz %+.1fdB  ",f,10*std::log10(rout/rin));
        }
        std::printf("\n");
    }
    return 0;
}
