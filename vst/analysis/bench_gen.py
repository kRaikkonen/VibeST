# Ground-truth bench, part 1: build a test signal and run it through the real
# amp's NAM capture. Writes:
#   renders/bench_in.wav    (the test signal, 48k mono)
#   renders/bench_nam.wav   (real amp head output via NAM)
#   renders/bench_meta.json (segment sample ranges)
# The test signal has: a low + a high log sweep (freq response clean & driven),
# stepped 220 Hz tones over a wide level range (THD/compression transfer curve),
# and a slice of the real Suffer DI (real-playing null test).
import json, sys, numpy as np, torch, wave, struct
from nam.models._from_nam import init_from_nam

SR = 48000
def sine(f, n, a):
    t = np.arange(n)/SR; return a*np.sin(2*np.pi*f*t)
def logsweep(f0, f1, dur, a):
    n = int(dur*SR); t = np.arange(n)/SR; T = dur
    k = (f1/f0)**(1/T)
    phase = 2*np.pi*f0*(k**t - 1)/np.log(k)
    return a*np.sin(phase)

def read_wav_mono(path, want_sr=SR, start=0.0, dur=8.0):
    w = wave.open(path,'rb'); sr=w.getframerate(); ch=w.getnchannels(); sw=w.getsampwidth()
    n=w.getnframes(); raw=w.readframes(n); w.close()
    if sw==3:
        a=np.frombuffer(raw,dtype=np.uint8).reshape(-1,3).astype(np.int32)
        v=(a[:,0]|(a[:,1]<<8)|(a[:,2]<<16)); v=np.where(v&0x800000, v-(1<<24), v)/8388608.0
    elif sw==2: v=np.frombuffer(raw,dtype='<i2').astype(np.float32)/32768.0
    else: v=np.frombuffer(raw,dtype='<f4').astype(np.float32)
    v=v.reshape(-1,ch).mean(1)
    s0=int(start*sr); s1=min(len(v), s0+int(dur*sr)); v=v[s0:s1]
    if sr!=want_sr:
        x=np.arange(len(v)); xi=np.arange(0,len(v),sr/want_sr); v=np.interp(xi,x,v)
    return v.astype(np.float32)

def write_wav_f32(path, x):
    x=np.asarray(x,dtype=np.float32)
    with wave.open(path,'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes((np.clip(x,-1,1)*32767).astype('<i2').tobytes())
# f32 wav (for the amp input, keep full range): write IEEE float
def write_wav_float(path, x):
    x=np.asarray(x,dtype=np.float32); data=x.tobytes()
    with open(path,'wb') as f:
        f.write(b'RIFF'); f.write(struct.pack('<I',36+len(data))); f.write(b'WAVE')
        f.write(b'fmt '); f.write(struct.pack('<IHHIIHH',16,3,1,SR,SR*4,4,32))
        f.write(b'data'); f.write(struct.pack('<I',len(data))); f.write(data)

meta={}; segs=[]; cur=0
def add(name, x):
    global cur
    x=np.asarray(x,dtype=np.float32); segs.append(x)
    meta[name]=[cur, cur+len(x)]; cur+=len(x)
    add_sil()
def add_sil(n=int(0.15*SR)):
    global cur; segs.append(np.zeros(n,dtype=np.float32)); cur+=n

add("sweep_lo", logsweep(20,20000,3.0,0.10))
add("sweep_hi", logsweep(20,20000,3.0,0.50))
for a in [0.02,0.05,0.1,0.2,0.35,0.5,0.7]:
    add(f"tone_{a}", sine(220, int(0.6*SR), a))
di = read_wav_mono(r"C:\Users\Ziyu.Liu25\Documents\xwechat_files\wxid_h7gd25u6tiug12_70c5\msg\file\2026-07\Bad Religion - Suffer Album DI\Guitar L.wav", start=32, dur=8)
add("di", di)

x = np.concatenate(segs).astype(np.float32)
write_wav_float(r"D:\sd1\vst\renders\bench_in.wav", x)
print("test signal:", len(x)/SR, "s, peak", np.max(np.abs(x)))

# run the NAM (highest-quality submodel)
namf = sys.argv[1] if len(sys.argv)>1 else r"C:\Users\Ziyu.Liu25\Documents\xwechat_files\wxid_h7gd25u6tiug12_70c5\msg\file\2026-07\pp(1)\pp\1964 Fender Princeton Head NAM\64 Princeton Sweet Spot - Vol 7, Tone 4.nam"
d=json.load(open(namf)); sm=max(d["config"]["submodels"], key=lambda s:s["max_value"])["model"]
model=init_from_nam(sm); model.eval()
with torch.no_grad():
    y=model(torch.from_numpy(x)).cpu().numpy().ravel()
if len(y)<len(x): y=np.concatenate([y,np.zeros(len(x)-len(y),dtype=np.float32)])
write_wav_float(r"D:\sd1\vst\renders\bench_nam.wav", y[:len(x)])
json.dump(meta, open(r"D:\sd1\vst\renders\bench_meta.json","w"))
print("NAM:", namf.split(chr(92))[-1], "| out peak", float(np.max(np.abs(y))), "| segs", list(meta.keys()))
