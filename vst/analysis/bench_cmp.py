# Ground-truth bench, part 2: compare my white-box amp vs the real amp (NAM).
# Loads renders/bench_{in,nam,my}.wav + bench_meta.json and runs the three
# measurement-bench tests: frequency response (sweeps), harmonic character
# (stepped tones), and a null on real playing (DI). Prints a gap report.
import json, numpy as np, struct, sys
from scipy.signal import correlate as _xc

SR=48000
def load(path):
    with open(path,'rb') as f: b=f.read()
    # find 'data'
    i=b.find(b'data'); n=struct.unpack('<I',b[i+4:i+8])[0]; data=b[i+8:i+8+n]
    fmt=struct.unpack('<H',b[20:22])[0]; bits=struct.unpack('<H',b[34:36])[0]
    if fmt==3: return np.frombuffer(data,dtype='<f4').astype(np.float64)
    if bits==16: return np.frombuffer(data,dtype='<i2').astype(np.float64)/32768
    return np.frombuffer(data,dtype='<f4').astype(np.float64)

x=load(r"D:\sd1\vst\renders\bench_in.wav")
nam=load(r"D:\sd1\vst\renders\bench_nam.wav")
my=load(r"D:\sd1\vst\renders\bench_my.wav")
meta=json.load(open(r"D:\sd1\vst\renders\bench_meta.json"))
N=min(len(x),len(nam),len(my)); x,nam,my=x[:N],nam[:N],my[:N]

def lag(a,ref):
    # raw cross-correlation on the low sweep (clean, broadband) in a tight
    # window -- main-path amp latency is only the up/down FIR group delay.
    a0,a1=meta["sweep_lo"]; A=a[a0:a1]-a[a0:a1].mean(); R=ref[a0:a1]-ref[a0:a1].mean()
    c=_xc(A,R,mode='full',method='fft'); mid=len(A)-1; win=800
    return int(np.argmax(c[mid-win:mid+win])-win)
Lnam=lag(nam,x); Lmy=lag(my,x)
print(f"latency vs input: NAM {Lnam} smp ({1000*Lnam/SR:.1f}ms)  MINE {Lmy} smp ({1000*Lmy/SR:.1f}ms)")
def shift(a,L): return np.roll(a,-L)
nam_a=shift(nam,Lnam); my_a=shift(my,Lmy)

