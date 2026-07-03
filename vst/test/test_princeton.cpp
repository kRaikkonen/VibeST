// C++ Princeton engine vs Python reference, sample-by-sample.
#include <cstdio>
#include <cmath>
#include <vector>
#include "../src/engine/princeton.hpp"

static std::vector<double> readf64(const char* path) {
    std::vector<double> v;
    if (FILE* f = std::fopen(path, "rb")) {
        std::fseek(f, 0, SEEK_END);
        long bytes = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        v.resize(static_cast<size_t>(bytes) / sizeof(double));
        if (std::fread(v.data(), sizeof(double), v.size(), f) != v.size())
            v.clear();
        std::fclose(f);
    }
    return v;
}

int main() {
    const double fs = 48000.0 * 4.0;
    auto x = readf64("pr_input.f64");
    auto ref = readf64("pr_ref_py.f64");
    auto ir = readf64("pr_tank_ir.f64");
    if (x.empty() || ref.size() != x.size() || ir.empty()) {
        std::printf("missing dumps\n");
        return 1;
    }
    // direct-FIR streaming convolver (test only; plugin uses partitioned FFT)
    std::vector<double> hist;
    auto tank = [&](const double* in, double* out, int n) {
        size_t base = hist.size();
        hist.insert(hist.end(), in, in + n);
        for (int i = 0; i < n; ++i) {
            size_t pos = base + static_cast<size_t>(i);
            size_t kmax = std::min(pos + 1, ir.size());
            double acc = 0.0;
            for (size_t k = 0; k < kmax; ++k)
                acc += ir[k] * hist[pos - k];
            out[i] = acc;
        }
    };
    princeton::AmpControls c;
    c.volume = 0.5; c.treble = 0.55; c.bass = 0.5;
    c.reverb = 0.25; c.tremIntensity = 0.0;
    princeton::Amp amp(fs, c, tank);
    int n = static_cast<int>(x.size());
    std::vector<double> y(n), b1(n), b2(n), b3(n);
    amp.processBlock(x.data(), y.data(), n, b1.data(), b2.data(), b3.data());

    double se = 0, sr = 0;
    for (int i = n / 4; i < n; ++i) {
        double e = y[i] - ref[i];
        se += e * e;
        sr += ref[i] * ref[i];
    }
    double rel = std::sqrt(se / sr);
    // 1e-4 threshold: with the recalibrated (tiny) reverb return level, the
    // solvers' absolute NR tolerance (1e-9 V) is no longer negligible
    // relative to the mV-scale wet path, so bit-match degrades to ~2e-5
    // total. Still ~-94 dB, far below the physics-validation error floor.
    std::printf("Princeton C++ vs Python relative RMS: %.3e  %s\n", rel,
                rel < 1e-4 ? "PASS" : "FAIL");
    return rel < 1e-4 ? 0 : 1;
}
