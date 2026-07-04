// Watch clean output level + PSU rails over 1.5 s to catch a sag collapse.
#include <cstdio>
#include <cmath>
#include <vector>
#include "../src/engine/princeton.hpp"

int main() {
    double fs = 96000.0;
    princeton::AmpControls c; c.volume = 0.35; c.reverb = 0.0;
    auto tank = [](const double*, double* o, int n){ for(int i=0;i<n;++i)o[i]=0; };
    princeton::Amp amp(fs, c, tank);
    int win = (int)(0.1 * fs);
    std::vector<double> in(win), out(win), b1(win), b2(win), b3(win);
    std::printf("time(s)  outRMS(V)   PSU vA   vB\n");
    for (int w = 0; w < 15; ++w) {
        for (int i = 0; i < win; ++i) {
            double t = (w * win + i) / fs;
            in[i] = 0.015 * std::sin(2 * 3.14159265 * 220 * t);
        }
        amp.processBlock(in.data(), out.data(), win, b1.data(), b2.data(),
                         b3.data());
        double a = 0; for (int i = 0; i < win; ++i) a += out[i]*out[i];
        std::printf("  %.1f     %8.4f   %6.1f  %6.1f\n",
                    (w+1)*0.1, std::sqrt(a/win), amp.dbgVA, amp.dbgVB);
    }
    return 0;
}
