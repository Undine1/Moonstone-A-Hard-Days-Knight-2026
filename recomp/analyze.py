"""Hardware/OS surface analysis for the Moonstone 68k binaries.

Determines, for each executable, exactly which Amiga custom-chip registers,
CIA ports, exception vectors, and AmigaOS library entry points the code
touches. This is the evidence base for the Route-A viability decision: a small,
regular set of chip features => a tractable native shim.
"""
from __future__ import annotations

import sys
from collections import Counter, defaultdict
from pathlib import Path

import capstone

import hunkload

# ---- custom chip register names (offset from 0xDFF000) ------------------
CHIP = {
    0x000: "BLTDDAT", 0x002: "DMACONR", 0x004: "VPOSR", 0x006: "VHPOSR",
    0x00A: "JOY0DAT", 0x00C: "JOY1DAT", 0x00E: "CLXDAT", 0x010: "ADKCONR",
    0x016: "POT0DAT", 0x01A: "POTGOR", 0x01C: "SERDATR", 0x01E: "INTENAR",
    0x040: "BLTCON0", 0x042: "BLTCON1", 0x044: "BLTAFWM", 0x046: "BLTALWM",
    0x048: "BLTCPTH", 0x04C: "BLTBPTH", 0x050: "BLTAPTH", 0x054: "BLTDPTH",
    0x058: "BLTSIZE", 0x060: "BLTCMOD", 0x062: "BLTBMOD", 0x064: "BLTAMOD",
    0x066: "BLTDMOD", 0x070: "BLTCDAT", 0x072: "BLTBDAT", 0x074: "BLTADAT",
    0x07E: "DSKSYNC", 0x080: "COP1LCH", 0x084: "COP2LCH", 0x088: "COPJMP1",
    0x08A: "COPJMP2", 0x08E: "DIWSTRT", 0x090: "DIWSTOP", 0x092: "DDFSTRT",
    0x094: "DDFSTOP", 0x096: "DMACON", 0x098: "CLXCON", 0x09A: "INTENA",
    0x09C: "INTREQ", 0x09E: "ADKCON", 0x0A0: "AUD0LCH", 0x0A6: "AUD0VOL",
    0x0B0: "AUD1LCH", 0x0C0: "AUD2LCH", 0x0D0: "AUD3LCH",
    0x0E0: "BPL1PTH", 0x0E2: "BPL1PTL", 0x0E4: "BPL2PTH", 0x0E8: "BPL3PTH",
    0x0EC: "BPL4PTH", 0x0F0: "BPL5PTH", 0x100: "BPLCON0", 0x102: "BPLCON1",
    0x104: "BPLCON2", 0x108: "BPL1MOD", 0x10A: "BPL2MOD", 0x110: "BPL1DAT",
    0x120: "SPR0PTH", 0x140: "SPR0POS", 0x180: "COLOR00",
}


def chip_name(off: int) -> str:
    if off in CHIP:
        return CHIP[off]
    if 0x120 <= off < 0x180:
        return f"SPR{(off-0x120)//4}xx"
    if 0x180 <= off < 0x1C0:
        return f"COLOR{(off-0x180)//2:02d}"
    if 0x0A0 <= off < 0x0E0:
        return f"AUD{(off-0xA0)//0x10}xx"
    return f"+{off:#05x}"


def md():
    m = capstone.Cs(capstone.CS_ARCH_M68K, capstone.CS_MODE_M68K_000)
    m.detail = True
    return m


