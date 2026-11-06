#!/usr/bin/env python3
"""fieldcheck.py: rtypes' field walker against offsets the port already uses (tfex/Game.hpp, xplay.cpp, rnd.cpp, found by hand).
Each row: type, member, expected offset (instance) or expected static slot (module offset of the slot that holds the field's
address: the RELATIVE reloc of that slot must point at the field's storage)."""
import rtypes, relocs, tfexrefs
KNOWN = [   # (type, member, offset, where it was found)
    ("Monocle.Entity", "Active", 0x10, "Game.hpp"), ("Monocle.Entity", "Visible", 0x11, "Game.hpp"),
    ("Monocle.Entity", "Collidable", 0x12, "Game.hpp"), ("Monocle.Entity", "Position", 0x14, "Game.hpp"),
    ("Monocle.Entity", "Scene", 0x20, "Game.hpp (auto-property)"),
    ("Monocle.Component", "Entity", 0x10, "Game.hpp (auto-property)"), ("Monocle.Component", "Active", 0x21, "Game.hpp"),
    ("Monocle.Component", "Visible", 0x22, "Game.hpp"),
    ("Monocle.GraphicsComponent", "Position", 0x24, "Game.hpp"), ("Monocle.GraphicsComponent", "Scale", 0x34, "Game.hpp"),
    ("Monocle.GraphicsComponent", "Rotation", 0x40, "Game.hpp"), ("Monocle.GraphicsComponent", "Color", 0x44, "Game.hpp"),
    ("Monocle.Engine", "scene", 0xe8, "Game.hpp (private)"), ("Monocle.Engine", "nextScene", 0xf0, "Game.hpp (private)"),
    ("TowerFall.MainMenu", "BackState", 0x9c, "Game.hpp"), ("TowerFall.MainMenu", "state", 0xb4, "Game.hpp (private)"),
    ("TowerFall.MainMenu", "switchTo", 0xc0, "Game.hpp (private)"), ("TowerFall.MainMenu", "ToStartSelected", 0xc8, "Game.hpp"),
    ("TowerFall.MenuItem", "Selected", 0x68, "Game.hpp (property)"), ("TowerFall.MenuItem", "MainMenu", 0x70, "Game.hpp"),
    ("TowerFall.MenuItem", "DownItem", 0x78, "Game.hpp"), ("TowerFall.MenuItem", "UpItem", 0x80, "Game.hpp"),
    ("TowerFall.MenuItem", "RightItem", 0x88, "Game.hpp"), ("TowerFall.MenuItem", "LeftItem", 0x90, "Game.hpp"),
    ("TowerFall.MainModeButton", "text", 0xc8, "Game.hpp (private)"), ("TowerFall.RollcallElement", "playerIndex", 0xc4, "Game.hpp (private)"),
    ("TowerFall.Player", "PlayerIndex", 0xe4, "xplay.cpp"), ("TowerFall.Player", "Dead", 0x108, "xplay.cpp"),
    ("TowerFall.Player", "Speed", 0x2c8, "xplay.cpp"), ("TowerFall.Explosion", "PlayerIndex", 0xfc, "xplay.cpp"),
    ("Monocle.Layer", "Index", 0x38, "xplay.cpp (auto-property)"), ("TowerFall.LevelLoaderXML", "Finished", 0x60, "xplay.cpp"),
    ("Monocle.SFX", "Data", 0x10, "sfx.cpp"), ("Monocle.SFX", "ObeysMasterPitch", 0x18, "sfx.cpp"), ("TowerFall.TowerTheme", "Name", 0x10, "xplay.cpp"),
]
STATICS = [  # (type, member, slot)
    ("TowerFall.MainMenu", "NoGamepadUpdates", 0x4794a00), ("TowerFall.MainMenu", "NoGamepads", 0x4794a08),
    ("TowerFall.MainMenu", "RollcallMode", 0x4792b58), ("TowerFall.MainMenu", "CurrentMatchSettings", 0x4793818),
    ("TowerFall.MainMenu", "VersusMatchSettings", 0x4793398), ("TowerFall.TFGame", "Players", 0x4793890),
    ("TowerFall.TFGame", "Characters", 0x4792458), ("TowerFall.TFGame", "AltSelect", 0x4792460), ("TowerFall.TFGame", "PlayerInputs", 0x4791fa8),
]
def lookup(t, m):
    off = rtypes.field(t, m)
    if off is not None: return off, "field record"
    for owner, f, o, ty in rtypes.fields(t, statics=True):
        if f == m: return o, "field record (static)"
    for c in rtypes.chain(t):
        o = tfexrefs.accessor_offset(c.name, m)
        if o is not None: return o, "get_%s accessor" % m
    return None, "-"
if __name__ == "__main__":
    R = relocs.relative(); ok = miss = bad = 0
    for t, m, want, src in KNOWN:
        got, how = lookup(t, m)
        st = "ok" if got == want else ("MISSING" if got is None else "WRONG")
        ok += st == "ok"; miss += st == "MISSING"; bad += st == "WRONG"
        print(f"  {st:7} {t}.{m:18} want {want:#x} got {('%#x' % got) if got is not None else '-':>8}  via {how:24} ({src})")
    for t, m, slot in STATICS:
        got, how = lookup(t, m)
        st = "ok" if got is not None and R.get(slot) == got else ("MISSING" if got is None else "WRONG")
        ok += st == "ok"; miss += st == "MISSING"; bad += st == "WRONG"
        print(f"  {st:7} {t}.{m:18} slot {slot:#x} -> {R.get(slot, 0):#x}, record {('%#x' % got) if got is not None else '-'}  via {how}")
    print(f"{ok} ok, {miss} missing (private fields: not in BRUTE's reflection), {bad} wrong")
