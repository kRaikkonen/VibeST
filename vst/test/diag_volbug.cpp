// Reproduce: OD-1 on, drag amp Volume back and forth many times -> "mush"
// then overflow. Likely the tone-stack rebuild makes the filter state
// diverge at some pot position. Sweep volume repeatedly, watch the output.
#define MA_IMPLEMENTATION
#include "../src/standalone/engine_core.hpp"
#include <cstdio>
#include <cmath>
#include <vector>

int main() {
    pa::Engine e(pa::springTankIr(96000.0), {}, false);
    pa::Controls c;
    c.odOn = true; c.odDrive = 0.6; c.odLevel = 0.35;
    c.volume = 0.4; c.reverb = 0.25; c.master = 0.045; c.inTrim = 0.4;
    e.apply(c);
    std::vector<float> in(pa::kChunk48), out(pa::kChunk48);
    double worst = 0;
    int nanBlocks = 0;
    unsigned seed = 12345;
    auto rnd = [&](){ seed = seed*1664525+1013904223; return (seed>>8)/16777216.0; };
    // aggressive: change Volume to a random value EVERY block (like frantic
    // slider dragging), including extremes 0 and 1
    for (int block = 0; block < 6000; ++block) {
        c.volume = (block % 7 == 0) ? (rnd() < 0.5 ? 0.0 : 1.0) : rnd();
        e.apply(c);
        for (int i = 0; i < pa::kChunk48; ++i) {
            double t = (block * pa::kChunk48 + i) / 48000.0;
            in[i] = (float)(0.4 * std::sin(2 * 3.14159265 * 220 * t));
        }
        e.processChunk(in.data(), out.data());
        double blkRms = 0;
        for (int i = 0; i < pa::kChunk48; ++i) {
            double a = std::fabs(out[i]);
            if (!std::isfinite(a)) { ++nanBlocks; a = 0; }
            worst = std::max(worst, a);
            blkRms += out[i] * out[i];
        }
        if (worst > 1.5 || nanBlocks || (block % 1500 == 0))
            std::printf("block %4d vol %.2f: worst %.3f nan %d\n",
                        block, c.volume, worst, nanBlocks);
        if (nanBlocks) break;
    }
    std::printf("FINAL: worst|out| %.4f, nan/inf: %s\n",
                worst, nanBlocks ? "YES <-- BUG REPRODUCED" : "no");
    return 0;
}
