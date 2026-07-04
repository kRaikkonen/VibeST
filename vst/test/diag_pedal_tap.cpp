#include <cstdio>
#include <cmath>
#include "../src/engine/od1.hpp"
static void run(double fs, int os){
    od1::Pedal p(fs, 1.0, 0.8, od1::RC3403A(), od1::Params{}, os);
    int m=(int)(0.3*fs);
    for(int i=0;i<m;++i) p.step(0.1*std::sin(2*3.14159265*220*i/fs));
    std::printf("fs=%.0fk os=%d drive=1.0 stage peaks: bufIn %.4f drive %.4f filt %.4f level %.4f out %.4f\n",
        fs/1000, os, p.tap[0],p.tap[1],p.tap[2],p.tap[3],p.tap[4]);
}
int main(){ run(384000,1); run(192000,1); run(192000,2); return 0; }
