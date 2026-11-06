"""widecheck.py <WiderSet decompiled source dir>: every IL constant WiderSet's transpilers rewrite in wide (320 -> 420, 32 -> 42, ...),
looked up in the same method of the Switch's game. A method whose Switch body still holds the narrow constant is a candidate gap in the
Switch's wide mode (docs/ws-wide.md 3.2: the Switch scales most of these through its own width, not all: LevelTiles.ctor's SolidTiles read
kept 32, the Dreadwood desync of 2026-10-26). Each candidate still needs a reading: a constant can be in an unrelated branch.

Source: ilspycmd -p -o <dir> WiderSet.dll (the GameBanana zip's WiderSet.dll); the transpilers are <Type>_<Method>_Transpiler with
ILMatch.LdcI4(n) / LdcR4(nf) matchers. Switch side: callmap.py's body names, main.flat, capstone.

Output, one line per (method, constant): FOUND <offset> <insn> | absent | no Switch body."""
import os, re, sys, glob, struct, bisect
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN
import callmap

_here = os.path.dirname(os.path.abspath(__file__))


def widerset_rewrites(src):
    """[(file, Type, [methods], [(kind, value)])] from the transpilers' names and ILMatch.LdcI4 / LdcR4 matchers."""
    out = []
    for f in sorted(glob.glob(os.path.join(src, "Teuria.WiderSet", "*Hooks.cs"))):
        t = open(f, encoding="utf-8", errors="replace").read()
        for m in re.finditer(r"IEnumerable<CodeInstruction> (\w+)_Transpiler\(IEnumerable<CodeInstruction> instructions, ILGenerator generator\)\s*\{(.*?)\n\t\}", t, re.S):
            name, body = m.group(1), re.sub(r"//IL_.*", "", m.group(2))
            consts = [(k, v.rstrip("f")) for k, v in re.findall(r"ILMatch\.(LdcI4|LdcR4)\(([-\d.]+f?)\)", body)]
            if not consts:
                continue
            typ, _, rest = name.partition("_")
            methods = [p for p in rest.split("_") if p and p not in ("Vector2", "int", "bool", "LoopPlatform", "MoveDirs", "Teams", "FFA")]
            methods = [".ctor" if p == "ctor" else "get_" + p[4:] if p.startswith("get_") else p for p in methods] or [".ctor"]
            if "Added" in methods and "HandleGraphicsDispose" in methods:
                methods = ["Added", "HandleGraphicsDispose"]
            elif rest.startswith("SpawnPlayersFFA"):
                methods = ["SpawnPlayersFFA", "SpawnPlayersTeams"]
            elif rest.startswith("ctor"):
                methods = [".ctor"]
            else:
                methods = methods[:1]
            out.append((os.path.basename(f), typ, methods, consts))
    return out


def main(src):
    flat = open(os.path.join(_here, "main.flat"), "rb").read()
    names = callmap.body_names()
    starts = sorted(names)
    by_name = {}
    for off, n in names.items():
        by_name.setdefault(n, []).append(off)
    md = Cs(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN)
    for f, typ, methods, consts in widerset_rewrites(src):
        for meth in methods:
            bodies = by_name.get(f"TowerFall.{typ}::{meth}") or by_name.get(f"Monocle.{typ}::{meth}") or []
            if not bodies:
                print(f"{typ}.{meth}: no Switch body ({f})")
                continue
            for body in bodies:
                i = bisect.bisect_right(starts, body)
                end = min(starts[i] if i < len(starts) else body + 0x4000, body + 0x4000)
                ints, floats = [], []
                page = {}
                for ins in md.disasm(flat[body:end], body):
                    m = ins.mnemonic
                    if m == "adrp":
                        page[ins.op_str.split(",")[0]] = int(ins.op_str.split("#")[1], 16)
                    elif m == "ldr" and ins.op_str.startswith("s"):
                        mm = re.match(r"s\d+, \[(x\d+), #(0x[0-9a-f]+|\d+)\]", ins.op_str)
                        if mm and mm.group(1) in page:
                            a = page[mm.group(1)] + int(mm.group(2), 0)
                            if a + 4 <= len(flat):
                                floats.append((ins.address, struct.unpack_from("<f", flat, a)[0], f"{m} {ins.op_str}"))
                    elif m == "fmov" and "#" in ins.op_str:
                        try:
                            floats.append((ins.address, float(ins.op_str.split("#")[1]), f"{m} {ins.op_str}"))
                        except ValueError:
                            pass
                    elif m in ("mov", "movz", "orr", "cmp", "add", "sub") and "#" in ins.op_str and "sp" not in ins.op_str and "x29" not in ins.op_str:
                        try:
                            ints.append((ins.address, int(ins.op_str.split("#")[-1].split(",")[0], 0), f"{m} {ins.op_str}"))
                        except ValueError:
                            pass
                for kind, v in consts:
                    val = float(v)
                    hits = [h for h in (floats if kind == "LdcR4" else ints) if abs(h[1] - val) < 1e-6]
                    tag = f"{typ}.{meth} @+{body:#x}: {kind} {v}"
                    print(f"{tag}: " + ("; ".join(f"FOUND +{a:#x} {t}" for a, _, t in hits[:4]) if hits else "absent"))


if __name__ == "__main__":
    main(sys.argv[1])
