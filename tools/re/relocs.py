"""relocs.py: the R_AARCH64_RELATIVE relocations of main.flat ({slot offset: target offset}), from MOD0 -> .dynamic -> DT_RELA."""
import struct, os, pickle
_here = os.path.dirname(os.path.abspath(__file__))
def relative():
    cache = os.path.join(_here, "relocs.idx")
    flat = os.path.join(_here, "main.flat")
    if os.path.exists(cache) and os.path.getmtime(cache) > os.path.getmtime(flat):
        with open(cache, "rb") as f: return pickle.load(f)
    img = open(flat, "rb").read()
    mod0 = struct.unpack_from("<I", img, 4)[0]
    dyn = mod0 + struct.unpack_from("<i", img, mod0 + 4)[0]
    d, o = {}, dyn
    while True:
        tag, val = struct.unpack_from("<qQ", img, o); o += 16
        if tag == 0: break
        d.setdefault(tag, val)
    out = {}
    for i in range(d[8] // 24):
        off, info, add = struct.unpack_from("<QQq", img, d[7] + 24 * i)
        if info & 0xffffffff == 1027: out[off] = add
    with open(cache, "wb") as f: pickle.dump(out, f)
    return out
