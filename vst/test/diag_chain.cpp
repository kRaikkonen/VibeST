// Isolate the 40 dB discrepancy: resampler gain, direct amp, full engine.
#define MA_IMPLEMENTATION
#include "../src/standalone/engine_core.hpp"
#include <cstdio>
#include <cmath>

static double rmsd(const double* y, int n, int from) {
    double a = 0; for (int i = from; i < n; ++i) a += y[i]*y[i];
    return std::sqrt(a / (n - from));
}

int main() {
    // (a) resampler 48->96->48 round trip gain
    int n = 4800;
    dsp::Up2 up; dsp::Down2 dn;
    std::vector<double> x(n), x2(2*n), y(n);
    for (int i = 0; i < n; ++i) x[i] = 0.03 * std::sin(2*3.14159265*220*i/48000.0);
    up.process(x.data(), x2.data(), n);
    dn.process(x2.data(), y.data(), n);
    std::printf("(a) resampler 48->96->48 gain: in RMS %.4f out RMS %.4f\n",
                rmsd(x.data(),n,n/4), rmsd(y.data(),n,n/4));

    // (b) direct amp at 96k, 0.015 V steady, reverb 0 — split DC vs AC
    {
        double fs = 96000.0;
        princeton::AmpControls c; c.volume = 0.35; c.reverb = 0.0;
        auto tank = [](const double*, double* o, int nn){ for(int i=0;i<nn;++i)o[i]=0; };
        princeton::Amp amp(fs, c, tank);
        int m = (int)(0.3*fs);
        std::vector<double> in(m), out(m), b1(m), b2(m), b3(m);
        for (int i=0;i<m;++i) in[i]=0.015*std::sin(2*3.14159265*220*i/fs);
        amp.processBlock(in.data(), out.data(), m, b1.data(), b2.data(), b3.data());
        double mean=0; for(int i=m/3;i<m;++i) mean+=out[i]; mean/=(m-m/3);
        double ac=0; for(int i=m/3;i<m;++i) ac+=(out[i]-mean)*(out[i]-mean);
        ac=std::sqrt(ac/(m-m/3));
        std::printf("(b) direct amp: total RMS %.4f  DC(mean) %.4f  "
                    "AC RMS %.4f  <-- AC is the real signal\n",
                    rmsd(out.data(),m,m/3), mean, ac);
    }

    // (c) full engine, input that yields 0.015 V after inTrim
    {
        pa::Engine e(pa::springTankIr(), {}, false);
        pa::Controls c; c.odOn=false; c.volume=0.35; c.reverb=0.0;
        c.master=1.0; c.inTrim=1.0;   // unity, so out = raw speaker V (pre-clip)
        e.apply(c);
        int chunks = 48000*1/pa::kChunk48;
        std::vector<float> in(pa::kChunk48), out(pa::kChunk48), yy;
        for (int cix=0;cix<chunks;++cix){
            for (int i=0;i<pa::kChunk48;++i){
                double t=(cix*pa::kChunk48+i)/48000.0;
                in[i]=(float)(0.015*std::sin(2*3.14159265*220*t));
            }
            e.processChunk(in.data(), out.data());
            for (int i=0;i<pa::kChunk48;++i) yy.push_back(out[i]);
        }
        double a=0; for (size_t i=yy.size()/3;i<yy.size();++i) a+=yy[i]*yy[i];
        std::printf("(c) full engine, 0.015V in, rev0, unity master/trim: "
                    "out RMS %.4f\n", std::sqrt(a/(yy.size()*2/3)));
        // y96 RMS + x96 DC over 0.1 s windows to see the decay
        pa::Engine e2(pa::springTankIr(), {}, false);
        e2.apply(c);
        std::vector<float> in2(pa::kChunk48), out2(pa::kChunk48);
        int wch = (int)(0.1*48000)/pa::kChunk48;
        std::printf("(c2) engine y96 over time:\n  win  y96RMS   x96DCavg\n");
        std::printf("  win  y96RMS  y48RMS  ycabRMS\n");
        for (int w=0; w<15; ++w) {
            e2.dbgY96sq=0; e2.dbgY48Sq=0; e2.dbgYcabSq=0; e2.dbgN=0;
            double osq=0; long on=0;
            for (int cix=0;cix<wch;++cix){
                for (int i=0;i<pa::kChunk48;++i){
                    double t=((w*wch+cix)*pa::kChunk48+i)/48000.0;
                    in2[i]=(float)(0.015*std::sin(2*3.14159265*220*t)); }
                e2.processChunk(in2.data(), out2.data());
                for (int i=0;i<pa::kChunk48;++i){ osq+=out2[i]*out2[i]; ++on; }
            }
            std::printf("  %.1fs %8.4f %8.4f %8.4f\n", (w+1)*0.1,
                        std::sqrt(e2.dbgY96sq/e2.dbgN),
                        std::sqrt(e2.dbgY48Sq/e2.dbgN),
                        std::sqrt(e2.dbgYcabSq/e2.dbgN));
        }
        std::printf("     y96 peak %.3f -> y48(post-down) peak %.3f -> "
                    "ycab peak %.3f\n", e2.dbgY96, e2.dbgY48, e2.dbgYcab);
        const char* nm[7]={"V1A","stack","V1B","Vg(mix)","V3B","PI","spk"};
        for (int i=0;i<7;++i)
            std::printf("      engine amp tap %-8s %.4f\n", nm[i], e.amp.tap[i]);
        std::printf("      eco=%d ampChunk=%d\n", (int)e.eco, e.ampChunk);
    }
    // (f) Up2 alone gain, and amp fed in 256-sample blocks
    {
        dsp::Up2 up2; int m=2*n; std::vector<double> xu(n), yu(m);
        for (int i=0;i<n;++i) xu[i]=0.015*std::sin(2*3.14159265*220*i/48000.0);
        up2.process(xu.data(), yu.data(), n);
        std::printf("(f) Up2 48->96 gain: in RMS %.5f out RMS %.5f\n",
                    rmsd(xu.data(),n,n/4), rmsd(yu.data(),m,m/4));
        // amp in 256-sample blocks, direct 0.015V @96k, reverb 0
        double fs=96000.0; princeton::AmpControls c; c.volume=0.35; c.reverb=0.0;
        auto tk=[](const double*,double* o,int nn){for(int i=0;i<nn;++i)o[i]=0;};
        princeton::Amp amp(fs,c,tk);
        int blk=256, nb=(int)(0.5*fs)/blk; std::vector<double> yall;
        std::vector<double> ib(blk), ob(blk), s1(blk), s2(blk), s3(blk);
        for (int w=0;w<nb;++w){
            for (int i=0;i<blk;++i){ double t=(w*blk+i)/fs;
                ib[i]=0.015*std::sin(2*3.14159265*220*t); }
            amp.processBlock(ib.data(), ob.data(), blk, s1.data(),s2.data(),s3.data());
            for (int i=0;i<blk;++i) yall.push_back(ob[i]);
        }
        double a=0; for (size_t i=yall.size()/2;i<yall.size();++i)a+=yall[i]*yall[i];
        std::printf("(g) amp in 256-blocks, 0.015V, rev0: out RMS %.4f\n",
                    std::sqrt(a/(yall.size()/2)));
    }
    // (h) standalone amp, THEN setTone (as the engine's apply() does)
    {
        double fs=96000.0; princeton::AmpControls c; c.volume=0.35; c.reverb=0.0;
        auto tk=[](const double*,double* o,int nn){for(int i=0;i<nn;++i)o[i]=0;};
        princeton::Amp amp(fs,c,tk);
        amp.setTone(0.55, 0.5, 0.35);      // exactly what engine apply() calls
        int m=(int)(0.5*fs); std::vector<double> in(m),out(m),s1(m),s2(m),s3(m);
        for (int i=0;i<m;++i) in[i]=0.015*std::sin(2*3.14159265*220*i/fs);
        amp.processBlock(in.data(),out.data(),m,s1.data(),s2.data(),s3.data());
        double a=0; for(int i=m/2;i<m;++i)a+=out[i]*out[i];
        std::printf("(h) standalone amp + setTone(.55,.5,.35): out RMS %.4f\n",
                    std::sqrt(a/(m/2)));
    }
    // (d) FULL cab chain as the engine builds it (incl. 0-dB peaking)
    {
        dsp::Biquad hp, bump, pres, lp1, lp2;
        hp.highpass(75.0, 0.707, 48000.0);
        bump.peaking(110.0, 1.1, 0.0, 48000.0);
        pres.peaking(2600.0, 1.0, 0.0, 48000.0);
        lp1.lowpass(5500.0, 0.707, 48000.0);
        lp2.lowpass(5500.0, 0.707, 48000.0);
        std::vector<double> x2(n), y2(n);
        for (int i=0;i<n;++i) x2[i]=20.0*std::sin(2*3.14159265*220*i/48000.0);
        for (int i=0;i<n;++i)
            y2[i]=lp2.step(lp1.step(pres.step(bump.step(hp.step(x2[i])))));
        std::printf("(d) FULL cab chain @220Hz: in RMS %.3f out RMS %.3f "
                    "(gain %.3f)\n", rmsd(x2.data(),n,n/4),
                    rmsd(y2.data(),n,n/4),
                    rmsd(y2.data(),n,n/4)/rmsd(x2.data(),n,n/4));
        // 0-dB peaking alone
        dsp::Biquad pk; pk.peaking(110.0, 1.1, 0.0, 48000.0);
        std::vector<double> y3(n);
        for (int i=0;i<n;++i) y3[i]=pk.step(x2[i]);
        std::printf("    0-dB peaking alone gain: %.4f\n",
                    rmsd(y3.data(),n,n/4)/rmsd(x2.data(),n,n/4));
    }
    // (e) Down2 alone, big 220 Hz signal at 96k -> 48k
    {
        dsp::Down2 d2;
        int m=2*n; std::vector<double> x2(m), y2(n);
        for (int i=0;i<m;++i) x2[i]=20.0*std::sin(2*3.14159265*220*i/96000.0);
        d2.process(x2.data(), y2.data(), n);
        std::printf("(e) Down2 96->48 @220Hz: in pk 20 out RMS %.3f "
                    "(expect ~14.1)\n", rmsd(y2.data(),n,n/4));
    }
    return 0;
}
