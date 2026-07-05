// Marshall Super Lead "Plexi" (1959 100W / 1987 50W) preamp + power.
// Reuses the validated Princeton white-box building blocks (Koren triode
// stages, MNA tone stack, push-pull power stage with the NFB-stabilising
// feedback low-pass, PSU sag). The Marshall character vs the Princeton is:
//   * cascaded high-gain 12AX7 stages (V1 -> V2) => crunch/gain
//   * brighter, mid-scooped FMV tone stack (Marshall values)
//   * REAL long-tailed-pair phase inverter (princeton::LTP): gain + built-in
//     imbalance + asymmetric overdrive, with the OT feedback injected into the
//     LTP reference grid (the physically-correct Marshall NFB point)
// Circuit: Rob Robinette's annotated Super Lead schematic (user-provided).
//
// Known simplifications (tracked): power tube reuses the 6V6 model (EL34 is a
// refinement); the DC-coupled cathode-follower tone buffer is folded into the
// stage loading; tone-stack values are Marshall but the network is the same
// 8-node FMV MNA as the Fender.
#pragma once
#include "princeton.hpp"

namespace plexi {

using princeton::Triode;
using princeton::T12AX7;

// Marshall FMV tone stack (treble 250k, bass 1M, mid 25k, slope 33k;
// caps 470pF / 22n / 22n) — brighter & more mid-voiced than the Fender.
class MarshallTone {
public:
    MarshallTone(double fs, double treble, double bass, double mid,
                 double Rsrc = 38e3) { rebuild(fs, treble, bass, mid, Rsrc); }
    void rebuild(double fs, double treble, double bass, double mid,
                 double Rsrc = 38e3) {
        const double RT = 250e3, RB = 1e6, RM = 25e3;
        auto tap = [](double p){ p = std::clamp(p, 1e-3, 0.999); return p * p; };
        double tw = tap(treble), bw = tap(bass), mw = std::clamp(mid,1e-3,0.999);
        double G[8][8] = {}, C[8][8] = {};
        auto R = [&](int a,int b,double o){ double g=1.0/std::max(o,1.0);
            if(a>0)G[a-1][a-1]+=g; if(b>0)G[b-1][b-1]+=g;
            if(a>0&&b>0){G[a-1][b-1]-=g;G[b-1][a-1]-=g;} };
        auto Cap=[&](int a,int b,double f){
            if(a>0)C[a-1][a-1]+=f; if(b>0)C[b-1][b-1]+=f;
            if(a>0&&b>0){C[a-1][b-1]-=f;C[b-1][a-1]-=f;} };
        // node map like the Fender FMV: 1 src,2 plate,3 treb-cap,4 slope,
        // 5 bass/mid,6 treb wiper(out),7 (unused vol),8 bass-cap
        R(1,2,Rsrc);
        Cap(2,3,470e-12);
        R(3,6,(1.0-tw)*RT); R(6,4,tw*RT);      // treble pot lower -> slope node
                                               // 4 ("B"), not mid node 5 (same
                                               // wiring bug fixed in Fender FMV)
        R(2,4,33e3);
        Cap(4,8,22e-9); R(8,5,bw*RB);
        Cap(4,5,22e-9);
        R(5,0,mw*RM);                          // mid pot to ground
        R(7,0,1e9);                            // node 7 unused (no volume pot
                                               // in the stack) -> tie down so
                                               // the MNA matrix isn't singular
        double k=2.0*fs, A1[N][N]={},A2[N][N]={};
        for(int r=0;r<8;++r)for(int c=0;c<8;++c){
            A1[r][c]=G[r][c]+k*C[r][c]; A2[r][c]=-G[r][c]+k*C[r][c]; }
        A1[0][8]=1; A1[8][0]=1; A2[8][0]=-1;
        double B[N]={}; B[8]=1;
        double aug[N][2*N+1];
        for(int r=0;r<N;++r){ for(int c=0;c<N;++c){aug[r][c]=A1[r][c];
            aug[r][N+c]=A2[r][c];} aug[r][2*N]=B[r]; }
        for(int col=0;col<N;++col){ int piv=col;
            for(int r=col+1;r<N;++r) if(std::fabs(aug[r][col])>std::fabs(aug[piv][col]))piv=r;
            for(int c=0;c<=2*N;++c) std::swap(aug[col][c],aug[piv][c]);
            double d=aug[col][col]; for(int c=0;c<=2*N;++c)aug[col][c]/=d;
            for(int r=0;r<N;++r){ if(r==col)continue; double f=aug[r][col];
                if(f==0)continue; for(int c=0;c<=2*N;++c)aug[r][c]-=f*aug[col][c]; } }
        int inact=1-active_.load(std::memory_order_relaxed);
        for(int r=0;r<N;++r){ for(int c=0;c<N;++c)M_[inact][r][c]=aug[r][N+c];
            K_[inact][r]=aug[r][2*N]; }
        active_.store(inact,std::memory_order_release);
    }
    double step(double u){
        int a=active_.load(std::memory_order_acquire);
        const auto&M=M_[a];const auto&K=K_[a];
        std::array<double,N> xn{};
        for(int r=0;r<N;++r){ double acc=K[r]*(u+u1_);
            for(int c=0;c<N;++c)acc+=M[r][c]*x_[c]; xn[r]=acc; }
        x_=xn; u1_=u; return x_[5];             // treble wiper (node 6)
    }
private:
    static constexpr int N=9;
    std::array<std::array<std::array<double,N>,N>,2> M_{};
    std::array<std::array<double,N>,2> K_{};
    std::atomic<int> active_{0};
    std::array<double,N> x_{}; double u1_=0;
};

class Amp {
public:
    Amp(double fs, double gain = 0.6, double treble = 0.6, double bass = 0.4,
        double mid = 0.5, double master = 0.7)
        : fs_(fs),
          // cascaded high-gain 12AX7 stages (Marshall: 100k plate, small
          // cathode R -> high gain); V1 bright, V2 driver
          v1_(T12AX7(), fs, 320.0, 100e3, 820.0, 330e-6, 0.022e-6, 1e6),
          v2_(T12AX7(), fs, 320.0, 100e3, 2700.0, 0.68e-6, 0.022e-6, 1e6),
          cf_(T12AX7(), fs, 320.0, 100e3),      // V2B cathode follower
          tone_(fs, treble, bass, mid),
          pi_(fs, 400.0),                       // LTP PI on its own B+ node
          // EL34 push-pull, Marshall OT (Raa ~3.4k) + higher B+
          power_(fs, 3400.0, 22.0, 22.0, 40.0, princeton::PEL34(), 470.0, 480.0),
          psu_(fs),
          gain_(gain), master_(master) {
        setPresence(presence_);                      // sets the NFB presence corner
        power_.setOtBw(4500.0);                      // Marshall OT darker than the
                                                     // Princeton: rolls the presence
                                                     // rise into a peak above ~5 kHz
        double wb = 2.0 * princeton::kPi * 3000.0;   // bright-cap corner
        brA_ = wb / (fs + wb);
    }

