#!/usr/bin/env python3
"""privfield.py: the Switch offset of a game field BRUTE's reflection doesn't list (private fields; rtypes.py has public/internal
ones only), from the methods that use it.

For each method of the class whose PC IL (towerfall-pc.il) loads or stores Type::field, the Switch body (sigs.paired: overloads
by IL order) is scanned for memory accesses through `this` (x0 at entry, followed through `mov` copies until overwritten; a call
clobbers x0-x18). An offset that every using method touches, that no known field (reflection records, base classes included)
has and that lies inside the instance, is the field's. The access width is checked against the field's PC type
(bool/byte 1, int/float 4, reference/long 8; a value type: its first member's width at least).

A lead, not a proof: the scan is linear (no branches, `this` spilled to the stack is lost), so confirm the answer in the
disassembly (a store of the constructor argument, the getter, a line of the PC source) before pinning it. Checked against the
hand-verified offsets (MainMenu.state/switchTo, RollcallElement.playerIndex/input/portrait/finishedJoined, ScreenTitle.targetTexture,
Layer.toAdd, Engine.scene/nextScene: all agree; RollcallElement.archerType, MainModeButton.text: refused as ambiguous); it said
PauseMenu.optionIndex = 0xc4 where Update shows 0x80 (0xc4 is controllerDisconnected).

  privfield.py TowerFall.RollcallElement state [-v]   -> offsets with how many using methods touch them
  import privfield; privfield.find("TowerFall.PauseMenu", "level") -> (offset or None, evidence text)"""
import os, re, sys, collections
import rtypes, sigs
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN
_md = Cs(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN)
_md.skipdata = True

_uses = None
def _scan_il():
    """{(type, field): {(method name, params): ld/st count}} for every TowerFall/Monocle field use in the PC IL, and the field types."""
    global _uses, _ftypes
    _uses, _ftypes = collections.defaultdict(collections.Counter), {}
    stack, meth, buf, inm = [], None, "", False
    for line in open(sigs.PC_IL, encoding="utf-8", errors="replace"):
        s = line.strip()
        if s.startswith(".class "):
            n = s.split()[-1].strip("'")
            stack.append(stack[-1] + "+" + n if " nested " in s and stack else n); continue
        if s.startswith("} // end of class"):
            if stack: stack.pop()
            continue
        if s.startswith(".field ") and stack:
            parts = s.split()
            mods = {"public", "private", "family", "assembly", "famorassem", "famandassem", "static", "initonly", "notserialized", "literal", "specialname", "rtspecialname"}
            _ftypes[(stack[-1], parts[-1].strip("'"))] = " ".join(x for x in parts[1:-1] if x not in mods)
            continue
        if s.startswith(".method"): inm, buf = True, s
        elif inm: buf += " " + s
        if inm and ")" in buf and ("cil managed" in buf or "runtime managed" in buf):
            head, rest = buf.split("(", 1)
            params = re.sub(r"\s+", " ", rest.rsplit(")", 1)[0])
            params = ", ".join(p.strip().split(" ")[-2] if len(p.strip().split(" ")) >= 2 else p.strip() for p in params.split(",") if p.strip())
            meth = (stack[-1] if stack else None, head.split()[-1], params); inm = False
            continue
        m = re.match(r"IL_[0-9a-f]+: (ldfld|stfld|ldflda)\s+.*?\s([\w.+/`]+)::'?([\w<>`]+)'?$", s)
        if m and meth:
            _uses[(m.group(2).replace("/", "+"), m.group(3))][meth] += 1

def _body_of(t, name, params):
    for n, k, body, p, static, mis in sigs.paired(t):
        if n == name and p == params and not static: return body, mis
    return None, False

