import sys, wave, array
path, t_lo, t_hi = sys.argv[1], float(sys.argv[2]), float(sys.argv[3])
blkms = int(sys.argv[4]) if len(sys.argv) > 4 else 50
w = wave.open(path, "rb")
ch, sw, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
lo = int(t_lo*rate); hi = min(n, int(t_hi*rate)); w.setpos(lo)
a = array.array('h'); a.frombytes(w.readframes(hi-lo))
blk = int(blkms/1000.0*rate)
def env(c):
    s = a[c::ch]; e=[]
    for i in range(0, len(s)-blk, blk):
        acc = 0
        for j in range(i, i+blk): acc += s[j]*s[j]
        e.append((acc/blk) ** 0.5)
    return e
L = env(0)
pk = max(max(L), 1.0)
spark = " .:-=+*#@"
line = "".join(spark[min(8, int(9*v/pk))] for v in L)
print(f"{path.split(chr(92))[-1]}  {t_lo}-{t_hi}s @ {blkms}ms  peak={pk:.0f}  (each char = {blkms}ms)")
print("|" + line + "|")
# label tick every 0.5s
ticks = ""
for i in range(len(L)):
    t = t_lo + i*blkms/1000.0
    ticks += ("^" if abs((t*2) - round(t*2)) < (blkms/1000.0) else " ")
print(" " + ticks + "   (^ = every 0.5s)")
