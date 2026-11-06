import struct, re
import metaresolve as m
md=m.md; img=m.img
def is_udf(a): return struct.unpack_from("<I",img,a)[0]==0
def fn_starts(lo,hi):
    """function starts: the first non-udf word after one or more udf words (landing pads end in udf)."""
    out=[]; a=lo; prev_udf=True
    while a<hi:
        u=is_udf(a)
        if prev_udf and not u: out.append(a)
        prev_udf=u; a+=4
    return out
_cache={}
def span(start):
    if start in _cache: return _cache[start]
    ins=[]; a=start
    while a<start+0x8000:
        w=struct.unpack_from("<I",img,a)[0]
        if w==0 and ins: break
        ins.append(next(md.disasm(img[a:a+4],a))); a+=4
    _cache[start]=ins
    return ins
def feats(ins):
    imms=set(); calls=[]
    for i in ins:
        if i.mnemonic=="bl":
            calls.append(int(i.op_str.lstrip('#'),16))
        for mm in re.finditer(r"#(-?0x[0-9a-f]+|-?\d+)",i.op_str):
            try: imms.add(int(mm.group(1),0))
            except: pass
    return imms,calls
def locate(guess, want_imms, want_calls=(), around=3):
    """starts near `guess` (the resolver's answer) whose span has all wanted immediates / calls"""
    starts=fn_starts(guess-0x6000, guess+0x6000)
    k=min(range(len(starts)),key=lambda j:abs(starts[j]-guess))
    res=[]
    for j in range(max(0,k-around),min(len(starts),k+around+1)):
        s=starts[j]; ins=span(s); im,cl=feats(ins)
        ok=all(w in im for w in want_imms) and all(c in cl for c in want_calls)
        res.append((s,len(ins),ok,j-k))
    return res
