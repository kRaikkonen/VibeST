// Isolate the C++ princeton::Amp: feed the clean probe DIRECTLY into the amp
// (no engine_core, no resamplers, no gate/master/ceiling, no render_di) and
// write its flat-load output. Pins whether the jagged/junk C++ head is the
// amp physics or the engine harness around it. Compare vs the Python head
// (which matches the NAM within 0.28 dB) and the NAM itself.
#include "../src/engine/princeton.hpp"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
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
int main() {
    auto xf = readf32("renders/probe.wav");
    std::vector<double> x(xf.begin(), xf.end());
    int n = (int)x.size();
    princeton::AmpControls c;
    c.treble = 0.4; c.bass = 0.2; c.volume = 0.4; c.reverb = 0.0; c.tremIntensity = 0.0;
    // no-op spring tank (reverb=0 anyway)
    princeton::Amp amp(48000.0, c, [](const double*, double* w, int m) { for (int i = 0; i < m; ++i) w[i] = 0.0; });
    amp.setFlatLoad(true);
    amp.setNfb(0.0);            // open loop (matches the Python head that fit NAM)
    std::vector<double> y(n), v1b(n), drv(n), wet(n);
    // process in 128-sample blocks like the engine
    for (int off = 0; off < n; off += 128) {
        int m = std::min(128, n - off);
        amp.processBlock(&x[off], &y[off], m, &v1b[off], &drv[off], &wet[off]);
    }
    std::vector<float> yf(y.begin(), y.end());
    writef32("renders/head_probe.wav", yf);
    std::printf("bare princeton::Amp -> renders/head_probe.wav (%d samples, flat load, nfb off)\n", n);
    return 0;
}
