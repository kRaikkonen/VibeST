#include <chrono>
#include <cstdio>
#include <vector>
#include <cmath>
#include "../src/engine/od1.hpp"
int main(){
    const double fs = 48000.0*8.0;
    const int n = static_cast<int>(fs*2.0);          // 2 s of audio
    std::vector<double> x(n);
    for (int i=0;i<n;++i) x[i]=0.3*std::sin(2*od1::kPi*220.0*i/fs);
    od1::Pedal ped(fs,1.0,0.8,od1::RC3403A());
    auto t0=std::chrono::high_resolution_clock::now();
    double acc=0;
    for (int i=0;i<n;++i) acc+=ped.step(x[i]);
    auto t1=std::chrono::high_resolution_clock::now();
    double sec=std::chrono::duration<double>(t1-t0).count();
    std::printf("processed 2.0 s of audio in %.3f s -> %.1fx realtime "
                "(%.1f%% of one core), checksum %.3f\n",
                sec, 2.0/sec, sec/2.0*100.0, acc);
    return 0;
}
