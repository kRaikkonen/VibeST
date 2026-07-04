// Is the OD-1 actually saturating at the engine's real input levels, or
// barely clipping (mostly-clean = "two layers / feels linear")?
// Measure the drive-stage transfer curve + harmonic content vs input level.
#include <cstdio>
#include <cmath>
#include <vector>
#include "../src/engine/od1.hpp"

static double thd_ratio(od1::Pedal& p, double amp, double fs) {
    int m = (int)(0.1 * fs);
    std::vector<double> y(m);
    for (int i = 0; i < m; ++i) y[i] = p.step(amp * std::sin(2*3.14159265*220*i/fs));
    int from = m/2, N = m-from;
    auto H = [&](int k){ double re=0,im=0; for(int i=from;i<m;++i){
        double a=2*3.14159265*k*220*i/fs; re+=y[i]*cos(a); im-=y[i]*sin(a);}
        return std::sqrt(re*re+im*im)*2/N; };
    double h1=H(1), hn=0; for(int k=2;k<=10;++k) hn+=H(k)*H(k);
    return std::sqrt(hn)/h1;  // THD (harmonic energy / fundamental)
}

int main() {
    double fs = 192000;
    // engine feeds inTrim(0.15) x guitar. A typical picked note off a
    // Scarlett is ~0.2-0.5 full scale -> 0.03-0.075 V into the OD-1.
    std::printf("OD-1 THD vs input level (what the pedal actually sees):\n");
    for (double dr : {0.4, 0.6, 0.8}) {
        std::printf(" drive %.1f: ", dr);
        for (double vin : {0.03, 0.06, 0.12, 0.25}) {
            od1::Pedal p(fs, dr, 0.8, od1::RC3403A(), od1::Params{}, 2);
            std::printf("%.0fmV->THD %2.0f%%  ", vin*1000, thd_ratio(p, vin, fs)*100);
        }
        std::printf("\n");
    }
    std::printf("(guitar overdrive wants ~30-60%% THD to feel saturated;\n"
                " <15%% = mostly clean fundamental = the 'two layers' feel)\n");
    return 0;
}
