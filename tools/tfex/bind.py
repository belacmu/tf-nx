"""bind.py: resolves the game members the translator's output uses to the Switch binary (1.4.1.6), with tools/re
(rtypes.py fields/types, sigs.py overloads by PC signature, tfexrefs.py accessors, classmeta.py records).

  bind.py <requests.json> <binding.json>
requests: [{"key", "kind", "type", "member", "sig"}]; kind is
  type      the runtime type: getter, descriptor, vtable, size, cctor runner, base
  field     instance field offset ("off") or static storage ("static": the slot address)
  method    body ("body"), by name and PC parameter list (overloads)
  vslot     the vtable slot (byte offset) whose entry is that method's body, and the vtable size of "type"
binding.json is updated in place (existing keys kept): the binary doesn't change, so it is committed and builds don't need
main.flat. Needs tools/re/main.flat: linked from the main checkout ($XPLAY_RE_DIR) when missing."""
import json, os, re, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.join(HERE, "..", "re")
flat = os.path.join(RE, "main.flat")
if not os.path.exists(flat):
    src = os.path.join(os.environ.get("XPLAY_RE_DIR", os.path.expanduser("~/Code/Towerfall Switch Mods/switch-online/tools/re")), "main.flat")
    if os.path.exists(src):
        os.symlink(src, flat)
sys.path.insert(0, RE)
ARGS = [os.path.abspath(a) for a in sys.argv[1:]]
os.chdir(RE)                      # metaresolve.py opens main.flat relative to the working directory
import relocs, rtypes, sigs, tfexrefs, vtable  # noqa: E402
import tomllib as _tomllib   # noqa: E402
RE_FIELDS = _tomllib.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "re-fields.toml"), "rb"))
_R = None

PRIM = {"float": "float32", "double": "float64", "int": "int32", "uint": "uint32", "long": "int64", "short": "int16",
        "byte": "uint8", "sbyte": "int8", "char": "char", "bool": "bool", "string": "string", "object": "object"}


def norm(params):
    """PC IL parameters or the translator's list -> one comparable form: no class/valuetype/[asm], C# keywords as IL names."""
    out = []
    for p in [x.strip() for x in re.split(r",(?![^<\[]*[>\]])", params or "") if x.strip()]:
        p = re.sub(r"\b(class|valuetype)\s+", "", p)
        p = re.sub(r"\[[A-Za-z][\w.]*\]", "", p).replace("/", "+").split(" ")[0]
        out.append(PRIM.get(p, p))
    return ",".join(out)


def generic_overload(inst, mem, sig, nrec):
    """An instance of a game generic type (Monocle.Sprite`1[System.Int32]) has no PC IL of its own: its overloads are the open
    definition's (Monocle.Sprite`1<T>) in IL order, which is BRUTE's record order (sigs.py). The index of the one whose parameters
    match sig ("T,bool" against "!T, bool"), when the overload counts agree; else None."""
    if "[" not in inst:
        return None
    gdef = inst.split("[", 1)[0]
    sigs.pc_methods(gdef)                      # loads the IL index
    keys = [k for k in (sigs._classes or {}) if k.split("<", 1)[0] == gdef and "+" not in k]
    if len(keys) != 1:
        return None
    pcs = [p for p in sigs._classes[keys[0]] if p[0] == mem and not p[2]] or [p for p in sigs._classes[keys[0]] if p[0] == mem]
    if len(pcs) != nrec:
        return None
    want = norm(sig).replace("!", "")
    hit = [i for i, p in enumerate(pcs) if norm(p[1]).replace("!", "") == want]
    return hit[0] if len(hit) == 1 else None


