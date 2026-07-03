#define MA_IMPLEMENTATION
#include "../src/standalone/engine_core.hpp"
#include <cstdio>
int main(){
    auto gen = pa::springTankIr();
    auto ref = pa::loadIr("../tank_ir_96k.f64");
    if (ref.empty()) { std::printf("ref file missing\n"); return 1; }
    size_t n = std::min(gen.size(), ref.size());
    double se=0, sr=0;
    for (size_t i=0;i<n;++i){ double e=gen[i]-ref[i]; se+=e*e; sr+=ref[i]*ref[i]; }
    std::printf("generated vs Python IR: n=%zu rel RMS %.3e\n", n, std::sqrt(se/sr));
    return 0;
}
