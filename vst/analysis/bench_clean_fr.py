# Linear freq response: my amp at a CLEAN (tiny) drive vs the NAM head, to see
# if my tone-stack / coupling voicing matches -- separate from the gain-staging
# gap. Uses bench_my_clean.wav (rendered at intrim=0.015).
import json, numpy as np, struct
from scipy.signal import correlate as _xc
SR=48000
def load(p):
    b=open(p,'rb').read(); i=b.find(b'data'); n=struct.unpack('<I',b[i+4:i+8])[0]
    d=b[i+8:i+8+n]; fmt=struct.unpack('<H',b[20:22])[0]; bits=struct.unpack('<H',b[34:36])[0]
    if fmt==3: return np.frombuffer(d,dtype='<f4').astype(float)
    if bits==16: return np.frombuffer(d,dtype='<i2').astype(float)/32768
    return np.frombuffer(d,dtype='<f4').astype(float)
x=load(r"D:\sd1\vst\renders\bench_in.wav"); nam=load(r"D:\sd1\vst\renders\bench_nam.wav")
my=load(r"D:\sd1\vst\renders\bench_my_clean.wav"); meta=json.load(open(r"D:\sd1\vst\renders\bench_meta.json"))
N=min(len(x),len(nam),len(my)); x,nam,my=x[:N],nam[:N],my[:N]
def lag(a):
    a0,a1=meta["sweep_lo"]; A=a[a0:a1]-a[a0:a1].mean(); R=x[a0:a1]-x[a0:a1].mean()
    c=_xc(A,R,'full','fft'); m=len(A)-1; return int(np.argmax(c[m-800:m+800])-800)
nam=np.roll(nam,-lag(nam)); my=np.roll(my,-lag(my))
def fr(sig,seg):
    a0,a1=meta[seg]; s=sig[a0:a1]; xi=x[a0:a1]; n=len(s); w=np.hanning(n)
    S=np.abs(np.fft.rfft(s*w)); X=np.abs(np.fft.rfft(xi*w)); f=np.fft.rfftfreq(n,1/SR)
    return f, S/(X+1e-6*X.max())
def at(f,H,fq):
    lo,hi=fq/2**(1/12),fq*2**(1/12); m=(f>=lo)&(f<=hi); return 20*np.log10(np.mean(H[m])+1e-12)
pts=[80,120,160,240,320,480,640,1000,1500,2000,3000,4000,6000,8000,12000]
f,Hn=fr(nam,"sweep_lo"); _,Hm=fr(my,"sweep_lo")
rn=at(f,Hn,1000); rm=at(f,Hm,1000)
print("=== LINEAR freq response (clean sweep, dB rel 1kHz): my TONE voicing vs real head ===")
print("freq :", " ".join(f"{p:>6}" for p in pts))
print("NAM  :", " ".join(f"{at(f,Hn,p)-rn:>+6.1f}" for p in pts))
print("MINE :", " ".join(f"{at(f,Hm,p)-rm:>+6.1f}" for p in pts))
print("diff :", " ".join(f"{(at(f,Hm,p)-rm)-(at(f,Hn,p)-rn):>+6.1f}" for p in pts))
# also report my clean-drive compression at the tones to confirm it's clean now
def rms(sig,seg): a0,a1=meta[seg]; return np.sqrt(np.mean(sig[a0:a1]**2))
print("\nmy clean-drive tone out RMS (should track input linearly if truly clean):")
for a in [0.05,0.2,0.5]: print(f"  tone {a}: {rms(my,'tone_'+str(a)):.5f}")
