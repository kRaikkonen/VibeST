// Bare plexi::Amp harness: feed the clean probe DIRECTLY (flat load, like a NAM
// head capture into a load box), full control of gain/tone, write output.
// argv: gain treble bass mid inScale [nfb]
#include "../src/engine/plexi.hpp"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <algorithm>
static std::vector<float> readf32(const char* p) {
    FILE* f = std::fopen(p, "rb"); std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> b(n); if (std::fread(b.data(), 1, n, f) != (size_t)n) {} std::fclose(f);
    size_t i = 12;
    for (; i + 8 <= (size_t)n;) {
        uint32_t id; std::memcpy(&id, &b[i], 4); uint32_t sz; std::memcpy(&sz, &b[i + 4], 4);
        if (id == 0x61746164) { if (i + 8 + sz > (size_t)n) sz = n - i - 8; std::vector<float> v(sz / 4); std::memcpy(v.data(), &b[i + 8], sz); return v; }
        i += 8 + sz + (sz & 1);
    }
    return {};
}
static void writef32(const char* p, const std::vector<float>& v) {
    uint32_t dl = v.size() * 4; FILE* f = std::fopen(p, "wb");
    auto w = [&](const void* d, int n) { std::fwrite(d, 1, n, f); };
    w("RIFF", 4); uint32_t r = 36 + dl; w(&r, 4); w("WAVE", 4); w("fmt ", 4); uint32_t s16 = 16; w(&s16, 4);
    uint16_t fmt = 3, ch = 1; w(&fmt, 2); w(&ch, 2); uint32_t sr = 48000; w(&sr, 4); uint32_t br = 48000 * 4; w(&br, 4);
    uint16_t ba = 4, bits = 32; w(&ba, 2); w(&bits, 2); w("data", 4); w(&dl, 4); w(v.data(), dl); std::fclose(f);
}
int main(int argc, char** argv) {
    double gain = argc > 1 ? std::atof(argv[1]) : 0.6;
    double treble = argc > 2 ? std::atof(argv[2]) : 0.6;
    double bass = argc > 3 ? std::atof(argv[3]) : 0.4;
    double mid = argc > 4 ? std::atof(argv[4]) : 0.5;
    double inScale = argc > 5 ? std::atof(argv[5]) : 1.0;
    double bright = argc > 6 ? std::atof(argv[6]) : 0.0;
    double presence = argc > 7 ? std::atof(argv[7]) : 0.0;
    double otbw = argc > 8 ? std::atof(argv[8]) : 0.0;
    double nfb = argc > 9 ? std::atof(argv[9]) : 0.0;
    auto xf = readf32("renders/probe_plexi.wav");
    int n = (int)xf.size();
    plexi::Amp amp(48000.0, gain, treble, bass, mid, 0.7);
    amp.setFlatLoad(true);
    amp.setNfb(nfb);
    amp.setBright(bright);
    amp.setPresence(presence);
    if (otbw > 0) amp.setOtBw(otbw);
    std::vector<double> x(n), y(n);
    for (int i = 0; i < n; ++i) x[i] = xf[i] * inScale;
    for (int off = 0; off < n; off += 128) {
        int m = std::min(128, n - off);
        amp.processBlock(&x[off], &y[off], m);
    }
    std::vector<float> yf(y.begin(), y.end());
    writef32("renders/plexi_head.wav", yf);
    std::printf("plexi head: gain=%.2f t=%.2f b=%.2f m=%.2f in=%.4f nfb=%.2f -> plexi_head.wav\n",
                gain, treble, bass, mid, inScale, nfb);
    return 0;
}
