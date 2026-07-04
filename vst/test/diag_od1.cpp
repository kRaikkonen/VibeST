// Does the OD-1 pedal misbehave (oscillate / blow up) at the engine's
// actual rates (192k HQ, 96k Eco) vs its 384k validation rate?
#include <cstdio>
#include <cmath>
#include <vector>
#include "../src/engine/od1.hpp"

static void run(double fs, double drive, int os = 1) {
    od1::Pedal p(fs, drive, 0.8, od1::RC3403A(), od1::Params{}, os);
    int m = (int)(0.3 * fs);
    std::vector<double> out(m);
    // 220 Hz, ~0.1 V pk guitar-ish level
    for (int i = 0; i < m; ++i)
        out[i] = p.step(0.1 * std::sin(2 * 3.14159265 * 220 * i / fs));
    int from = m / 2, zc = 0;
    double mean = 0; for (int i=from;i<m;++i) mean += out[i]; mean /= (m-from);
    for (int i=from+1;i<m;++i) if((out[i-1]-mean)*(out[i]-mean)<0) ++zc;
    double freq = zc * fs / (2.0*(m-from));
    double ac=0,pk=0; for(int i=from;i<m;++i){ac+=(out[i]-mean)*(out[i]-mean);
        pk=std::max(pk,std::fabs(out[i]));}
    bool bad = freq > 1000 || !std::isfinite(ac) || pk > 10;
    std::printf("  fs=%3.0fk os=%d drive=%.1f: RMS %.4f peak %.4f freq ~%.0f Hz  %s\n",
                fs/1000, os, drive, std::sqrt(ac/(m-from)), pk, freq,
                bad ? "<-- BAD" : "OK");
}

int main() {
    std::printf("384k os=1 (reference, correct):\n");
    for (double d : {0.0, 0.5, 1.0}) run(384000.0, d, 1);
    std::printf("192k os=1 (collapses at high drive):\n");
    for (double d : {0.0, 0.5, 1.0}) run(192000.0, d, 1);
    std::printf("192k os=2 (drive stage internally 384k):\n");
    for (double d : {0.0, 0.5, 1.0}) run(192000.0, d, 2);
    std::printf("96k os=4 (Eco, drive stage internally 384k):\n");
    for (double d : {0.0, 0.5, 1.0}) run(96000.0, d, 4);
    return 0;
}
