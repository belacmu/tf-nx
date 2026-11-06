#!/usr/bin/env python3
"""Decompress a Switch NSO0 into a vaddr-accurate flat image.
Offsets in the produced image are relative to the module base (== what
exlaunch InstallAtOffset expects). Also dumps segment map + strings index.
"""
import struct, sys, lz4.block

def u32(b, o): return struct.unpack_from("<I", b, o)[0]

def load_nso(path):
    raw = open(path, "rb").read()
    assert raw[:4] == b"NSO0", "not an NSO0"
    flags = u32(raw, 0x0C)
    segs = {}
    # (name, header_off, compressed_size_off)
    for name, hoff, coff in (("text",0x10,0x60), ("ro",0x20,0x64), ("data",0x30,0x68)):
        file_off = u32(raw, hoff+0)
        mem_off  = u32(raw, hoff+4)
        dec_size = u32(raw, hoff+8)
        comp_sz  = u32(raw, coff)
        segs[name] = dict(file_off=file_off, mem_off=mem_off, dec_size=dec_size, comp_sz=comp_sz)
    # decide compression per segment (flags bit0/1/2)
    comp = {"text": flags & 1, "ro": flags & 2, "data": flags & 4}
    # build flat image sized to end of data segment
    end = max(s["mem_off"] + s["dec_size"] for s in segs.values())
    img = bytearray(end)
    for name, s in segs.items():
        blob = raw[s["file_off"]: s["file_off"] + s["comp_sz"]]
        if comp[name]:
            data = lz4.block.decompress(blob, uncompressed_size=s["dec_size"])
        else:
            data = blob[:s["dec_size"]]
        assert len(data) == s["dec_size"], f"{name} size mismatch {len(data)} vs {s['dec_size']}"
        img[s["mem_off"]: s["mem_off"] + s["dec_size"]] = data
    return bytes(img), segs

if __name__ == "__main__":
    path = sys.argv[1]
    out  = sys.argv[2] if len(sys.argv) > 2 else path + ".flat"
    img, segs = load_nso(path)
    open(out, "wb").write(img)
    print(f"wrote {out}  size=0x{len(img):x}")
    for n, s in segs.items():
        print(f"  {n:4} mem=0x{s['mem_off']:08x}..0x{s['mem_off']+s['dec_size']:08x} "
              f"(dec 0x{s['dec_size']:x}, comp 0x{s['comp_sz']:x})")
