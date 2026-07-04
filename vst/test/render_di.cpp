// Offline DI render + measurement bench.
// Reads a mono 24-bit PCM WAV (e.g. Bad Religion "Suffer" Guitar L.wav DI,
// 44.1k), resamples to 48k, runs it through the EXACT practice-amp chain
// (engine_core.hpp, user preset), and writes a 48k stereo float WAV.
// Prints RMS / peak / crest-factor (dynamics) / spectral-centroid so before
// vs after physics changes can be compared objectively, not just by ear.
//
//   render_di <in.wav> <out.wav> [start_sec=0] [dur_sec=20]
#define MA_IMPLEMENTATION
#include "../src/standalone/engine_core.hpp"
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>
#include <cmath>

// ---- minimal WAV reader: 16/24/32-int + 32-float, any channel count -------
static std::vector<float> readWavMono(const char* path, int& srOut) {
    std::vector<float> out;
    FILE* f = std::fopen(path, "rb");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", path); return out; }
    std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> b(n);
    if (std::fread(b.data(), 1, n, f) != (size_t)n) { std::fclose(f); return out; }
    std::fclose(f);
    auto u16 = [&](size_t o){ return (uint16_t)(b[o] | (b[o+1]<<8)); };
    auto u32 = [&](size_t o){ return (uint32_t)(b[o] | (b[o+1]<<8) | (b[o+2]<<16) | (b[o+3]<<24)); };
    if (n < 12 || b[0]!='R'||b[1]!='I'||b[2]!='F'||b[3]!='F') return out;
    int ch=1, sr=44100, bits=16, fmt=1; size_t dataOff=0, dataLen=0;
    size_t p = 12;
    while (p + 8 <= (size_t)n) {
        uint32_t id = u32(p); uint32_t sz = u32(p+4); size_t body = p+8;
        if (id == 0x20746d66) { // 'fmt '
            fmt = u16(body); ch = u16(body+2); sr = u32(body+4); bits = u16(body+14);
        } else if (id == 0x61746164) { // 'data'
            dataOff = body; dataLen = sz; break;
        }
        p = body + sz + (sz & 1);
    }
    srOut = sr;
    if (!dataOff) return out;
    if (dataOff + dataLen > (size_t)n) dataLen = n - dataOff;
    int bytesPer = bits/8; size_t frames = dataLen / (bytesPer * ch);
    out.reserve(frames);
    for (size_t i = 0; i < frames; ++i) {
        double acc = 0;
        for (int c = 0; c < ch; ++c) {
            size_t o = dataOff + (i*ch + c)*bytesPer; double s = 0;
            if (fmt == 3 && bits == 32) { float v; std::memcpy(&v,&b[o],4); s=v; }
            else if (bits == 16) { int16_t v=(int16_t)u16(o); s=v/32768.0; }
            else if (bits == 24) { int32_t v=(b[o]|(b[o+1]<<8)|(b[o+2]<<16)); if(v&0x800000)v|=~0xFFFFFF; s=v/8388608.0; }
            else if (bits == 32) { int32_t v=(int32_t)u32(o); s=v/2147483648.0; }
            acc += s;
        }
        out.push_back((float)(acc/ch));   // downmix to mono
    }
    return out;
}

// linear resample (guitar DI is band-limited; 44.1->48 up-ratio 1.088, the
// interpolation error sits well above the guitar band)
static std::vector<float> resample(const std::vector<float>& x, int srIn, int srOut) {
    if (srIn == srOut) return x;
    double r = (double)srIn / srOut;
    size_t m = (size_t)(x.size() / r);
    std::vector<float> y(m);
    for (size_t i = 0; i < m; ++i) {
        double t = i * r; size_t i0 = (size_t)t; double fr = t - i0;
        float a = x[i0], bb = (i0+1 < x.size()) ? x[i0+1] : a;
        y[i] = (float)(a + (bb - a) * fr);
    }
    return y;
}

static void writeWavF32Stereo(const char* path, const std::vector<float>& L,
                              const std::vector<float>& R, int sr) {
    size_t n = std::min(L.size(), R.size());
    uint32_t dataLen = (uint32_t)(n * 2 * 4);
    FILE* f = std::fopen(path, "wb");
    auto w32=[&](uint32_t v){ uint8_t d[4]={(uint8_t)v,(uint8_t)(v>>8),(uint8_t)(v>>16),(uint8_t)(v>>24)}; std::fwrite(d,1,4,f); };
    auto w16=[&](uint16_t v){ uint8_t d[2]={(uint8_t)v,(uint8_t)(v>>8)}; std::fwrite(d,1,2,f); };
    std::fwrite("RIFF",1,4,f); w32(36+dataLen); std::fwrite("WAVE",1,4,f);
    std::fwrite("fmt ",1,4,f); w32(16); w16(3); w16(2); w32(sr); w32(sr*2*4); w16(2*4); w16(32);
    std::fwrite("data",1,4,f); w32(dataLen);
    for (size_t i=0;i<n;++i){ float l=L[i],r=R[i]; std::fwrite(&l,4,1,f); std::fwrite(&r,4,1,f);}
    std::fclose(f);
}

