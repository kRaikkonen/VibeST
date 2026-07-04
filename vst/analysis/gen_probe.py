# Clean multi-tone probe to measure v1a's (or any stage's) gain vs frequency
# directly -- low level so the stage is linear. Writes probe.wav + probe.json.
import numpy as np, struct, json
SR=48000
freqs=[250,500,750,1000,1500,2000,2500,3000,3500,4000,4400,4800,5200,5600,6000,7000,8000,10000]
amp=0.03
segs=[]; meta={}; cur=0
def add(name,x):
    global cur; segs.append(x.astype(np.float32)); meta[name]=[cur,cur+len(x)]; cur+=len(x)
    segs.append(np.zeros(int(0.1*SR),dtype=np.float32)); cur+=int(0.1*SR)
for f in freqs:
    n=int(0.4*SR); t=np.arange(n)/SR
    # tiny raised-cosine fade to avoid click transients (which spread spectrum)
    env=np.ones(n); r=int(0.02*SR); env[:r]=0.5-0.5*np.cos(np.pi*np.arange(r)/r); env[-r:]=env[:r][::-1]
    add(f"f{f}", amp*np.sin(2*np.pi*f*t)*env)
x=np.concatenate(segs).astype(np.float32); data=x.tobytes()
with open(r"D:\sd1\vst\renders\probe.wav",'wb') as fp:
    fp.write(b'RIFF'); fp.write(struct.pack('<I',36+len(data))); fp.write(b'WAVE')
    fp.write(b'fmt '); fp.write(struct.pack('<IHHIIHH',16,3,1,SR,SR*4,4,32))
    fp.write(b'data'); fp.write(struct.pack('<I',len(data))); fp.write(data)
json.dump({"freqs":freqs,"meta":meta},open(r"D:\sd1\vst\renders\probe.json","w"))
print("probe.wav:",len(x)/SR,"s,",len(freqs),"tones")