def vtable_bytes(t):
    """A class's vtable length in bytes: up to its interface table, which the descriptor's third word points at (Entity +0x90 as
    tfex/Game.hpp, MenuItem +0xb8, MainModeButton +0x118), or up to the descriptor when that table lies elsewhere (PlayerInput 0x178:
    its last three slots are abstract, null). Abstract (null) slots count. Not the first null slot: that dropped MainModeButton's
    abstract slots and everything after (0xa8, so a generated subclass copied too little of its base's vtable). Never smaller than the
    base class's: a string (the error) if it would be."""
    tab = rtypes.table()
    ty = tab[t]
    if not ty.vtable or not ty.desc:
        return None
    R = relocs.relative()
    it = R.get(ty.desc + 0x10)
    it = it & ((1 << 48) - 1) if it is not None else None
    if it is not None and ty.vtable <= it <= ty.desc:
        n = it - ty.vtable
    elif ty.vtable < ty.desc <= ty.vtable + 0x800:
        n = ty.desc - ty.vtable
    else:
        return None   # the descriptor isn't right after the vtable (value types, enumerators: nothing derives from them): not derived
    if n % 8:
        return f"vtable size {n:#x}: not a vtable"
    base = ty.parent if ty.parent in tab else None
    if base:
        b = vtable_bytes(base)
        b = b if b is not None else 0
        if isinstance(b, int) and n < b:
            return f"vtable {n:#x} smaller than its base {base}'s {b:#x}"
    return n


def repair_vtbytes(have):
    """type entries (t_/d_/vb_/z_/c_: the same record) whose vtbytes an older bind.py counted to the first null slot: recomputed in place"""
    bydesc = {ty.desc: name for name, ty in rtypes.table().items() if ty.desc}
    n = 0
    for k, v in have.items():
        if "vtbytes" not in v or "desc" not in v or v["desc"] not in bydesc:
            continue
        vb = vtable_bytes(bydesc[v["desc"]])
        if isinstance(vb, int) and vb != v["vtbytes"]:
            have[k] = dict(v, vtbytes=vb)
            n += 1
    return n


_DESC = None
_SLOTS = {}


def _descendants(owner):
    """the types below owner (with a vtable), from one pass over every type's chain (cached)"""
    global _DESC
    if _DESC is None:
        _DESC = {}
        tab = rtypes.table()
        for name in tab:
            if not tab[name].vtable:
                continue
            try:
                chain = rtypes.chain(name)
            except Exception:
                continue
            for c in chain[1:]:
                _DESC.setdefault(c.name, []).append(name)
    return _DESC.get(owner, [])


def _slots(name):
    if name not in _SLOTS:
        try:
            _SLOTS[name] = vtable.slots(name)
        except Exception:
            _SLOTS[name] = None
    return _SLOTS[name]


def abstract_slot(owner, mem, params=None):
    """The vtable slot of an abstract method of `owner`, proven by the binary: every subclass with a body for `mem` (classmeta's
    record names, vtable.py) has it at the same offset, and `owner`'s own entry there is null. A slot the subclasses disagree on, or
    none, is an error (a guessed slot calls the wrong method). Checked against the disassembly for MainModeButton (docs/ws-tfex-bind.md)."""
    tab = rtypes.table()
    R = relocs.relative()
    ovt = tab[owner].vtable
    if not ovt:
        return {"error": "abstract: owner has no vtable"}
    want = norm(params) if params is not None else None
    seen = {}
    for name in _descendants(owner):
        rows = _slots(name)
        if rows is None:
            continue
        offs = set()
        for off, tgt, label in rows:
            if not tgt or "." not in label:
                continue
            m, paren, rest = label.split(".", 1)[1].partition("(")   # a body with no PC signature (a Switch-only class) has no "(...)"
            if m != mem or (want is not None and paren and norm(rest.rstrip(")")) != want):
                continue
            if R.get(ovt + off) is None and int.from_bytes(rtypes._img()[ovt + off:ovt + off + 8], "little") == 0:
                offs.add(off)
        if len(offs) > 1:
            return {"error": f"abstract: {name} has {mem} at several slots {sorted(offs)}"}
        if offs:
            seen[name] = offs.pop()
    if not seen:
        return {"error": "abstract: no subclass overrides it"}
    if len(set(seen.values())) != 1:
        return {"error": "abstract: subclasses disagree: " + ", ".join(f"{k} {v:#x}" for k, v in sorted(seen.items()))}
    return {"slot": next(iter(seen.values())), "abstract": True, "owner": owner, "proof": f"{len(seen)} subclasses"}


