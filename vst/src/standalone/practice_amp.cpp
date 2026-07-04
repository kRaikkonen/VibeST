// Console front-end for the white-box OD-1 + Princeton Reverb practice amp.
// Usage:
//   practice_amp --list
//   practice_amp --selftest
//   practice_amp [--in N] [--out N] [--buffer 64|128|256|512]
//                [--shared] [--cab ir.wav]
#define MA_IMPLEMENTATION
#include "engine_core.hpp"
#include "asio_io.hpp"

#include <conio.h>
#include <string>
#include <windows.h>

namespace {

void printStatus(const pa::Controls& c, int load) {
    std::printf("\r[OD-1 %s d=%.2f l=%.2f] [Vol %.2f Tre %.2f Bas %.2f "
                "Rev %.2f TrS %.2f TrI %.2f] trim %.2f mast %.3f load %d%%  ",
                c.aOn ? "ON " : "off", c.aDrive, c.aLevel, c.volume,
                c.treble, c.bass, c.reverb, c.tremSpeed, c.tremIntensity,
                c.inTrim, c.master, load);
    std::fflush(stdout);
}

double bump(double v, double d) {
    v += d;
    return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
}

int askIndex(const char* what, int nMax) {
    std::printf("select %s [0-%d, Enter=default]: ", what, nMax - 1);
    std::fflush(stdout);
    char line[32];
    if (!std::fgets(line, sizeof(line), stdin)) return -1;
    if (line[0] == '\n' || line[0] == '\r') return -1;
    int v = std::atoi(line);
    return (v >= 0 && v < nMax) ? v : -1;
}

}  // namespace

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    ma_context ctx;
    if (ma_context_init(nullptr, 0, nullptr, &ctx) != MA_SUCCESS) {
        std::printf("audio context init failed\n");
        return 1;
    }
    ma_device_info* playback;
    ma_uint32 nPlayback;
    ma_device_info* capture;
    ma_uint32 nCapture;
    ma_context_get_devices(&ctx, &playback, &nPlayback, &capture, &nCapture);

    bool listOnly = argc > 1 && std::string(argv[1]) == "--list";
    int inIdx = -1, outIdx = -1, buffer = 128, inCh = 2;
    bool exclusive = true, useAsio = false, eco = false;
    const char* cabPath = nullptr;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--shared") exclusive = false;
        if (a == "--asio") useAsio = true;
        if (a == "--eco") eco = true;
        if (i + 1 < argc) {
            if (a == "--in") inIdx = std::atoi(argv[i + 1]);
            if (a == "--out") outIdx = std::atoi(argv[i + 1]);
            if (a == "--buffer") buffer = std::atoi(argv[i + 1]);
            if (a == "--cab") cabPath = argv[i + 1];
            if (a == "--ch") inCh = std::atoi(argv[i + 1]);
        }
    }

    if (argc > 1 && std::string(argv[1]) == "--selftest") {
        for (int mode = 0; mode < 2; ++mode) {
            bool ec = mode == 1;
            pa::Engine e(pa::springTankIr(ec ? 48000.0 : 96000.0), {}, ec);
            const int chunks = 48000 * 2 / pa::kChunk48;
            std::vector<float> in(pa::kChunk48), oL(pa::kChunk48), oR(pa::kChunk48);
            double acc = 0.0;
            auto t0 = std::chrono::high_resolution_clock::now();
            for (int cix = 0; cix < chunks; ++cix) {
                for (int i = 0; i < pa::kChunk48; ++i) {
                    double tt = (cix * pa::kChunk48 + i) / 48000.0;
                    in[i] = static_cast<float>(
                        0.6 * std::sin(2 * 3.14159265358979 * 220.0 * tt)
                        * std::exp(-1.5 * tt));
                }
                e.processChunk(in.data(), oL.data(), oR.data());
                for (int i = 0; i < pa::kChunk48; ++i) acc += oL[i] * oL[i];
            }
            auto t1 = std::chrono::high_resolution_clock::now();
            double sec = std::chrono::duration<double>(t1 - t0).count();
            std::printf("selftest %s: 2.0 s audio in %.3f s (%.2fx realtime, "
                        "%.1f%% core), RMS %.4f, finite %s\n",
                        ec ? "ECO" : "HQ ", sec, 2.0 / sec,
                        sec / 2.0 * 100.0,
                        std::sqrt(acc / (chunks * pa::kChunk48)),
                        std::isfinite(acc) ? "yes" : "NO");
        }
        ma_context_uninit(&ctx);
        return 0;
    }

    pa::AsioIO asio;
    asio.refresh();
    if (useAsio) {
        std::printf("ASIO devices:\n");
        for (size_t i = 0; i < asio.names.size(); ++i)
            std::printf("  [%zu] %s\n", i, asio.names[i].c_str());
        if (asio.ids.empty()) {
            std::printf("no ASIO drivers found\n");
            return 1;
        }
    } else {
        std::printf("capture devices:\n");
        for (ma_uint32 i = 0; i < nCapture; ++i)
            std::printf("  [%u] %s\n", i, capture[i].name);
        std::printf("playback devices:\n");
        for (ma_uint32 i = 0; i < nPlayback; ++i)
            std::printf("  [%u] %s\n", i, playback[i].name);
    }
    if (listOnly) { ma_context_uninit(&ctx); return 0; }

    if (useAsio) {
        if (inIdx < 0 && asio.ids.size() > 1)
            inIdx = askIndex("ASIO device", static_cast<int>(asio.ids.size()));
    } else {
        if (inIdx < 0 && nCapture > 1)
            inIdx = askIndex("input", static_cast<int>(nCapture));
        if (outIdx < 0 && nPlayback > 1)
            outIdx = askIndex("output", static_cast<int>(nPlayback));
    }

    std::vector<double> cab;
    if (cabPath) {
        cab = pa::loadCabWav(cabPath);
        std::printf("cab IR: %s (%zu taps)\n",
                    cab.empty() ? "LOAD FAILED, built-in voicing" : cabPath,
                    cab.size());
    }
    pa::Engine engine(pa::springTankIr(eco ? 48000.0 : 96000.0),
                      std::move(cab), eco);

    ma_device dev;
    char info[160] = {};
    bool asioRunning = false;
    if (useAsio) {
        if (!asio.start(inIdx, static_cast<unsigned>(buffer), &engine,
                        info, sizeof(info), inCh - 1)) {
            std::printf("%s\n", info);
            return 1;
        }
        asioRunning = true;
    } else {
        const ma_device_id* inId =
            (inIdx >= 0 && inIdx < static_cast<int>(nCapture))
                ? &capture[inIdx].id : nullptr;
        const ma_device_id* outId =
            (outIdx >= 0 && outIdx < static_cast<int>(nPlayback))
                ? &playback[outIdx].id : nullptr;
        if (pa::startDevice(&ctx, &dev, &engine, inId, outId, exclusive,
                            info, sizeof(info),
                            static_cast<ma_uint32>(buffer)) != MA_SUCCESS) {
            std::printf("device init failed\n");
            return 1;
        }
    }
    std::printf("\n%s\nkeys: o=OD-1  d/D drive  l/L od-level  v/V volume  "
                "t/T treble\n      b/B bass  r/R reverb  s/S trem-speed  "
                "i/I trem-int  g/G trim  m/M master  q=quit\n\n", info);
    printStatus(engine.ctl, 0);

    for (;;) {
        if (!_kbhit()) {
            Sleep(200);
            printStatus(engine.ctl, engine.loadPct.load());
            continue;
        }
        int ch = _getch();
        if (ch == 'q' || ch == 27) break;
        pa::Controls c;
        {
            std::lock_guard<std::mutex> lk(engine.mtx);
            c = engine.ctl;
        }
        switch (ch) {
            case 'o': c.aOn = !c.aOn; break;
            case 'd': c.aDrive = bump(c.aDrive, -0.05); break;
            case 'D': c.aDrive = bump(c.aDrive, 0.05); break;
            case 'l': c.aLevel = bump(c.aLevel, -0.05); break;
            case 'L': c.aLevel = bump(c.aLevel, 0.05); break;
            case 'v': c.volume = bump(c.volume, -0.05); break;
            case 'V': c.volume = bump(c.volume, 0.05); break;
            case 't': c.treble = bump(c.treble, -0.05); break;
            case 'T': c.treble = bump(c.treble, 0.05); break;
            case 'b': c.bass = bump(c.bass, -0.05); break;
            case 'B': c.bass = bump(c.bass, 0.05); break;
            case 'r': c.reverb = bump(c.reverb, -0.05); break;
            case 'R': c.reverb = bump(c.reverb, 0.05); break;
            case 's': c.tremSpeed = bump(c.tremSpeed, -0.05); break;
            case 'S': c.tremSpeed = bump(c.tremSpeed, 0.05); break;
            case 'i': c.tremIntensity = bump(c.tremIntensity, -0.05); break;
            case 'I': c.tremIntensity = bump(c.tremIntensity, 0.05); break;
            case 'g': c.inTrim = bump(c.inTrim, -0.02); break;
            case 'G': c.inTrim = bump(c.inTrim, 0.02); break;
            case 'm': c.master = bump(c.master, -0.005); break;
            case 'M': c.master = bump(c.master, 0.005); break;
            default: continue;
        }
        {
            std::lock_guard<std::mutex> lk(engine.mtx);
            engine.apply(c);
        }
        printStatus(c, engine.loadPct.load());
    }
    if (asioRunning) asio.stop();
    else ma_device_uninit(&dev);
    ma_context_uninit(&ctx);
    std::printf("\nbye\n");
    return 0;
}

