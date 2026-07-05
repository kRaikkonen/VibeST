// Isolate the C++ TriodeStage: feed it the clean probe DIRECTLY (no engine,
// no ampInHp, no resamplers) and write its output, to compare its frequency
// response against the Python v1a (which is perfectly flat). Pins whether the
// head's darkness is the TriodeStage port or the engine input path.
#include "../src/engine/dsp.hpp"
#include "../src/engine/princeton.hpp"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
static std::vector<float> readf32(const char* p){
    FILE* f=std::fopen(p,"rb"); std::fseek(f,0,SEEK_END); long n=std::ftell(f); std::fseek(f,0,SEEK_SET);
    std::vector<uint8_t> b(n); if(std::fread(b.data(),1,n,f)!=(size_t)n){} std::fclose(f);
    size_t i=12; for(;i+8<=(size_t)n;){ uint32_t id; std::memcpy(&id,&b[i],4); uint32_t sz; std::memcpy(&sz,&b[i+4],4);
        if(id==0x61746164){ if(i+8+sz>(size_t)n)sz=n-i-8; std::vector<float> v(sz/4); std::memcpy(v.data(),&b[i+8],sz); return v;} i+=8+sz+(sz&1);}
    return {};
}
static void writef32(const char* p,const std::vector<float>&v){
    uint32_t dl=v.size()*4; FILE* f=std::fopen(p,"wb");
    auto w=[&](const void*d,int n){std::fwrite(d,1,n,f);};
    w("RIFF",4);uint32_t r=36+dl;w(&r,4);w("WAVE",4);w("fmt ",4);uint32_t s16=16;w(&s16,4);
    uint16_t fmt=3,ch=1;w(&fmt,2);w(&ch,2);uint32_t sr=48000;w(&sr,4);uint32_t br=48000*4;w(&br,4);
    uint16_t ba=4,bits=32;w(&ba,2);w(&bits,2);w("data",4);w(&dl,4);w(v.data(),dl);std::fclose(f);
}
int main(int argc,char**argv){
    auto x=readf32("renders/probe.wav");
    std::vector<float> y(x.size());
    if(argc>1 && std::strcmp(argv[1],"hp")==0){
        dsp::Biquad hp; hp.highpass(18.0, 0.707, 48000.0);   // the amp input HP
        for(size_t i=0;i<x.size();++i) y[i]=(float)hp.step(x[i]);
        std::printf("fed %zu samples through ampInHp (18Hz highpass) ALONE\n", x.size());
    } else {
        princeton::TriodeStage v1a(princeton::T12AX7(), 48000.0, 240.0, 100e3, 1500.0, 25e-6);
        for(size_t i=0;i<x.size();++i) y[i]=(float)v1a.step(x[i]);
        std::printf("fed %zu samples through a bare C++ TriodeStage(v1a)\n", x.size());
    }
    writef32("renders/tap.wav", y);
    return 0;
}