WIDTH = {"ldrb": 1, "strb": 1, "ldrsb": 1, "ldrh": 2, "strh": 2, "ldrsh": 2}
def _this_accesses(body, limit=0x3000):
    """{offset: set(widths)} of loads/stores through registers holding `this`, scanning the function linearly."""
    img = rtypes._img()
    this = {"x0"}
    out = collections.defaultdict(set)
    a = body
    for ins in _md.disasm(img[body:body + limit], body):
        if ins.bytes == b"\0\0\0\0" and ins.address > body: break
        ops = [o.strip() for o in ins.op_str.split(",")]
        mn = ins.mnemonic
        mm = re.search(r"\[(x\d+|sp)(?:, #(-?0x[0-9a-f]+|-?\d+))?\]", ins.op_str)
        if mm and mm.group(1) in this and (mn.startswith("ld") or mn.startswith("st")) and "!" not in ins.op_str:
            off = int(mm.group(2), 0) if mm.group(2) else 0
            reg = ops[0]
            w = WIDTH.get(mn) or (8 if reg.startswith(("x", "d")) else 16 if reg.startswith("q") else 4)
            if mn in ("ldp", "stp"):
                out[off].add(w); out[off + w].add(w)
            else:
                out[off].add(w)
        if mn == "mov" and len(ops) == 2 and ops[1] in this and ops[0].startswith("x"):
            this.add(ops[0]); continue
        if mn in ("bl", "blr"):
            this -= {f"x{i}" for i in range(19)}; continue
        if mn in ("ret",) and ins.address > body + 0x40: break
        if ops and ops[0] in this and not (mn.startswith("st") or mn.startswith("cmp") or mn.startswith("tst") or mn.startswith("cb") or mn.startswith("tb")):
            this.discard(ops[0])
        if mn in ("ldp",) and len(ops) > 1 and ops[1] in this:
            this.discard(ops[1])
    return out

def _want_width(fty):
    if (fty or "").startswith("class ") or (fty or "").endswith("[]") or fty == "string": return {8}
    if (fty or "").startswith("valuetype") and "/" in fty: return {1, 4}   # a nested enum: int32 mostly, byte enums exist
    fty = re.sub(r"^(class|valuetype)\s+", "", fty or "")
    if fty in ("bool", "uint8", "int8"): return {1}
    if fty in ("int16", "uint16", "char"): return {2}
    if fty in ("int32", "uint32", "float32"): return {4}
    if fty in ("int64", "uint64", "float64"): return {8}
    if "Vector2" in fty or "Point" in fty or "Rectangle" in fty: return {4, 8}
    if "Color" in fty: return {1, 4}
    return None   # a class (8) or an enum (4: int32 enums mostly) or another struct: not checked

def find(t, field, verbose=False):
    if _uses is None: _scan_il()
    ty = rtypes.table().get(t)
    if ty is None: return None, f"no type {t}"
    size = ty.size if isinstance(ty.size, int) else 1 << 16
    known = {off for owner, f, off, fty in rtypes.fields(t) if off is not None}
    fty = _ftypes.get((t, field), "?")
    want = _want_width(fty)
    users = [(n, p) for (c, n, p) in _uses.get((t, field), {}) if c == t]
    votes, ev = collections.Counter(), []
    used = 0
    for n, p in users:
        body, mis = _body_of(t, n, p)
        if not body: ev.append(f"{n}({p}): no Switch body"); continue
        acc = _this_accesses(body)
        cand = {o for o, ws in acc.items() if 0x10 <= o < size and o not in known and (want is None or ws & want)}
        used += 1
        for o in cand: votes[o] += 1
        ev.append(f"{n}({p}) +{body:#x}{' (overload count differs)' if mis else ''}: " + " ".join(f"{o:#x}" for o in sorted(cand)))
    if not used: return None, f"{t}.{field} ({fty}): no usable method ({'; '.join(ev) or 'no PC IL use'})"
    # the this-tracking is linear (no branches; `this` spilled to the stack is lost): a method may miss the access. The answer is
    # the offset most methods touch when it leads clearly (at least 60% of the methods, and no other offset as often)
    top = votes.most_common()
    best = [o for o, v in top if v == top[0][1]] if top else []
    ok = len(best) == 1 and top[0][1] >= max(1, 0.6 * used) and (used == 1 or top[0][1] >= 2)
    text = (f"{t}.{field} ({fty}, size {size:#x}): {used} methods; most touched: " +
            (" ".join(f"{o:#x}x{v}" for o, v in top[:5]) or "none"))
    best = best if ok else []
    if verbose: text += "\n    " + "\n    ".join(ev)
    return (best[0] if best else None), text

if __name__ == "__main__":
    a = [x for x in sys.argv[1:] if not x.startswith("-")]
    off, text = find(a[0], a[1], verbose="-v" in sys.argv)
    print(text)
    print("=> " + (f"{off:#x}" if off is not None else "ambiguous / not found"))
