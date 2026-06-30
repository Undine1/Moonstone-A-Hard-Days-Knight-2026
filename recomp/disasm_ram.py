"""Disassemble a region of a RAM dump (2MB flat image, addr == file offset)."""
import sys, capstone

ram = open(r"build/ram3000.bin","rb").read()
md = capstone.Cs(capstone.CS_ARCH_M68K, capstone.CS_MODE_M68K_000)

def dis(addr, n):
    code = ram[addr:addr+n*8]
    cnt = 0
    for ins in md.disasm(code, addr):
        print(f"{ins.address:06x}: {ins.mnemonic:8} {ins.op_str}")
        cnt += 1
        if cnt >= n: break

if __name__ == "__main__":
    addr = int(sys.argv[1], 0)
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 40
    dis(addr, n)