static void metrics(const char* tag, const std::vector<float>& y, int sr) {
    if (y.empty()) return;
    double rms=0, pk=0; for (float v: y){ rms+=(double)v*v; pk=std::max(pk,(double)std::fabs(v)); }
    rms = std::sqrt(rms/y.size());
    double crest = pk>1e-9 ? 20*std::log10(pk/rms) : 0;
    // spectral centroid over a 32768 window mid-file
    size_t N=32768; while(N>y.size()) N>>=1;
    size_t off = y.size()>N ? (y.size()-N)/2 : 0;
    std::vector<dsp::cplx> X(N);
    for(size_t i=0;i<N;++i){ double w=0.5-0.5*std::cos(2*3.141592653589793*i/(N-1)); X[i]=dsp::cplx(y[off+i]*w,0);}
    dsp::fft(X,false);
    double num=0,den=0; for(size_t k=1;k<N/2;++k){ double mag=std::abs(X[k]); double f=(double)k*sr/N; num+=f*mag; den+=mag; }
    double cen = den>0? num/den : 0;
    std::printf("%-10s RMS %.4f  peak %.4f  crest %.1f dB  centroid %.0f Hz\n",
                tag, rms, pk, crest, cen);
}

int main(int argc, char** argv) {
    if (argc < 3) { std::printf("usage: render_di <in.wav> <out.wav> [start_sec=0] [dur_sec=20]\n"); return 1; }
    double startSec = argc>3 ? std::atof(argv[3]) : 0.0;
    double durSec   = argc>4 ? std::atof(argv[4]) : 20.0;
    int sr=0;
    std::vector<float> di = readWavMono(argv[1], sr);
    if (di.empty()) { std::fprintf(stderr, "no samples read\n"); return 1; }
    std::printf("read %s: %zu frames @ %d Hz (%.1f s)\n", argv[1], di.size(), sr, di.size()/(double)sr);
    // slice
    size_t s0 = (size_t)(startSec*sr), s1 = std::min(di.size(), s0 + (size_t)(durSec*sr));
    if (s0 >= di.size()) s0 = 0;
    std::vector<float> slice(di.begin()+s0, di.begin()+s1);
    std::vector<float> in = resample(slice, sr, 48000);
    metrics("DI(in)", in, 48000);

    bool eco = (argc > 7) && std::atoi(argv[7]) != 0;   // 1 = eco/48k amp
    pa::Engine eng(pa::springTankIr(eco ? 48000.0 : 96000.0), {}, eco);
    if (argc > 5) {   // master override (0 = keep preset) to probe staging
        double m = std::atof(argv[5]);
        if (m > 0) { pa::Controls c = eng.ctl; c.master = m; eng.apply(c); }
    }
    if (argc > 6) {   // Princeton NFB feedback-LP cutoff Hz (0 = default ~1.7k)
        double fc = std::atof(argv[6]);
        if (fc > 0) eng.amp.setFbCutoff(fc);
    }
    // pad to a chunk multiple
    while (in.size() % pa::kChunk48) in.push_back(0.0f);
    std::vector<float> L, R; L.reserve(in.size()); R.reserve(in.size());
    float cL[pa::kChunk48], cR[pa::kChunk48];
    double blk1=0, blk3=0;
    for (size_t i=0;i+pa::kChunk48<=in.size(); i+=pa::kChunk48) {
        eng.processChunk(&in[i], cL, cR);
        blk1 = std::max(blk1, eng.amp.v1aBlock());
        blk3 = std::max(blk3, eng.amp.v3bBlock());
        for (int j=0;j<pa::kChunk48;++j){ L.push_back(cL[j]); R.push_back(cR[j]); }
    }
    std::printf("grid-block bias peak: V1A %.3f V  V3B %.3f V (0 = never conducted)\n", blk1, blk3);
    metrics("out(L)", L, 48000);
    writeWavF32Stereo(argv[2], L, R, 48000);
    std::printf("wrote %s: %zu frames stereo @ 48000 Hz\n", argv[2], L.size());
    return 0;
}
