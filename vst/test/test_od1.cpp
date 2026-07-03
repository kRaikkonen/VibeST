// Reads od1_input.f64, runs the C++ engine, writes od1_out_cpp.f64,
// and prints relative RMS error against od1_ref_py.f64.
#include <cstdio>
#include <cmath>
#include <vector>
#include "../src/engine/od1.hpp"

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
    const double fs = 48000.0 * 8.0;
    auto x = readf64("od1_input.f64");
    auto ref = readf64("od1_ref_py.f64");
    if (x.empty() || ref.size() != x.size()) {
        std::printf("missing/mismatched dump files\n");
        return 1;
    }
    od1::Pedal pedal(fs, 0.6, 0.8, od1::RC3403A());
    std::vector<double> y(x.size());
    for (size_t i = 0; i < x.size(); ++i) y[i] = pedal.step(x[i]);

    if (FILE* f = std::fopen("od1_out_cpp.f64", "wb")) {
        std::fwrite(y.data(), sizeof(double), y.size(), f);
        std::fclose(f);
    }
    double se = 0.0, sr = 0.0;
    for (size_t i = x.size() / 4; i < x.size(); ++i) {   // skip DC settling
        double e = y[i] - ref[i];
        se += e * e;
        sr += ref[i] * ref[i];
    }
    double rel = std::sqrt(se / sr);
    std::printf("C++ vs Python relative RMS: %.3e  %s\n", rel,
                rel < 1e-9 ? "PASS" : (rel < 1e-6 ? "PASS (fp-order)" : "FAIL"));
    return rel < 1e-6 ? 0 : 1;
}
