// Is the amp self-oscillating (NFB unstable) at 96k/48k but not 192k?
#include <cstdio>
#include <cmath>
#include <vector>
#include "../src/engine/princeton.hpp"

static void run(double fs, double nfb = 1.0, double fc = 1600.0) {
    princeton::AmpControls c; c.volume = 0.35; c.reverb = 0.0;
    auto tank = [](const double*, double* o, int n){ for(int i=0;i<n;++i)o[i]=0; };
    princeton::Amp amp(fs, c, tank);
    amp.setNfb(nfb);
    amp.setFbCutoff(fc);
    int m = (int)(0.5 * fs);
    std::vector<double> in(m), out(m), b1(m), b2(m), b3(m);
    for (int i = 0; i < m; ++i)
        in[i] = 0.015 * std::sin(2 * 3.14159265 * 220 * i / fs);
    amp.processBlock(in.data(), out.data(), m, b1.data(), b2.data(), b3.data());
    // crude spectral peak by zero-crossing rate on the last half
    int zc = 0; int from = m/2;
    double mean = 0; for (int i=from;i<m;++i) mean += out[i]; mean/=(m-from);
    for (int i=from+1;i<m;++i)
        if ((out[i-1]-mean)*(out[i]-mean) < 0) ++zc;
    double freq = zc * fs / (2.0 * (m-from));
    double ac=0; for(int i=from;i<m;++i) ac+=(out[i]-mean)*(out[i]-mean);
    std::printf("  fs=%.0fk nfb=%.1f fc=%.0f: out RMS %.3f  freq ~%.0f Hz  %s\n",
                fs/1000, nfb, fc, std::sqrt(ac/(m-from)), freq,
                freq > 2000 ? "<-- OSC" : "(220 Hz OK)");
}

int main() {
    std::printf("full NFB, find fc stable at BOTH 48k and 96k:\n");
    for (double fc : {300.0, 250.0, 200.0, 150.0}) {
        run(48000.0, 1.0, fc);
        run(96000.0, 1.0, fc);
    }
    return 0;
}
