#include <cstdio>
#include <cmath>
#include <vector>
#include "../src/engine/princeton.hpp"
static std::vector<double> readf64(const char* p_) {
    std::vector<double> v; FILE* f=std::fopen(p_,"rb");
    std::fseek(f,0,SEEK_END); long b=std::ftell(f); std::fseek(f,0,SEEK_SET);
    v.resize(b/8); std::fread(v.data(),8,v.size(),f); std::fclose(f); return v;
}
int main(){
    const double fs = 192000.0;
    auto x = readf64("pr_input.f64");
    auto ir = readf64("pr_tank_ir.f64");
    std::vector<double> hist;
    auto tank = [&](const double* in, double* out, int n){
        size_t base = hist.size();
        hist.insert(hist.end(), in, in+n);
        for (int i=0;i<n;++i){
            size_t pos = base+i, kmax = std::min(pos+1, ir.size());
            double acc=0; for (size_t k=0;k<kmax;++k) acc+=ir[k]*hist[pos-k];
            out[i]=acc;
        }
    };
    princeton::AmpControls c; c.volume=0.5; c.treble=0.55; c.bass=0.5; c.reverb=0.25;
    princeton::Amp amp(fs, c, tank);
    int n = (int)x.size();
    std::vector<double> y(n), b1(n), b2(n), b3(n);
    amp.processBlock(x.data(), y.data(), n, b1.data(), b2.data(), b3.data());
    FILE* f=std::fopen("pr_out_cpp.f64","wb");
    std::fwrite(y.data(),8,y.size(),f); std::fclose(f);
    return 0;
}
