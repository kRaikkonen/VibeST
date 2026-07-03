#include <cstdio>
#include <vector>
#include "../src/engine/od1.hpp"
static std::vector<double> readf64(const char* p_) {
    std::vector<double> v; FILE* f = std::fopen(p_, "rb");
    std::fseek(f,0,SEEK_END); long b=std::ftell(f); std::fseek(f,0,SEEK_SET);
    v.resize(b/8); std::fread(v.data(),8,v.size(),f); std::fclose(f); return v;
}
int main(){
    auto x = readf64("py_s1_bufin.f64");
    od1::Params p; od1::Pedal ped(48000.0*8.0, 0.6, 0.8, od1::RC3403A(), p);
    std::vector<double> y(x.size());
    for (size_t i=0;i<x.size();++i) y[i]=ped.driveStage.step(x[i]);
    FILE* f=std::fopen("cpp_s2.f64","wb");
    std::fwrite(y.data(),8,y.size(),f); std::fclose(f);
    return 0;
}