def repair_abstract(have):
    """binding.json entries an older bind.py gave by the null-slot guess (abstract, no "proof"): re-resolved with abstract_slot, in
    place. The member comes from the key (v_<Type>_<member>[_<hash>]) matched against the owner's abstract methods (sigs.py)."""
    n = 0
    for k, v in list(have.items()):
        if not (k.startswith("v_") and v.get("abstract") and "proof" not in v):
            continue
        owner = v["owner"]
        sigs.pc_methods(owner)
        hit = [(m, p) for m, p in sigs.ABSTRACT.get(owner, []) if re.search("_" + re.escape(re.sub(r"\W", "_", m)) + r"(_[0-9a-f]{4})?$", k)]
        if len(hit) != 1:
            have[k] = {"error": "abstract: member not found again for the repair (re-request it)"}
        else:
            have[k] = abstract_slot(owner, hit[0][0], hit[0][1])
        n += 1
    return n


def u64(off):
    return struct.unpack_from("<Q", rtypes._img(), off)[0]


# Arrays of a reference type the game never instantiates, which TF.EX / FortRise code only fills and reads back (no type checks, no game
# code sees them): System.Object[] stands in (FortRise's SFXVariedEntry: new Stream[Count] for patch_SFXVaried's constructor).
OBJECT_ARRAY_STANDINS = {"System.IO.Stream[]"}


