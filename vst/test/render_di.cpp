// Offline DI render + measurement bench (rebuilt to the measurement-bench
// subskill's discipline: ISOLATE the device under test, LOUDNESS-NORMALISE
// before any A/B, and let per-change toggles reconstruct old vs new so a
// comparison isn't confounded by loudness or by the pedals/delay/EQ/room the
// amp change was accidentally measured *through*).
//
//   render_di <in.wav> <out.wav> [key=value ...]
// keys (all optional):
//   start=30 dur=20      slice seconds
//   amp=0                0 Princeton, 1 Plexi
//   eco=0                1 = amp at 48k
//   pedals=1             0 = bypass BOTH pedal slots (hear the amp, not the OD)
//   postfx=1             0 = bypass delay+room+EQ+chorus (isolate amp+cab)
//   grid=1               0 = disable grid-conduction/blocking (Princeton)
//   cabir=1              0 = biquad cab voicing instead of the IR
//   nfb=0                >0 = Princeton feedback-LP cutoff Hz (0 = default 8k)
//   master=0             >0 = master override (0 = preset); irrelevant if norm>0
//   plexinfb=-0.08       Plexi LTP NFB scale
//   norm=0               >0 = normalise output to this RMS (loudness-fair A/B)
#define MA_IMPLEMENTATION
#include "../src/standalone/engine_core.hpp"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <cmath>

static std::vector<float> readWavMono(const char* path, int& srOut) {
    std::vector<float> out;
    FILE* f = std::fopen(path, "rb");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", path); return out; }
    std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> b(n);
    if (std::fread(b.data(), 1, n, f) != (size_t)n) { std::fclose(f); return out; }
    std::fclose(f);
    auto u16=[&](size_t o){return (uint16_t)(b[o]|(b[o+1]<<8));};
    auto u32=[&](size_t o){return (uint32_t)(b[o]|(b[o+1]<<8)|(b[o+2]<<16)|(b[o+3]<<24));};
    if (n<12||b[0]!='R'||b[1]!='I'||b[2]!='F'||b[3]!='F') return out;
    int ch=1,sr=44100,bits=16,fmt=1; size_t dataOff=0,dataLen=0,p=12;
    while (p+8<=(size_t)n){ uint32_t id=u32(p),sz=u32(p+4); size_t body=p+8;
        if(id==0x20746d66){fmt=u16(body);ch=u16(body+2);sr=u32(body+4);bits=u16(body+14);}
        else if(id==0x61746164){dataOff=body;dataLen=sz;break;}
        p=body+sz+(sz&1);}
    srOut=sr; if(!dataOff) return out;
    if(dataOff+dataLen>(size_t)n) dataLen=n-dataOff;
    int bp=bits/8; size_t frames=dataLen/(bp*ch); out.reserve(frames);
    for(size_t i=0;i<frames;++i){ double acc=0;
        for(int c=0;c<ch;++c){ size_t o=dataOff+(i*ch+c)*bp; double s=0;
            if(fmt==3&&bits==32){float v;std::memcpy(&v,&b[o],4);s=v;}
            else if(bits==16){int16_t v=(int16_t)u16(o);s=v/32768.0;}
            else if(bits==24){int32_t v=(b[o]|(b[o+1]<<8)|(b[o+2]<<16));if(v&0x800000)v|=~0xFFFFFF;s=v/8388608.0;}
            else if(bits==32){int32_t v=(int32_t)u32(o);s=v/2147483648.0;}
            acc+=s; }
        out.push_back((float)(acc/ch)); }
    return out;
}
static std::vector<float> resample(const std::vector<float>& x,int a,int b){
    if(a==b) return x; double r=(double)a/b; size_t m=(size_t)(x.size()/r);
    std::vector<float> y(m);
    for(size_t i=0;i<m;++i){ double t=i*r; size_t i0=(size_t)t; double fr=t-i0;
        float u=x[i0], v=(i0+1<x.size())?x[i0+1]:u; y[i]=(float)(u+(v-u)*fr);} return y;
}
static void writeWav(const char* path,const std::vector<float>&L,const std::vector<float>&R,int sr){
    size_t n=std::min(L.size(),R.size()); uint32_t dl=(uint32_t)(n*2*4);
    FILE* f=std::fopen(path,"wb");
    auto w32=[&](uint32_t v){uint8_t d[4]={(uint8_t)v,(uint8_t)(v>>8),(uint8_t)(v>>16),(uint8_t)(v>>24)};std::fwrite(d,1,4,f);};
    auto w16=[&](uint16_t v){uint8_t d[2]={(uint8_t)v,(uint8_t)(v>>8)};std::fwrite(d,1,2,f);};
    std::fwrite("RIFF",1,4,f);w32(36+dl);std::fwrite("WAVE",1,4,f);
    std::fwrite("fmt ",1,4,f);w32(16);w16(3);w16(2);w32(sr);w32(sr*2*4);w16(2*4);w16(32);
    std::fwrite("data",1,4,f);w32(dl);
    for(size_t i=0;i<n;++i){float l=L[i],r=R[i];std::fwrite(&l,4,1,f);std::fwrite(&r,4,1,f);} std::fclose(f);
}
static void metrics(const char* tag,const std::vector<float>&y,int sr){
    if(y.empty())return; double rms=0,pk=0; for(float v:y){rms+=(double)v*v;pk=std::max(pk,(double)std::fabs(v));}
    rms=std::sqrt(rms/y.size()); double crest=pk>1e-9?20*std::log10(pk/rms):0;
    size_t N=32768; while(N>y.size())N>>=1; size_t off=y.size()>N?(y.size()-N)/2:0;
    std::vector<dsp::cplx> X(N);
    for(size_t i=0;i<N;++i){double w=0.5-0.5*std::cos(2*3.14159265358979*i/(N-1));X[i]=dsp::cplx(y[off+i]*w,0);}
    dsp::fft(X,false); double num=0,den=0;
    for(size_t k=1;k<N/2;++k){double m=std::abs(X[k]);double f=(double)k*sr/N;num+=f*m;den+=m;}
    std::printf("%-12s RMS %.4f  peak %.4f  crest %.1f dB  centroid %.0f Hz\n",tag,rms,pk,crest,pk>1e-9?num/den:0);
}
// flag helpers
static std::string gArgs;
static double getf(const char* k,double def){ std::string key=std::string(" ")+k+"=";
    size_t p=gArgs.find(key); if(p==std::string::npos)return def; return std::atof(gArgs.c_str()+p+key.size()); }
