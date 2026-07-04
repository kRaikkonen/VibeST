# Cab-vs-IR matcher: my synthetic C10R cab IR vs the REAL 1964 Princeton
# Jensen P10R IR. Cabs are linear -> compare the IR magnitude spectra + the
# impulse waveforms. PNG out.
import numpy as np, struct
import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
SR=48000
def load(p):
    b=open(p,'rb').read(); i=b.find(b'data'); n=struct.unpack('<I',b[i+4:i+8])[0]
    d=b[i+8:i+8+n]; fmt=struct.unpack('<H',b[20:22])[0]; bits=struct.unpack('<H',b[34:36])[0]
    if fmt==3: return np.frombuffer(d,'<f4').astype(float)
    if bits==16: return np.frombuffer(d,'<i2').astype(float)/32768
    if bits==24:
        a=np.frombuffer(d[:len(d)//3*3],dtype=np.uint8).reshape(-1,3).astype(np.int32)
        v=(a[:,0]|(a[:,1]<<8)|(a[:,2]<<16)); v=np.where(v&0x800000,v-(1<<24),v); return v/8388608.0
    if bits==32: return np.frombuffer(d,'<i4').astype(float)/2147483648
    return np.frombuffer(d,'<f4').astype(float)
R=r"D:\sd1\vst\renders\ir"
mine=load(R+r"\synth_c10r.wav")
real=load(R+r"\real_p10r_sm57cap.wav")
def spec(h):
    N=1;
    while N<len(h)*2: N<<=1
    H=np.abs(np.fft.rfft(h,N)); f=np.fft.rfftfreq(N,1/SR); return f,H
def sm(f,H,pts):
    o=[]
    for fq in pts:
        lo,hi=fq/2**(1/6),fq*2**(1/6); m=(f>=lo)&(f<=hi); o.append(20*np.log10(np.mean(H[m])+1e-12))
    return np.array(o)
pts=np.geomspace(50,16000,70)
fm,Hm=spec(mine); fr,Hr=spec(real)
dm=sm(fm,Hm,pts); dr=sm(fr,Hr,pts)
k=np.argmin(np.abs(pts-1000)); dm-=dm[k]; dr-=dr[k]
band=(pts>=100)&(pts<=6000); gap=np.mean(np.abs(dm-dr)[band])
print(f"CAB IR spectrum mean |diff| 100-6kHz = {gap:.1f} dB")
for fq in [80,200,500,1000,2000,3500,5000,8000]:
    kk=np.argmin(np.abs(pts-fq)); print(f"  {fq:>5}Hz  REAL {dr[kk]:+5.1f}  MINE {dm[kk]:+5.1f}  diff {dm[kk]-dr[kk]:+5.1f}")
fig,ax=plt.subplots(2,1,figsize=(11,8))
ax[0].semilogx(pts,dr,label="REAL 1964 Princeton P10R (SM57 cap)",lw=2)
ax[0].semilogx(pts,dm,label="MY synth C10R cab",lw=2)
ax[0].set_title(f"CAB IR magnitude (norm @1kHz)  |  mean gap 100-6k = {gap:.1f} dB")
ax[0].set_xlabel("Hz"); ax[0].set_ylabel("dB"); ax[0].grid(True,which='both',alpha=.3); ax[0].legend(); ax[0].set_ylim(-40,15)
n=int(0.006*SR)
ax[1].plot(real[:n]/np.max(np.abs(real)),label="REAL IR",lw=1.3)
ax[1].plot(mine[:n]/np.max(np.abs(mine)),label="MY IR",lw=1.1,alpha=.85)
ax[1].set_title("impulse response (first 6ms, peak-norm)"); ax[1].legend(); ax[1].grid(True,alpha=.3)
plt.tight_layout(); plt.savefig(r"D:\sd1\vst\renders\cab_match.png",dpi=90); print("wrote cab_match.png")
