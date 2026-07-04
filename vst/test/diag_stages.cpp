// Where do the ~40 dB of clean-tone level go? Tap every stage of the amp.
#include <cstdio>
#include <cmath>
#include <vector>
#include "../src/engine/princeton.hpp"

int main() {
    double fs = 96000.0;
    princeton::AmpControls c;
    c.volume = 0.5; c.reverb = 0.0; c.treble = 0.55; c.bass = 0.5;
    auto tank = [](const double*, double* o, int n) {
        for (int i = 0; i < n; ++i) o[i] = 0.0;
    };
    princeton::Amp amp(fs, c, tank);
    int n = (int)(0.3 * fs);
    std::vector<double> in(n), out(n), b1(n), b2(n), b3(n);
    // ~0.03 V at the amp input (post-inTrim, a typical picked note)
    for (int i = 0; i < n; ++i)
        in[i] = 0.03 * std::sin(2 * 3.14159265 * 220 * i / fs);
    amp.processBlock(in.data(), out.data(), n, b1.data(), b2.data(),
                     b3.data());
    const char* names[7] = {"V1A plate", "tone stack", "V1B plate",
                            "V3B grid(mix)", "V3B out", "PI out", "speaker"};
    std::printf("clean, vol 0.5, in 0.03 V pk:\n");
    for (int i = 0; i < 7; ++i)
        std::printf("  %-14s peak %10.4f V  (%.1f dB re 1V)\n",
                    names[i], amp.tap[i],
                    amp.tap[i] > 1e-9 ? 20 * std::log10(amp.tap[i]) : -99);
    return 0;
}
