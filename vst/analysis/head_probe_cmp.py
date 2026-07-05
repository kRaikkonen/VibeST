# CORRECT head comparison: clean multi-tone gain curves (my head vs NAM),
# not the artifact-prone DI-Welch. Renders must exist:
#   renders/nam_probe.wav, renders/head_probe.wav (from probe.wav)
import numpy as np, struct, json
import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
SR=48000
def load(p):
    b=open(p,'rb').read(); i=b.find(b'data'); n=struct.unpack('<I',b[i+4:i+8])[0]
    d=b[i+8:i+8+n]; fmt=struct.unpack('<H',b[20:22])[0]; ch=struct.unpack('<H',b[22:24])[0]
    v=np.frombuffer(d,'<f4').astype(float) if fmt==3 else np.frombuffer(d,'<i2').astype(float)/32768
    return v[::ch] if ch>1 else v            # de-interleave stereo -> L
J=json.load(open(r"D:\sd1\vst\renders\probe.json")); freqs=J["freqs"]; meta=J["meta"]
xin=load(r"D:\sd1\vst\renders\probe.wav")
def curve(path):
    y=load(path); n=min(len(xin),len(y)); xi,yy=xin[:n],y[:n]; g=[]
    for f in freqs:
        a0,a1=meta[f"f{f}"]
        def A(sig):
            s=sig[a0+2000:a1-2000]; t=np.arange(len(s))/SR
            return np.abs(np.sum(s*np.exp(-2j*np.pi*f*t)))/len(s)*2
        gi=A(xi); go=A(yy); g.append(20*np.log10(go/gi) if gi>0 else -120)
    g=np.array(g); k1=min(range(len(freqs)),key=lambda i:abs(freqs[i]-1000)); return g-g[k1]
gn=curve(r"D:\sd1\vst\renders\nam_probe.wav")
gm=curve(r"D:\sd1\vst\renders\head_probe.wav")
band=[i for i,f in enumerate(freqs) if 200<=f<=6000]
gap=np.mean(np.abs(gm[band]-gn[band]))
print(f"HEAD clean-tone gain gap 200-6k = {gap:.1f} dB")
plt.figure(figsize=(11,5))
plt.semilogx(freqs,gn,'o-',label="NAM head (real Princeton Clean)",lw=2)
plt.semilogx(freqs,gm,'s-',label="MY head",lw=2)
plt.title(f"HEAD clean-tone transfer function (dB rel 1kHz)  |  gap 200-6k = {gap:.1f} dB")
plt.xlabel("Hz"); plt.ylabel("dB"); plt.grid(True,which='both',alpha=.3); plt.legend(); plt.ylim(-30,25)
plt.tight_layout(); plt.savefig(r"D:\sd1\vst\renders\head_probe.png",dpi=90); print("wrote head_probe.png")
