# Run the clean multi-tone probe through the real Princeton NAM head, so its
# gain-vs-frequency can be measured the SAME clean way as my head (avoiding the
# DI-Welch artifact). Writes nam_probe.wav.
import json, numpy as np, torch, struct, sys
from nam.models._from_nam import init_from_nam
def load(p):
    b=open(p,'rb').read(); i=b.find(b'data'); n=struct.unpack('<I',b[i+4:i+8])[0]
    return np.frombuffer(b[i+8:i+8+n],'<f4').astype(np.float32)
x=load(r"D:\sd1\vst\renders\probe.wav")
namf=sys.argv[1] if len(sys.argv)>1 else r"C:\Users\Ziyu.Liu25\Documents\xwechat_files\wxid_h7gd25u6tiug12_70c5\msg\file\2026-07\pp(1)\pp\1964 Fender Princeton Head NAM\64 Princeton Clean - Vol 4, Tone 4.nam"
d=json.load(open(namf)); sm=max(d["config"]["submodels"],key=lambda s:s["max_value"])["model"]
m=init_from_nam(sm); m.eval()
with torch.no_grad(): y=m(torch.from_numpy(x)).cpu().numpy().ravel()
if len(y)<len(x): y=np.concatenate([y,np.zeros(len(x)-len(y),dtype=np.float32)])
y=y[:len(x)].astype(np.float32); data=y.tobytes()
with open(r"D:\sd1\vst\renders\nam_probe.wav",'wb') as fp:
    fp.write(b'RIFF'); fp.write(struct.pack('<I',36+len(data))); fp.write(b'WAVE')
    fp.write(b'fmt '); fp.write(struct.pack('<IHHIIHH',16,3,1,48000,48000*4,4,32))
    fp.write(b'data'); fp.write(struct.pack('<I',len(data))); fp.write(data)
print("nam_probe.wav written")
