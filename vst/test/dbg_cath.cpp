#include <cstdio>
#include <cmath>
#include <vector>
#include "../src/engine/princeton.hpp"
static std::vector<double> readf64(const char* p_) {
    std::vector<double> v; FILE* f=std::fopen(p_,"rb");
    std::fseek(f,0,SEEK_END); long b=std::ftell(f); std::fseek(f,0,SEEK_SET);
    v.resize(b/8); std::fread(v.data(),8,v.size(),f); std::fclose(f); return v;
}
static double rel(const std::vector<double>&y, const std::vector<double>&r){
    double se=0,sr=0;
    for (size_t i=y.size()/4;i<y.size();++i){double e=y[i]-r[i];se+=e*e;sr+=r[i]*r[i];}
    return std::sqrt(se/sr);
}
int main(){
    const double fs=192000.0;
    const int n=38400;
    princeton::Cathodyne pi(fs, 240.0);
    std::vector<double> yt(n), yb(n);
    for (int i=0;i<n;++i){
        double g=8.0*std::sin(2*princeton::kPi*220.0*i/fs);
        pi.step(g, yt[i], yb[i]);
    }
    std::printf("cathodyne top rel: %.3e  bot rel: %.3e\n",
        rel(yt, readf64("pr_py_cath_t.f64")), rel(yb, readf64("pr_py_cath_b.f64")));
    return 0;
}
