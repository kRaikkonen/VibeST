#include <chrono>
#include <cstdio>
#include <cmath>
#include <vector>
#include "../src/engine/princeton.hpp"
int main(){
    const double fs = 48000.0*4.0;
    const int n = static_cast<int>(fs*2.0);
    std::vector<double> x(n);
    for (int i=0;i<n;++i) x[i]=0.1*std::sin(2*princeton::kPi*220.0*i/fs);
    auto tank = [](const double* in, double* out, int nn){
        for (int i=0;i<nn;++i) out[i]=in[i];   // bypass conv for engine bench
    };
    princeton::AmpControls c; c.volume=0.5; c.reverb=0.25;
    princeton::Amp amp(fs, c, tank);
    std::vector<double> y(n), b1(n), b2(n), b3(n);
    auto t0=std::chrono::high_resolution_clock::now();
    const int blk = 512;
    for (int i=0;i<n;i+=blk){
        int m = std::min(blk, n-i);
        amp.processBlock(x.data()+i, y.data()+i, m,
                         b1.data(), b2.data(), b3.data());
    }
    auto t1=std::chrono::high_resolution_clock::now();
    double sec=std::chrono::duration<double>(t1-t0).count();
    std::printf("Princeton: 2.0 s audio in %.3f s -> %.2fx realtime "
                "(%.1f%% core)\n", sec, 2.0/sec, sec/2.0*100.0);
    return 0;
}
