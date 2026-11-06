"""ctx.py <body hex> <const>...: Switch code around each use of the given float constants in a body (names from callmap)."""
import sys, struct, parity as p, callmap
names = callmap.body_names()
def ctx(body, want, before=16, after=8):
    a, b = p.extent(body); ins = list(p.md.disasm(p.img[a:b], a)); page = {}; wv = {}
    for k, i in enumerate(ins):
        ops = [x.strip() for x in i.op_str.split(",")]; hit = False
        if i.mnemonic == "adrp": page[ops[0]] = int(ops[1].lstrip("#"), 16)
        if i.mnemonic == "ldr" and ops[0][0] in "sd" and len(ops) == 3 and ops[1].lstrip("[") in page and ops[2].startswith("#"):
            addr = page[ops[1].lstrip("[")] + int(ops[2].rstrip("]").lstrip("#"), 0)
            if p.RODATA[0] <= addr < p.RODATA[1]:
                v = struct.unpack_from("<f", p.img, addr)[0]; hit = any(abs(abs(v) - w) < 1e-3 for w in want)
        if i.mnemonic == "fmov" and ops[-1].startswith("#"):
            try: hit = any(abs(abs(float(ops[-1].lstrip("#"))) - w) < 1e-3 for w in want)
            except ValueError: pass
        if i.mnemonic in ("mov", "movz") and len(ops) == 2 and ops[1].startswith("#"): wv[ops[0]] = int(ops[1].lstrip("#"), 0)
        if i.mnemonic == "movk" and ops[0] in wv:
            wv[ops[0]] |= int(ops[1].lstrip("#"), 0) << int(ops[2].split("#")[1], 0)
            v = struct.unpack("<f", struct.pack("<I", wv[ops[0]] & 0xffffffff))[0]; hit = any(abs(abs(v) - w) < 1e-3 for w in want)
        if hit:
            print("--- at", hex(i.address))
            for j in ins[max(0, k - before):k + after]:
                t = int(j.op_str[1:], 16) if j.mnemonic == "bl" and j.op_str.startswith("#") else None
                print("  ", hex(j.address), j.mnemonic, j.op_str, names.get(t, "") if t else "")
if __name__ == "__main__":
    ctx(int(sys.argv[1], 16), [float(x) for x in sys.argv[2:]])
