import sys, numpy as np, struct, json
from scipy.signal import welch
def load(p):
    b=open(p,'rb').read(); i=b.find(b'data'); n=struct.unpack('<I',b[i+4:i+8])[0]
    return np.frombuffer(b[i+8:i+8+n],'<f4').astype(float)
meta=json.load(open(r"D:\sd1\vst\renders\bench_meta.json"))
y=load(sys.argv[1]); a0,a1=meta['di']; s=y[a0:a1]
f,P=welch(s,48000,nperseg=16384)
def at(fq):
    m=(f>=fq/1.06)&(f<=fq*1.06); return 10*np.log10(np.mean(P[m])+1e-20)
r1=at(1000)
print(f"  4800/1k {at(4800)-r1:+6.1f}   2400/1k {at(2400)-r1:+6.1f}   1200/1k {at(1200)-r1:+6.1f}")
