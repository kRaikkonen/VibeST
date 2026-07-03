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
    auto x = readf64("pr_input.f64");
    princeton::TriodeStage st(princeton::T12AX7(), fs, 240.0, 100e3, 1500.0, 25e-6, 0.02e-6, 1e6);
    std::vector<double> y(x.size());
    for (size_t i=0;i<x.size();++i) y[i]=st.step(x[i]);
    std::printf("triode stage rel: %.3e\n", rel(y, readf64("pr_py_triode.f64")));
    princeton::PowerStage ps(fs);
    std::vector<double> g(x.size()), yp(x.size());
    for (size_t i=0;i<g.size();++i) g[i]=15.0*std::sin(2*princeton::kPi*110.0*i/fs);
    for (size_t i=0;i<g.size();++i) yp[i]=ps.step(g[i], -g[i], 420.0, 410.0);
    std::printf("power stage rel: %.3e\n", rel(yp, readf64("pr_py_power.f64")));
    return 0;
}
