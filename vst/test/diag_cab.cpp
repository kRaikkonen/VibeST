// Does the cab biquad chain degrade over time (denormals / state)?
#include <cstdio>
#include <cmath>
#include <vector>
#include "../src/engine/dsp.hpp"

int main() {
    dsp::Biquad hp, bump, pres, lp1, lp2;
    hp.highpass(75.0, 0.707, 48000.0);
    bump.peaking(110.0, 1.1, 0.0, 48000.0);
    pres.peaking(2600.0, 1.0, 0.0, 48000.0);
    lp1.lowpass(5500.0, 0.707, 48000.0);
    lp2.lowpass(5500.0, 0.707, 48000.0);
    int blk = 128, nb = 48000 * 3 / blk;
    std::printf("win(s)  cabInRMS  cabOutRMS  gain\n");
    for (int w = 0; w < 30; ++w) {
        double si = 0, so = 0;
        for (int b = 0; b < nb / 30; ++b) {
            for (int i = 0; i < blk; ++i) {
                double t = ((w * (nb/30) + b) * blk + i) / 48000.0;
                double x = 15.0 * std::sin(2 * 3.14159265 * 220 * t);
                double y = lp2.step(lp1.step(pres.step(bump.step(hp.step(x)))));
                si += x * x; so += y * y;
            }
        }
        int cnt = (nb/30) * blk;
        if (w % 3 == 0 || w > 26)
            std::printf("  %.1f    %7.3f   %7.4f   %.4f\n",
                        (w+1)*0.1, std::sqrt(si/cnt), std::sqrt(so/cnt),
                        std::sqrt(so/si));
    }
    return 0;
}
