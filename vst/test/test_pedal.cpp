// Bare pedals::Pedal harness: feed the clean probe directly through the pedal
// (SD-1 / TS808 / OD-1), full control of drive/tone/level, write output.
// argv: kind(0=OD1,1=SD1,2=TS808,3=MadRed) drive tone level inScale
#include "../src/engine/pedals.hpp"
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
    int kind = argc > 1 ? std::atoi(argv[1]) : 1;   // default SD1
    double drive = argc > 2 ? std::atof(argv[2]) : 0.5;
    double tone = argc > 3 ? std::atof(argv[3]) : 0.5;
    double level = argc > 4 ? std::atof(argv[4]) : 0.7;
    double inScale = argc > 5 ? std::atof(argv[5]) : 1.0;
    auto xf = readf32("renders/probe_plexi.wav");
    int n = (int)xf.size();
    pedals::Pedal p((pedals::Kind)kind, 48000.0, drive, level, tone, 2);
    std::vector<float> y(n);
    for (int i = 0; i < n; ++i) y[i] = (float)p.step(xf[i] * inScale);
    writef32("renders/pedal_out.wav", y);
    const char* names[] = {"OD1", "SD1", "TS808", "MadRed"};
    std::printf("pedal %s: drive=%.2f tone=%.2f level=%.2f in=%.4f -> pedal_out.wav\n",
                names[kind & 3], drive, tone, level, inScale);
    return 0;
}
