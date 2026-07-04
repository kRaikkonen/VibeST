import sys, numpy as np, struct, json
SR=48000
def load(p):
    b=open(p,'rb').read(); i=b.find(b'data'); n=struct.unpack('<I',b[i+4:i+8])[0]
    d=b[i+8:i+8+n]; fmt=struct.unpack('<H',b[20:22])[0]
    return np.frombuffer(d,'<f4').astype(float) if fmt==3 else np.frombuffer(d,'<i2').astype(float)/32768
J=json.load(open(r"D:\sd1\vst\renders\levels.json")); levels=J["levels"]; meta=J["meta"]
y=load(sys.argv[1])
print(f"1kHz clean-headroom (THD vs input level)  [{sys.argv[1].split(chr(92))[-1]}]:")
for v in levels:
    a0,a1=meta[f"L{v}"]; s=y[a0+3000:a1-3000]
    if len(s)<8192: continue
    w=8192; s=s[:w]*np.hanning(w); sp=np.abs(np.fft.rfft(s)); fr=np.fft.rfftfreq(w,1/SR)
    def h(k): return sp[np.argmin(np.abs(fr-1000*k))]
    H1=h(1); thd=np.sqrt(sum(h(k)**2 for k in range(2,10)))/max(H1,1e-12)
    print(f"  in {v:>5} V   out-1k {H1:9.4f}   THD {100*thd:6.2f}%   {'CLIP' if thd>0.1 else 'clean' if thd<0.02 else '~'}")