def harm(sig, a0,a1, f0=220):
    s=sig[a0:a1]; s=s[len(s)//4:len(s)//4+8192]
    if len(s)<8192: s=np.pad(s,(0,8192-len(s)))
    w=s*np.hanning(len(s)); sp=np.abs(np.fft.rfft(w)); fr=np.fft.rfftfreq(8192,1/SR)
    H=[sp[np.argmin(np.abs(fr-f0*k))] for k in range(1,9)]
    rms=np.sqrt(np.mean(sig[a0:a1]**2))
    thd=np.sqrt(sum(h*h for h in H[1:]))/max(H[0],1e-12)
    even=np.sqrt(H[1]**2+H[3]**2+H[5]**2); odd=np.sqrt(H[2]**2+H[4]**2+H[6]**2)
    return rms, thd, [h/max(H[0],1e-12) for h in H], even/max(odd,1e-12)

print("\n=== TONE TRANSFER (220Hz, rising level): compression + distortion char ===")
print(f"{'level':>6} | {'NAM out':>8} {'THD':>6} {'H2':>5} {'H3':>5} {'ev/od':>5} | {'MINE out':>8} {'THD':>6} {'H2':>5} {'H3':>5} {'ev/od':>5}")
namouts=[]; myouts=[]; lv=[]
for a in [0.02,0.05,0.1,0.2,0.35,0.5,0.7]:
    a0,a1=meta[f"tone_{a}"]
    rn,tn,Hn,eon=harm(nam_a,a0,a1); rm,tm,Hm,eom=harm(my_a,a0,a1)
    namouts.append(rn); myouts.append(rm); lv.append(a)
    print(f"{a:>6} | {rn:>8.4f} {100*tn:>5.1f}% {Hn[1]:>5.2f} {Hn[2]:>5.2f} {eon:>5.2f} | {rm:>8.4f} {100*tm:>5.1f}% {Hm[1]:>5.2f} {Hm[2]:>5.2f} {eom:>5.2f}")
# normalise transfer curves to clean point and compare compression shape
namouts=np.array(namouts); myouts=np.array(myouts); lv=np.array(lv)
nn=namouts/namouts[0]*lv[0]; mm=myouts/myouts[0]*lv[0]   # gain-normalised at cleanest
print("compression (out/in, normalised to cleanest = 1.0; <1 = compressing):")
print("  level :", " ".join(f"{a:>6}" for a in lv))
print("  NAM   :", " ".join(f"{v/lv[i]/(namouts[0]/lv[0]):>6.2f}" for i,v in enumerate(namouts)))
print("  MINE  :", " ".join(f"{v/lv[i]/(myouts[0]/lv[0]):>6.2f}" for i,v in enumerate(myouts)))

def freqresp(sig, seg):
    # FFT the WHOLE sweep segment so every frequency is actually excited
    a0,a1=meta[seg]; s=sig[a0:a1]; xi=x[a0:a1]; n=len(s)
    w=np.hanning(n)
    S=np.abs(np.fft.rfft(s*w)); X=np.abs(np.fft.rfft(xi*w))
    fr=np.fft.rfftfreq(n,1/SR); H=S/(X+1e-6*X.max())
    return fr,H
print("\n=== FREQ RESPONSE (out/in magnitude, dB rel 1kHz) ===")
for seg,lbl in [("sweep_lo","clean"),("sweep_hi","driven")]:
    fr,Hn=freqresp(nam_a,seg); _,Hm=freqresp(my_a,seg)
    def at(H,f):
        k=np.argmin(np.abs(fr-f)); import numpy as _n
        # smooth 1/6 oct
        lo,hi=f/2**(1/12),f*2**(1/12); m=(fr>=lo)&(fr<=hi)
        return 20*np.log10(np.mean(H[m])+1e-12)
    ref_n=at(Hn,1000); ref_m=at(Hm,1000)
    print(f"[{lbl}] {'freq':>6} " + " ".join(f"{f:>6}" for f in [80,160,320,640,1000,2000,4000,8000]))
    print(f"       NAM   " + " ".join(f"{at(Hn,f)-ref_n:>+6.1f}" for f in [80,160,320,640,1000,2000,4000,8000]))
    print(f"       MINE  " + " ".join(f"{at(Hm,f)-ref_m:>+6.1f}" for f in [80,160,320,640,1000,2000,4000,8000]))

print("\n=== NULL on real DI (level-matched) ===")
a0,a1=meta["di"]; sn=nam_a[a0:a1]; sm=my_a[a0:a1]
sm=sm*np.sqrt(np.mean(sn**2)/np.mean(sm**2))   # match RMS
res=sn-sm
print(f"  NAM RMS {np.sqrt(np.mean(sn**2)):.4f}  residual RMS {np.sqrt(np.mean(res**2)):.4f} = {100*np.sqrt(np.mean(res**2))/np.sqrt(np.mean(sn**2)):.0f}% of signal")
# residual by band
sp=np.abs(np.fft.rfft(res*np.hanning(len(res)))); fr=np.fft.rfftfreq(len(res),1/SR)
for lo,hi in [(0,200),(200,800),(800,3000),(3000,8000),(8000,24000)]:
    m=(fr>=lo)&(fr<hi); e=np.sqrt(np.mean(sp[m]**2))
    tot=np.sqrt(np.mean(sp**2)); print(f"  residual {lo:>5}-{hi:<5}Hz : {100*e/tot:>4.0f}% of residual energy")
