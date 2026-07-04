# Head-vs-NAM matcher: overlay my head's frequency response + waveform against
# the real Princeton head NAM, save a PNG (since I can't listen). Iterate my
# head config until the curves overlap ("similar ballpark").
import json, numpy as np, struct, sys
import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
from scipy.signal import correlate as xc
SR=48000
def load(p):
    b=open(p,'rb').read(); i=b.find(b'data'); n=struct.unpack('<I',b[i+4:i+8])[0]
    d=b[i+8:i+8+n]; fmt=struct.unpack('<H',b[20:22])[0]; bits=struct.unpack('<H',b[34:36])[0]
    if fmt==3: return np.frombuffer(d,'<f4').astype(float)
    if bits==16: return np.frombuffer(d,'<i2').astype(float)/32768
    return np.frombuffer(d,'<f4').astype(float)
R=r"D:\sd1\vst\renders"
x=load(R+r"\bench_in.wav"); nam=load(R+r"\bench_nam.wav"); my=load(R+r"\bench_myhead.wav")
meta=json.load(open(R+r"\bench_meta.json"))
N=min(len(x),len(nam),len(my)); x,nam,my=x[:N],nam[:N],my[:N]
def lag(a):
    a0,a1=meta["sweep_lo"]; A=a[a0:a1]-a[a0:a1].mean(); Rr=x[a0:a1]-x[a0:a1].mean()
    c=xc(A,Rr,'full','fft'); m=len(A)-1; return int(np.argmax(c[m-800:m+800])-800)
nam=np.roll(nam,-lag(nam)); my=np.roll(my,-lag(my))

from scipy.signal import welch
def fr(sig,seg):
    # robust voicing = Welch average PSD on the DI segment (real playing);
    # transients/hum spikes average out, unlike the sweep out/in ratio.
    a0,a1=meta[seg]; s=sig[a0:a1]
    f,P=welch(s,SR,nperseg=8192,noverlap=4096)
    return f, np.sqrt(P)
def smooth_db(f,H,pts):
    out=[]
    for fq in pts:
        lo,hi=fq/2**(1/6),fq*2**(1/6); m=(f>=lo)&(f<=hi)
        out.append(20*np.log10(np.mean(H[m])+1e-12))
    return np.array(out)
pts=np.geomspace(50,12000,60)
f,Hn=fr(nam,"di"); _,Hm=fr(my,"di")
dn=smooth_db(f,Hn,pts); dm=smooth_db(f,Hm,pts)
k1=np.argmin(np.abs(pts-1000)); dn-=dn[k1]; dm-=dm[k1]   # normalise @1kHz
band=(pts>=100)&(pts<=6000); gap=np.mean(np.abs(dm-dn)[band])
print(f"HEAD freq-response mean |diff| 100-6kHz = {gap:.1f} dB  (ballpark target < ~3 dB)")
for fq in [80,160,320,640,1200,2400,4800]:
    kk=np.argmin(np.abs(pts-fq)); print(f"  {fq:>5}Hz  NAM {dn[kk]:+5.1f}  MINE {dm[kk]:+5.1f}  diff {dm[kk]-dn[kk]:+5.1f}")

fig,ax=plt.subplots(2,1,figsize=(11,8))
ax[0].semilogx(pts,dn,label="NAM head (real Princeton Clean V4T4)",lw=2)
ax[0].semilogx(pts,dm,label="MY head",lw=2)
ax[0].set_title(f"HEAD voicing: DI avg spectrum (norm @1kHz)  |  mean gap 100-6k = {gap:.1f} dB")
ax[0].set_xlabel("Hz"); ax[0].set_ylabel("dB"); ax[0].grid(True,which='both',alpha=.3); ax[0].legend(); ax[0].set_ylim(-40,25)
# waveform overlay: a clean tone, level-normalised
a0,a1=meta["tone_0.35"]; sn=nam[a0:a1]; sm=my[a0:a1]
sm=sm*np.sqrt(np.mean(sn**2)/np.mean(sm**2))
o=4000
ax[1].plot(sn[o:o+900],label="NAM",lw=1.5); ax[1].plot(sm[o:o+900],label="MINE(lvl-matched)",lw=1.2,alpha=.8)
ax[1].set_title("waveform: 220Hz clean tone (level-matched)"); ax[1].legend(); ax[1].grid(True,alpha=.3)
plt.tight_layout(); plt.savefig(R+r"\head_match.png",dpi=90); print("wrote head_match.png")