def analyze(path: Path) -> None:
    prog = hunkload.load(path)
    m = md()
    code_segs = [s for s in prog.segments if s.kind == hunkload.HUNK_CODE]
    code_bytes = sum(s.size_bytes for s in code_segs)

    chip_hits: Counter = Counter()
    cia_hits: Counter = Counter()
    lvo_calls: Counter = Counter()
    vector_writes: Counter = Counter()
    insn_count = 0
    decoded_bytes = 0

    # raw 32-bit constant scan over code (catches lea/movea absolute-long bases)
    raw_chip = Counter()
    raw_cia = Counter()
    for s in code_segs:
        d = s.data
        for i in range(0, len(d) - 3):
            v = int.from_bytes(d[i:i + 4], "big")
            if 0xDFF000 <= v <= 0xDFF1FE:
                raw_chip[chip_name(v - 0xDFF000)] += 1
            elif 0xBFD000 <= v <= 0xBFEF01:
                bank = "CIAA" if v >= 0xBFE001 else "CIAB"
                raw_cia[bank] += 1

    # capstone pass for instruction-level signals
    for s in code_segs:
        for ins in m.disasm(bytes(s.data), s.base):
            insn_count += 1
            decoded_bytes += ins.size
            mn = ins.mnemonic
            ops = ins.op_str
            # absolute references to chip/CIA via decoded operands
            for tok in ops.replace(",", " ").split():
                t = tok.strip("()#.lw").lstrip("$")
                try:
                    val = int(t, 16)
                except ValueError:
                    continue
                if 0xDFF000 <= val <= 0xDFF1FE:
                    chip_hits[chip_name(val - 0xDFF000)] += 1
                elif 0xBFD000 <= val <= 0xBFEF01:
                    cia_hits["CIAA" if val >= 0xBFE001 else "CIAB"] += 1
                elif val in (0x68, 0x6C, 0x70, 0x74, 0x78, 0x64, 0x0C, 0x10, 0x14):
                    if mn.startswith("move") and ops.strip().endswith(t) is False:
                        pass
            # library LVO calls: jsr/jmp -(disp)(aN)
            if mn in ("jsr", "jmp") and "-0x" in ops and "(a" in ops:
                disp = ops.split("(")[0]
                lvo_calls[disp] += 1
            # exception-vector installs: move.l X,$xx.w with xx in vector area
            if mn.startswith("move") and (ops.endswith(".w") or True):
                parts = ops.split(",")
                if len(parts) == 2:
                    dst = parts[1].strip()
                    ds = dst.strip("()#.lw").lstrip("$")
                    try:
                        dv = int(ds, 16)
                    except ValueError:
                        dv = -1
                    if dv in (0x64, 0x68, 0x6C, 0x70, 0x74, 0x78, 0x7C):
                        vec = {0x64:"L1",0x68:"L2",0x6C:"L3",0x70:"L4",0x74:"L5",0x78:"L6",0x7C:"L7"}[dv]
                        vector_writes[vec] += 1

    print(f"\n===== {path.name} =====")
    print(f"code: {code_bytes} bytes in {len(code_segs)} hunks; "
          f"{insn_count} insns decoded ({decoded_bytes}/{code_bytes} bytes = "
          f"{100*decoded_bytes/code_bytes:.1f}% coverage)")
    print(f"custom-chip register refs (raw const scan): {sum(raw_chip.values())} hits, "
          f"{len(raw_chip)} distinct regs")
    if raw_chip:
        for name, c in sorted(raw_chip.items(), key=lambda x: -x[1]):
            print(f"    {name:10} x{c}")
    print(f"CIA refs (raw const scan): {dict(raw_cia)}")
    print(f"chip refs via decoded operands: {sum(chip_hits.values())} ({len(chip_hits)} regs)")
    if chip_hits:
        print("   ", dict(sorted(chip_hits.items(), key=lambda x: -x[1])))
    print(f"CIA via decoded operands: {dict(cia_hits)}")
    print(f"interrupt-vector installs (move.l ->$L#.w): {dict(vector_writes)}")
    print(f"library LVO-style jsr/jmp -disp(aN): {sum(lvo_calls.values())} calls, "
          f"{len(lvo_calls)} distinct offsets")
    if lvo_calls:
        top = sorted(lvo_calls.items(), key=lambda x: -x[1])[:12]
        print("   ", {k: v for k, v in top})


if __name__ == "__main__":
    args = sys.argv[1:] or ["portable/moonstone_hdd/crystal",
                            "portable/moonstone_hdd/mog",
                            "portable/moonstone_hdd/program",
                            "portable/moonstone_hdd/nb"]
    for a in args:
        analyze(Path(a))