    void setGain(double g) { gain_ = g; }
    void setMaster(double m) { master_ = m; }
    void setNfb(double s) { nfbScale_ = s; }   // LTP reference-grid feedback
    void setOtDamp(double rw) { power_.setRw(rw); }   // OT Cw damping (anti-osc)
    void setFlatLoad(bool b) { power_.setFlatLoad(b); }
    void setOtBw(double hz) { power_.setOtBw(hz); }
    double piBias() const { return pi_.vkBias(); }   // debug: LTP cathode V
    void setTone(double t, double b, double m) { tone_.rebuild(fs_, t, b, m); }
    // Presence — the REAL mechanism: the 22k pot + 0.1µF in the NFB tail shunts
    // HF feedback to ground, so above the corner the loop gain falls and the
    // closed-loop gain rises back toward open-loop -> a PRESENCE PEAK (with the
    // deep Marshall NFB this is the +12 dB @2-4 kHz "bark", rolled off >5 kHz by
    // the OT). Higher presence = lower feedback-LP corner = broader/taller peak.
    // (Replaces the old pre-PI shelf approximation.)
    void setPresence(double p) {
        presence_ = std::clamp(p, 0.0, 1.0);
        double fc = 6000.0 * std::pow(900.0 / 6000.0, presence_);   // 6k..0.9k
        double wc = 2.0 * princeton::kPi * fc;
        fbA_ = wc / (fs_ + wc);
    }
    // High Treble: channel I's bright voicing (470p/.005u bright caps across
    // Volume I) — blends a high-passed component into the gain path.
    void setBright(double b) { bright_ = std::clamp(b, 0.0, 1.0); }

    void processBlock(const double* in, double* out, int n) {
        for (int i = 0; i < n; ++i) {
            // preamp: V1 -> bright Volume I -> V2 -> tone stack -> PI. (The
            // real V2B cathode follower is a unity buffer, folded into the
            // tone-stack drive.)
            double y = v1_.step(in[i]);
            // High Treble channel bright caps: HP'd blend across the volume
            brHp_ += brA_ * (y - brHp_);
            y = y + bright_ * 1.5 * (y - brHp_);
            y = v2_.step(y * (0.1 + 1.4 * gain_));
            y = cf_.step(y);                    // V2B cathode-follower buffer
            y = tone_.step(y);
            // presence is now in the NFB loop (see setPresence), not a pre-shelf.
            double outT, outB;
            // LTP has real gain (~25), so the makeup is far smaller than the
            // unity cathodyne's used to be. NFB is injected into the LTP's
            // reference grid (physically where a Marshall's feedback lands);
            // full-band like the Princeton fix (OT Cw keeps the loop stable).
            fbLp_ += fbA_ * (vspk_ - fbLp_);
            pi_.step(y * 0.30, outT, outB, fbLp_ * nfbScale_);
            double vA, vB;
            psu_.step(power_.iPlates(), power_.iScreens(), vA, vB);
            vspk_ = power_.step(outT, outB, vA, vB);
            out[i] = vspk_;              // raw speaker V; engine applies master
        }
    }

private:
    double fs_;
    princeton::TriodeStage v1_, v2_;
    princeton::CathodeFollower cf_;
    MarshallTone tone_;
    princeton::LTP pi_;                    // real long-tailed-pair PI
    princeton::PowerStage power_;
    princeton::PSU psu_;
    double gain_, master_, vspk_ = 0.0, fbLp_ = 0.0, fbA_ = 0.1;
    // LTP reference-grid NFB. Sign/amount pinned by test/diag_nfb: NEGATIVE is
    // real negative feedback (positive self-oscillates -> +0.06 rings at 0.22
    // RMS); -0.08 gives ~3 dB, a light Marshall-style loop, burst-stable at
    // both 48k/96k across -0.06..-0.20. Old Plexi computed feedback but never
    // injected it (ran open-loop) -- the LTP gives it the physically-correct
    // injection point (the cold grid).
    double nfbScale_ = -0.08;
    double presence_ = 0.5, bright_ = 0.3;
    double prA_ = 0.1, prLp_ = 0.0, brA_ = 0.1, brHp_ = 0.0;
};

}  // namespace plexi