static std::string gets(const char* k){ std::string key=std::string(" ")+k+"=";
    size_t p=gArgs.find(key); if(p==std::string::npos)return ""; p+=key.size();
    size_t e=gArgs.find(' ',p); return gArgs.substr(p,e-p); }

int main(int argc,char**argv){
    if(argc<3){std::printf("usage: render_di <in.wav> <out.wav> [key=value ...]\n");return 1;}
    for(int i=3;i<argc;++i){gArgs+=" ";gArgs+=argv[i];} gArgs+=" ";
    // IR self-analysis: load the raw IR and report its own spectral centroid +
    // where it rolls off, to tell a genuinely-bright cab from a load/tail bug.
    std::string di_=gets("dumpir");   // write speakerIr(N) to out.wav and exit
    if(!di_.empty()){
        auto h=pa::speakerIr(std::atoi(di_.c_str()),48000.0);
        std::vector<float> hf(h.begin(),h.end());
        writeWav(argv[2],hf,hf,48000); std::printf("dumped synth IR %s (%zu)\n",di_.c_str(),h.size());
        return 0;
    }
    std::string ira=gets("iranalyze");
    if(!ira.empty()){
        std::vector<double> h;
        if(ira.rfind("synth",0)==0) h=pa::speakerIr(std::atoi(ira.c_str()+5),48000.0);
        else h=pa::loadCabWav(("renders/ir/"+ira+".wav").c_str());
        if(h.empty()){std::printf("%-10s empty\n",ira.c_str());return 1;}
        size_t N=1; while(N<h.size()*2)N<<=1;
        std::vector<dsp::cplx> H(N,dsp::cplx(0,0));
        for(size_t i=0;i<h.size();++i)H[i]=dsp::cplx(h[i],0);
        dsp::fft(H,false);
        std::vector<double> mag(N/2); double peak=0,num=0,den=0;
        for(size_t k=1;k<N/2;++k){mag[k]=std::abs(H[k]);peak=std::max(peak,mag[k]);
            double f=(double)k*48000.0/N; num+=f*mag[k]; den+=mag[k];}
        auto dbAt=[&](double f){size_t k=(size_t)(f*N/48000);return 20*std::log10(std::max(mag[k],1e-9)/peak);};
        double body=(dbAt(100)+dbAt(200)+dbAt(320))/3;   // low-end presence
        double pres=(dbAt(3000)+dbAt(4000))/2;           // 3-4k presence
        double top=(dbAt(6000)+dbAt(8000))/2;            // fizz region
        std::printf("%-8s cen %4.0f Hz | body(100-320) %+5.1f | pres(3-4k) %+5.1f | top(6-8k) %+5.1f  [%.0fms]\n",
            ira.c_str(),den>0?num/den:0,body,pres,top,h.size()*1000.0/48000);
        return 0;
    }
    double startSec=getf("start",30), durSec=getf("dur",20);
    int ampKind=(int)getf("amp",0), eco=(int)getf("eco",0);
    int pedals=(int)getf("pedals",1), postfx=(int)getf("postfx",1);
    int grid=(int)getf("grid",1), cabir=(int)getf("cabir",1);
    double nfb=getf("nfb",0), master=getf("master",0);
    double plexinfb=getf("plexinfb",-0.08), norm=getf("norm",0);

    int sr=0; std::vector<float> di=readWavMono(argv[1],sr);
    if(di.empty()){std::fprintf(stderr,"no samples\n");return 1;}
    size_t s0=(size_t)(startSec*sr), s1=std::min(di.size(),s0+(size_t)(durSec*sr));
    if(s0>=di.size())s0=0;
    std::vector<float> in=resample(std::vector<float>(di.begin()+s0,di.begin()+s1),sr,48000);

    // fair-comparison amp controls (default -1 = keep preset)
    double treble=getf("treble",-1), bass=getf("bass",-1), vol=getf("volume",-1),
           rev=getf("reverb",-1);
    int nocab=(int)getf("nocab",0), notrem=(int)getf("notrem",0);
    pa::Engine eng(pa::springTankIr(eco?48000.0:96000.0),{},eco!=0);
    pa::Controls c=eng.ctl;
    c.ampKind=ampKind;
    if(!pedals){c.aOn=false;c.bOn=false;}
    if((int)getf("gateoff",0))c.gateOn=false;
    if(!postfx){c.delayOn=false;c.roomOn=false;c.eqOn=false;c.chorusOn=false;}
    if(master>0)c.master=master;
    if(treble>=0)c.treble=treble;
    if(bass>=0)c.bass=bass;
    if(vol>=0)c.volume=vol;
    if(rev>=0)c.reverb=rev;
    if(notrem){c.tremIntensity=0;}
    double intrim=getf("intrim",-1); if(intrim>=0)c.inTrim=intrim;
    eng.apply(c);
    if(nocab)eng.bypassCab(true);
    if(nfb>0)eng.amp.setFbCutoff(nfb);
    double otd=getf("otdamp",-1);
    if(otd>=0){ if(ampKind==1)eng.plexiAmp.setOtDamp(otd); else eng.amp.setOtDamp(otd); }
    int flat=(int)getf("flatload",0);
    if(flat){ if(ampKind==1)eng.plexiAmp.setFlatLoad(true); else eng.amp.setFlatLoad(true); }
    double otbw=getf("otbw",-1);
    if(otbw>0){ if(ampKind==1)eng.plexiAmp.setOtBw(otbw); else eng.amp.setOtBw(otbw); }
    double nfbs=getf("nfbscale",-99);
    if(nfbs>-99){ if(ampKind==1)eng.plexiAmp.setNfb(nfbs); else eng.amp.setNfb(nfbs); }
    int tapsel=(int)getf("tapsel",0); if(tapsel>0)eng.amp.setTapSel(tapsel);
    if(!grid)eng.amp.enableGridBlock(false);
    if(!cabir)eng.forceNoIR();
    if(ampKind==1)eng.plexiAmp.setNfb(plexinfb);
    std::string ir=gets("ir");   // load renders/ir/<name>.wav as the cab
    if(!ir.empty()){ auto v=pa::loadCabWav(("renders/ir/"+ir+".wav").c_str());
        if(v.empty())std::printf("!! IR renders/ir/%s.wav not found/empty\n",ir.c_str());
        else{ eng.setCab(v); std::printf("cab IR: renders/ir/%s.wav (%zu taps)\n",ir.c_str(),v.size()); } }

    std::printf("[amp=%s eco=%d pedals=%d postfx=%d grid=%d cabir=%d nfb=%.0f master=%.4f norm=%.3f]\n",
        ampKind?"Plexi":"Princeton",eco,pedals,postfx,grid,cabir,nfb,master>0?master:c.master,norm);

    while(in.size()%pa::kChunk48)in.push_back(0.0f);
    std::vector<float> L,R; L.reserve(in.size()); R.reserve(in.size());
    float cL[pa::kChunk48],cR[pa::kChunk48];
    for(size_t i=0;i+pa::kChunk48<=in.size();i+=pa::kChunk48){
        eng.processChunk(&in[i],cL,cR);
        for(int j=0;j<pa::kChunk48;++j){L.push_back(cL[j]);R.push_back(cR[j]);}
    }
    // loudness-normalise (measurement-bench rule 1: no A/B without it)
    if(norm>0){ double rms=0; for(float v:L)rms+=(double)v*v; rms=std::sqrt(rms/L.size());
        double g=rms>1e-9?norm/rms:1.0; double pk=0; for(float v:L)pk=std::max(pk,(double)std::fabs(v*g));
        if(pk>0.99)g*=0.99/pk;   // peak guard
        for(auto&v:L)v=(float)(v*g); for(auto&v:R)v=(float)(v*g);
        std::printf("normalised: applied %.2f dB\n",20*std::log10(g)); }
    metrics("out(L)",L,48000);
    writeWav(argv[2],L,R,48000);
    std::printf("wrote %s: %zu frames\n",argv[2],L.size());
    return 0;
}
