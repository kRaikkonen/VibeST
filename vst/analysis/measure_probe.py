import sys, numpy as np, struct, json
SR=48000
def load(p):
    b=open(p,'rb').read(); i=b.find(b'data'); n=struct.unpack('<I',b[i+4:i+8])[0]
    d=b[i+8:i+8+n]; fmt=struct.unpack('<H',b[20:22])[0]; bits=struct.unpack('<H',b[34:36])[0]
    if fmt==3: return np.frombuffer(d,'<f4').astype(float)
    if bits==24:
        a=np.frombuffer(d[:len(d)//3*3],np.uint8).reshape(-1,3).astype(np.int32)
        v=(a[:,0]|(a[:,1]<<8)|(a[:,2]<<16)); return np.where(v&0x800000,v-(1<<24),v)/8388608.0
    return np.frombuffer(d,'<i2').astype(float)/32768
J=json.load(open(r"D:\sd1\vst\renders\probe.json")); freqs=J["freqs"]; meta=J["meta"]
xin=load(r"D:\sd1\vst\renders\probe.wav")
y=load(sys.argv[1])
n=min(len(xin),len(y)); xin,y=xin[:n],y[:n]
def amp_at(sig,a0,a1,f):
    s=sig[a0+2000:a1-2000]              # skip fades
    t=np.arange(len(s))/SR; c=np.exp(-2j*np.pi*f*t)
    return np.abs(np.sum(s*c))/len(s)*2  # amplitude at exactly f
g=[]
for f in freqs:
    a0,a1=meta[f"f{f}"]; gi=amp_at(xin,a0,a1,f); go=amp_at(y,a0,a1,f)
    g.append(20*np.log10(go/gi) if gi>0 else 0)
k1=min(range(len(freqs)),key=lambda i:abs(freqs[i]-1000)); ref=g[k1]
print(f"stage gain vs freq (dB rel 1kHz)  [{sys.argv[1].split(chr(92))[-1]}]:")
for f,gg in zip(freqs,g):
    bar="#"*max(0,int((gg-ref)/2)+1) if gg-ref>-2 else ""
    print(f"  {f:>6} Hz  {gg-ref:+6.1f} dB {bar}")
