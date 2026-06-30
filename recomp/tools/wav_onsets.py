import sys, wave, array
path = sys.argv[1]
dur  = float(sys.argv[2]) if len(sys.argv) > 2 else 14.0
w = wave.open(path, "rb")
ch, sw, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
start = max(0, n - int(dur * rate))
w.setpos(start)
a = array.array('h'); a.frombytes(w.readframes(n - start))
t0 = start / rate
print(f"ch={ch} rate={rate} bits={sw*8} total={n/rate:.1f}s, analyzing last {dur:.0f}s (from t={t0:.1f}s)")

blkms = 15
blk = int(blkms/1000.0*rate)
def env_of(chan):
    s = a[chan::ch]
    e = []
    for i in range(0, len(s)-blk, blk):
        acc = 0
        for j in range(i, i+blk): acc += s[j]*s[j]
        e.append((acc/blk) ** 0.5)
    return e

def onsets(e):
    pk = max(e) if e else 1.0
    thr = pk * 0.15
    out = []
    for i in range(2, len(e)):
        rising = e[i] > thr and e[i] > e[i-1]*1.5 and e[i-1] >= e[i-2]
        if rising and (not out or (i - out[-1]) * blkms > 90):   # 90ms refractory
            out.append(i)
    return out, pk

for name, c in (("LEFT (AUD0+AUD3 = trumpet)", 0), ("RIGHT (AUD1)", min(1, ch-1))):
    e = env_of(c); on, pk = onsets(e)
    times = [round(t0 + o*blkms/1000.0, 2) for o in on]
    gaps  = [round((on[i]-on[i-1])*blkms/1000.0, 2) for i in range(1, len(on))]
    print(f"\n{name}: peakRMS={pk:.0f}  {len(on)} onsets")
    print("  onset times(s):", times)
    print("  inter-onset(s):", gaps)