def resolve(r):
    global _R
    tab = rtypes.table()
    t, kind, mem = r["type"], r["kind"], r.get("member")
    ty = tab.get(t)
    if ty is None and t in OBJECT_ARRAY_STANDINS and kind == "type":
        t = "System.Object[]"   # an array of references only stored and read back: object[] holds them (see OBJECT_ARRAY_STANDINS)
        ty = tab.get(t)
    if ty is None:
        return {"error": "no type " + t}
    if kind == "type":
        if _R is None:
            _R = relocs.relative()
        vb = vtable_bytes(t)
        if isinstance(vb, str):
            return {"error": vb}
        if vb is None:   # not a class vtable (value types...): the relocated pointers in a row, as before
            vb = 0
            while ty.vtable and _R.get(ty.vtable + vb) is not None and vb < 0x800:
                vb += 8
        # a type no field or base type names has no getter in the table: rtypes.getter_of finds it (None stays None: the other
        # values of the type serve their own requests, and a t_ use of it reports "no value")
        getter = ty.getter or rtypes.getter_of(t)
        return {"getter": getter, "desc": ty.desc, "vtable": ty.vtable, "size": ty.size, "flags": ty.flags,
                "cctor": ty.cctor if isinstance(ty.cctor, int) else 0, "base": ty.parent, "vtbytes": vb}
    if kind == "enum":                     # a game enum's literals on the Switch, by name (values differ from the PC's: MenuState)
        vals = {}
        for owner, f, off, fty in rtypes.fields(t, inherited=False, statics=True):
            if f != "value__" and off is not None:   # past the file = .bss = 0 (MenuState.None)
                vals[f] = struct.unpack_from("<i", rtypes._img(), off)[0] if off + 4 <= len(rtypes._img()) else 0
        return {"values": vals} if vals else {"error": "no enum literals"}
    if kind == "field":
        # FortRise's patched TowerFall (what TF.EX compiles against) makes some private fields public under a capitalised name
        # (VersusLevelSystem.LastLevel is the game's private lastLevel): the game's own spelling is the fallback when the name isn't found
        names = [mem] + ([mem[0].lower() + mem[1:]] if mem[:1].isupper() else [])
        for want in names:
          for owner, f, off, fty in rtypes.fields(t, statics=True):
            if f == want and off is not None:
                attrs = next((a for n, o, a, _ in (tab[owner].fields or []) if n == f), 0) or 0
                static = bool(attrs & rtypes.STATIC)
                c = tab[owner].cctor
                via = "field record" if want == mem else "field record (the game's private " + want + ")"
                return {"static": off, "cctor": c if isinstance(c, int) else 0, "via": via} if static else {"off": off, "via": via}
        for c in rtypes.chain(t):
            o = tfexrefs.accessor_offset(c.name, mem)
            # (2026-10-26) classmeta.records runs on into the next classes: a get_<mem> there (an XACT class's get_State) gave
            # SoundEffectInstance.State a "field" at +0x40 of a 0x38-byte object. An offset past the instance isn't this type's field.
            if o is not None and tab[t].size and o >= tab[t].size:
                continue
            if o is not None:
                return {"off": o, "via": "get_" + mem + " accessor"}
        # a field the Switch's metadata doesn't name, its offset read from the code (tools/tfex/re-fields.toml, with the evidence)
        if (rf := RE_FIELDS.get(f"{t}.{mem}")) is not None:
            return {"off": int(rf["off"]), "via": "re-fields.toml: " + rf.get("evidence", "")[:80]}
        return {"error": "field not resolved"}
    if kind in ("method", "vslot"):
        want = norm(r.get("sig", ""))
        for c in rtypes.chain(t):
            rows = [row for row in sigs.paired(c.name) if row[0] == mem]
            if not rows and c.name != t:
                # (2026-10-18) classmeta.records of a base type is no evidence: for corlib types its lists are a sliding window over
                # neighbouring classes (System.ValueType's has System.Version's Parse/TryParse), and a static or a constructor is never
                # inherited: Boolean.TryParse was bound to Version.TryParse through ValueType (WiderSet's "MISSING VALUE"). By name,
                # only the requested type's own records count.
                continue
            if not rows:
                # a type sigs.py has no PC IL for (generic instances, corlib, FNA): bodies by name, in metadata order. The translator
                # gives the overload index and count from the declaring assembly (Mono's mscorlib, FNA); classmeta.records of a generic
                # instance lists several instantiations one after another: the first block is the named one.
                allr = tfexrefs.records(c.name)
                if "[" in c.name and allr:
                    # a generic instance: classmeta lists the named instantiation first, then others; its block ends where the
                    # first record's name comes back after other names
                    end = next((i for i in range(1, len(allr)) if allr[i][0] == allr[0][0] and allr[i - 1][0] != allr[0][0]), len(allr))
                    allr = allr[:end]
                recs = [b for n, b, fl in allr if n == mem]
                k, cnt = r.get("overload", -1), r.get("count", 0)
                if k < 0 and cnt > 0 and r.get("overload_pub", -1) < 0:
                    # (2026-10-28) Mono's metadata has methods of this name but none with this signature: a newer .NET overload the
                    # Switch's runtime lacks (Array.Clear(Array): the one body named Clear is Clear(Array, int, int), and calling it with
                    # one argument threw). Not bound: the call needs a rule.
                    return {"error": f"not in the Switch's runtime: {c.name}.{mem}({r.get('sig', '')}) (Mono has {cnt} other overload(s))"}
                if len(recs) == 1 and cnt <= 1:
                    rows = [(mem, 0, recs[0], r.get("sig", ""), None, False)]
                elif recs and k >= 0 and cnt > 0 and len(recs) % cnt == 0:
                    rows = [(mem, k, recs[k], r.get("sig", ""), None, False)]
                elif recs and cnt == 1:
                    rows = [(mem, 0, recs[0], r.get("sig", ""), None, False)]
                elif recs and r.get("overload_pub", -1) >= 0:
                    # the Switch's assembly has overloads the reference metadata lacks (FNA's internal MeasureString(ref StringProxy)):
                    # count the public ones only (record flags & 7 == 6)
                    pub = [b for n, b, fl in allr if n == mem and isinstance(fl, int) and fl & 7 == 6]
                    kp, cp = r["overload_pub"], r.get("count_pub", 0)
                    if cp and len(pub) % cp == 0:
                        rows = [(mem, kp, pub[kp], r.get("sig", ""), None, False)]
                elif len(recs) > 1 and (pick := generic_overload(c.name, mem, r.get("sig", ""), len(recs))) is not None:
                    rows = [(mem, pick, recs[pick], r.get("sig", ""), None, False)]
                elif len(recs) > 1:
                    return {"error": f"{len(recs)} bodies named {c.name}.{mem}, overload {k} of {cnt}: can't pick"}
            if not rows:
                continue
            pick = [row for row in rows if norm(row[3]) == want] if len(rows) > 1 and r.get("overload", -1) < 0 else rows
            if not pick and len(rows) > 1:
                # sigs.py keeps only the last space-separated token of a parameter: a generic parameter type
                # ("valuetype [mscorlib]System.Nullable`1<valuetype [FNA]...Rectangle>") comes out as "[FNA]...Rectangle>". Compare the
                # last identifier of each parameter type on both sides (a unique match only).
                def loose(sig):
                    return [re.findall(r"[A-Za-z_][\w`]*", p)[-1] if re.findall(r"[A-Za-z_][\w`]*", p) else p for p in norm(sig).split(",") if p]
                lw = loose(r.get("sig", ""))
                cand = [row for row in rows if loose(row[3]) == lw]
                if len(cand) == 1:
                    pick = cand
            if not pick:
                return {"error": f"no overload ({want}) among " + " | ".join(norm(x[3]) for x in rows)}
            n, k, body, params, static, mis = pick[0]
            if not body and kind == "vslot":
                # an abstract method: the slot where the concrete subclasses put their override (abstract_slot). Not vtable.py's
                # null-slot guess (the class's abstract methods in IL order): for MainModeButton it named the wrong slots.
                return abstract_slot(c.name, mem, params)
            if not body:
                return {"error": "no body on the Switch (inlined or removed)"}
            out = {"body": body, "static": bool(static), "owner": c.name, "overload": k}
            if kind == "vslot":
                if _R is None:
                    _R = relocs.relative()
                vt = ty.vtable
                if not vt:
                    return {"error": "no vtable"}
                slots = []
                # the type's own table only (vtbytes): past it lie a neighbour's entries (XmlNodeList's 0x48 bytes: get_Count's body
                # sat at +0x90 in the next table); System.Xml's XmlElement.SetAttribute is at 0x238
                vtb = vtable_bytes(c.name) if c.name == t else None
                n_slots = (vtb // 8) if isinstance(vtb, int) and vtb > 0 else 64
                for i in range(n_slots):
                    e = _R.get(vt + 8 * i)       # vtable entries are RELATIVE relocations (may carry flags above bit 48)
                    if e is not None and (e & ((1 << 48) - 1)) == body:
                        slots.append(8 * i)
                if not slots:
                    return {"error": "body not in the vtable (not virtual?)"}
                out["slot"] = slots[0]
            return out
        return {"error": "method not found"}
    return {"error": "unknown kind " + kind}


def main():
    reqs = json.load(open(ARGS[0]))
    path = ARGS[1]
    have = json.load(open(path)) if os.path.exists(path) else {}
    n = 0
    # entries of kind type written before rtypes.getter_of existed: getter null (the header then has no value). Repaired in place.
    bydesc = None
    for k, v in have.items():
        if "error" not in v and "desc" in v and "getter" in v and not v["getter"]:
            if bydesc is None:
                bydesc = {ty.desc: name for name, ty in rtypes.table().items() if ty.desc}
            name = bydesc.get(v["desc"])
            g = rtypes.getter_of(name) if name else None
            if g:
                have[k] = dict(v, getter=g)
                n += 1
    n += repair_abstract(have)
    n += repair_vtbytes(have)
    for r in reqs:
        if r["key"] in have and "error" not in have[r["key"]]:
            continue
        try:
            have[r["key"]] = resolve(r)
        except Exception as e:          # a tools/re failure on one type (classmeta's operand parsing...) must not stop the rest
            have[r["key"]] = {"error": f"tools/re failed: {type(e).__name__}: {e}"}
        n += 1
    json.dump(dict(sorted(have.items())), open(path, "w"), indent=1)
    bad = [k for k, v in have.items() if "error" in v]
    print(f"bind.py: {n} resolved now, {len(have)} known, {len(bad)} unresolved", file=sys.stderr)


if __name__ == "__main__":
    main()
