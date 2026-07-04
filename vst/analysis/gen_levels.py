# 1 kHz clean tone at rising input LEVELS (volts) to find a stage's clean
# headroom (THD vs level). Writes levels.wav + levels.json.
import numpy as np, struct, json
SR=48000
levels=[0.01,0.02,0.05,0.1,0.15,0.2,0.3,0.5,0.7]
segs=[]; meta={}; cur=0
for v in levels:
    n=int(0.4*SR); t=np.arange(n)/SR
    env=np.ones(n); r=int(0.02*SR); env[:r]=0.5-0.5*np.cos(np.pi*np.arange(r)/r); env[-r:]=env[:r][::-1]
    s=(v*np.sin(2*np.pi*1000*t)*env).astype(np.float32)
    meta[f"L{v}"]=[cur,cur+len(s)]; segs.append(s); cur+=len(s)
    segs.append(np.zeros(int(0.1*SR),dtype=np.float32)); cur+=int(0.1*SR)
x=np.concatenate(segs).astype(np.float32); data=x.tobytes()
with open(r"D:\sd1\vst\renders\levels.wav",'wb') as fp:
    fp.write(b'RIFF'); fp.write(struct.pack('<I',36+len(data))); fp.write(b'WAVE')
    fp.write(b'fmt '); fp.write(struct.pack('<IHHIIHH',16,3,1,SR,SR*4,4,32))
    fp.write(b'data'); fp.write(struct.pack('<I',len(data))); fp.write(data)
json.dump({"levels":levels,"meta":meta},open(r"D:\sd1\vst\renders\levels.json","w"))
print("levels.wav:",len(x)/SR,"s")
