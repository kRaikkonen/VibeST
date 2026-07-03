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

static void compare(const char* name, const std::vector<double>& y,
                    const char* refPath) {
    auto ref = readf64(refPath);
    double se = 0, sr = 0;
    for (size_t i = y.size() / 4; i < y.size(); ++i) {
        double e = y[i] - ref[i];
        se += e * e;
        sr += ref[i] * ref[i];
    }
    std::printf("%-10s rel RMS %.3e\n", name, std::sqrt(se / (sr + 1e-30)));
}

int main() {
    const double fs = 48000.0 * 8.0;
    auto x = readf64("od1_input.f64");
    od1::Params p;
    od1::OpampMacro oa = od1::RC3403A();
    od1::Pedal pedal(fs, 0.6, 0.8, oa, p);

    size_t n = x.size();
    std::vector<double> y(n);
    for (size_t i = 0; i < n; ++i) y[i] = pedal.bufIn.step(x[i]);
    compare("s1_bufin", y, "py_s1_bufin.f64");
    for (size_t i = 0; i < n; ++i) y[i] = pedal.driveStage.step(y[i]);
    compare("s2_drive", y, "py_s2_drive.f64");
    for (size_t i = 0; i < n; ++i) y[i] = pedal.filt.step(y[i]);
    compare("s3_filt", y, "py_s3_filt.f64");
    for (size_t i = 0; i < n; ++i) y[i] = pedal.hpLvl.step(y[i]) * pedal.kLevel;
    compare("s4_level", y, "py_s4_level.f64");
    for (size_t i = 0; i < n; ++i) y[i] = pedal.bufOut.step(y[i]);
    compare("s5_bufout", y, "py_s5_bufout.f64");
    for (size_t i = 0; i < n; ++i) y[i] = pedal.hpOut.step(y[i]);
    compare("s6_out", y, "py_s6_out.f64");
    return 0;
}
