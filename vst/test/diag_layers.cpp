// With OD-1 on, WHERE does distortion happen? Tap the OD-1 output, the amp
// tube stages (are they clipping?), and how often the output soft-limiter
// engages (>0.9). "Two layers" = OD-1 grit + a second clipping somewhere.
#define MA_IMPLEMENTATION
#include "../src/standalone/engine_core.hpp"
#include <cstdio>
#include <cmath>
#include <vector>

int main() {
    for (int mode = 0; mode < 2; ++mode)
    for (double pick : {0.15, 0.5, 1.0}) {   // soft/med/hard
        pa::Engine e(pa::springTankIr(96000.0), {}, false);
        pa::Controls c;
        c.odOn = (mode == 1); c.odDrive = 0.6; c.odLevel = 0.35;
        c.volume = 0.4; c.reverb = 0.0; c.master = 0.045; c.inTrim = 0.4;
        e.apply(c);
        // reset amp taps
        for (int k = 0; k < 7; ++k) e.amp.tap[k] = 0;
        std::vector<float> in(pa::kChunk48), out(pa::kChunk48);
        int limHits = 0, total = 0;
        double outPk = 0;
        for (int cix = 0; cix < 48000 / pa::kChunk48; ++cix) {
            for (int i = 0; i < pa::kChunk48; ++i) {
                double t = (cix * pa::kChunk48 + i) / 48000.0;
                in[i] = (float)(pick * std::sin(2 * 3.14159265 * 220 * t));
            }
            e.processChunk(in.data(), out.data());
            for (int i = 0; i < pa::kChunk48; ++i) {
                double a = std::fabs(out[i]);
                outPk = std::max(outPk, a);
                if (a > 0.9) ++limHits;
                ++total;
            }
        }
        // amp taps: [0]V1A [1]stack [2]V1B [3]Vg(mix) [4]V3B [5]PI [6]spk
        std::printf("OD-1 %s pick=%.2f: out peak %.2f limiter %.0f%% | "
                    "amp V1A %.3f V1B %.2f spk %.1f\n",
                    mode ? "ON " : "off", pick, outPk, 100.0 * limHits / total,
                    e.amp.tap[0], e.amp.tap[2], e.amp.tap[6]);
    }
    return 0;
}
