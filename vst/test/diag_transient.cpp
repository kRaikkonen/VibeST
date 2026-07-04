// Render a bright hard-pick transient through OD-1 at 384k vs 192k+os2,
// decimate both to 48k, dump for spectral comparison (aliasing check).
#include <cstdio>
#include <cmath>
#include <vector>
#include "../src/engine/od1.hpp"

static void render(double fs, int os, double drive, const char* path) {
    od1::Pedal p(fs, drive, 0.8, od1::RC3403A(), od1::Params{}, os);
    int m = (int)(0.5 * fs);
    std::vector<double> y(m);
    for (int i = 0; i < m; ++i) {
        double t = i / fs;
        double env = (t < 0.002) ? t / 0.002 : std::exp(-5.0 * (t - 0.002));
        double s = 0;
        for (int h = 1; h <= 25; ++h) s += (1.0 / h) * std::sin(2*3.14159265*220*h*t);
        y[i] = p.step(0.4 * env * s);
    }
    // naive decimate to 48k (NO extra filter — exposes aliasing that the
    // pedal's own output rate does/doesn't prevent)
    int dec = (int)(fs / 48000);
    std::vector<double> d;
    for (size_t i = 0; i < y.size(); i += dec) d.push_back(y[i]);
    FILE* f = std::fopen(path, "wb");
    std::fwrite(d.data(), 8, d.size(), f);
    std::fclose(f);
}

int main() {
    render(384000, 1, 0.6, "od1_384.f64");
    render(192000, 2, 0.6, "od1_192os2.f64");
    std::printf("dumped 384k and 192k+os2 (drive 0.6, bright pluck)\n");
    return 0;
}
