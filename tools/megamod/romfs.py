#!/usr/bin/env python3
"""romfs.py <romfs root>: the megamod's game data (hw-probe/exlaunch/source/program/megamod) into a Switch romfs, next to what
tools/pc-levels.sh puts there. docs/PLAN-MEGAMOD-2026-10-30.md.

romfs.py --clean <romfs root>: removes what an earlier run wrote (a "TF.EX Switch" build, no extras: tools/run-probe.sh XPLAY_EXTRAS=0).
What it writes (and lists in <romfs root>/.xplay-megamod, so a later run removes what it no longer writes):
  - DarkWorldContent/Atlas/atlas.{png,xml} and Content/Atlas/menuAtlas.{png,xml}: the game's own atlases with only the megamod's
    entries added below them (TowerBall's baskets and ball, three-team's yellow team banner, 8-player's controller icons 7/8, the
    TOWERBALL mode banner); the SSB roster's art is not here: it is FortRise archer mods now (tools/mods/ssb_to_fortrise.py), so
    the game's archer data stays the game's, which TF.EX's online play needs (archer indices).
  - no levels any more (2026-11-04): TowerBall's are its folder's, the 2-8 lobby plays WiderSet's (its folder).
The one change online can see: arrows/squeakyArrow is TowerBall's ball (the toy arrow's look on this console only).
Sources (never in git: the game's files and the mods' third-party art): the game's own romfs (XPLAY_SWITCH_ROMFS, default the roster
tools' base extract) and ../combined-mod/romfs (XPLAY_MEGAMOD_ART). (three-team's yellow archer sheets aren't in combined-mod: its code no longer reads them.)"""
import os, re, shutil, struct, sys, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
MODS = HERE
while not os.path.isdir(os.path.join(MODS, "combined-mod")):
    if os.path.dirname(MODS) == MODS:
        sys.exit("megamod romfs: no combined-mod folder above this script")
    MODS = os.path.dirname(MODS)
sys.path.insert(0, os.path.join(MODS, "additional-characters/smash-roster-mod"))
import build_unit as bu                                     # PNG reading (load_atlas_rgba) and chunk writing

GAME = os.environ.get("XPLAY_SWITCH_ROMFS", bu.EXT)
ART = os.environ.get("XPLAY_MEGAMOD_ART", os.path.join(MODS, "combined-mod/romfs"))
MARKER = ".xplay-megamod"

# The megamod's atlas entries (the rest of combined-mod's additions are the roster's)
# (TowerBall's art and levels are its mod folder now, sd:/xplay/mods/Switch.TowerBall: tools/mods/towerball_folder.py, 2026-11-04)
ATLASES = {
    "Content/Atlas/menuAtlas": lambda n: re.fullmatch(r"controls/\w+/player[78]", n) is not None,
}

def rects(xml):
    t = open(xml, encoding="utf-8-sig").read()
    return t, {m.group(1): tuple(int(g) for g in m.groups()[1:]) for m in
               re.finditer(r'<SubTexture name="([^"]+)" x="(\d+)" y="(\d+)" width="(\d+)" height="(\d+)"\s*/>', t)}

def write_png(path, w, h, px):
    raw = b"".join(b"\0" + bytes(px[y * w * 4:(y + 1) * w * 4]) for y in range(h))
    open(path, "wb").write(bu.PNG_SIG + bu.chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
                           + bu.chunk(b"IDAT", zlib.compress(raw, 6)) + bu.chunk(b"IEND", b""))

def atlas(rel, keep, out, written):
    gtext, grects = rects(os.path.join(GAME, rel + ".xml"))
    _, arects = rects(os.path.join(ART, rel + ".xml"))
    gw, gh, gpx = bu.load_atlas_rgba(os.path.join(GAME, rel + ".png"))
    aw, ah, apx = bu.load_atlas_rgba(os.path.join(ART, rel + ".png"))
    names = [n for n in arects if keep(n) and (n not in grects or grects[n] != arects[n])]
    # shelf-pack them below the game's image, tallest first
    x = y = shelf = 0
    place = {}
    for n in sorted(names, key=lambda n: -arects[n][3]):
        w, h = arects[n][2:]
        if x + w > gw: x, y, shelf = 0, y + shelf + 1, 0
        place[n] = (x, gh + y, w, h); x += w + 1; shelf = max(shelf, h)
    nh = gh + y + shelf + 1
    px = bytearray(gpx) + bytearray(gw * (nh - gh) * 4)
    for n, (dx, dy, w, h) in place.items():
        sx, sy = arects[n][:2]
        for r in range(h):
            s = ((sy + r) * aw + sx) * 4; d = ((dy + r) * gw + dx) * 4
            px[d:d + w * 4] = apx[s:s + w * 4]
    text = gtext
    for n, (dx, dy, w, h) in place.items():
        line = f'<SubTexture name="{n}" x="{dx}" y="{dy}" width="{w}" height="{h}"/>'
        old = re.search(r'<SubTexture name="%s" [^>]*/>' % re.escape(n), text)
        text = text[:old.start()] + line + text[old.end():] if old else text.replace("</TextureAtlas>", "    " + line + "\n</TextureAtlas>")
    os.makedirs(os.path.dirname(os.path.join(out, rel)), exist_ok=True)
    write_png(os.path.join(out, rel + ".png"), gw, nh, px)
    open(os.path.join(out, rel + ".xml"), "w", encoding="utf-8").write(text)
    written += [rel + ".png", rel + ".xml"]
    replaced = [n for n in place if n in grects]
    print(f"megamod romfs: {rel}: {len(place)} entries added below the game's {gw}x{gh} (now {gw}x{nh})"
          + (f", replaced: {', '.join(replaced)}" if replaced else ""))

def levels(out, written):
    # (none since 2026-11-04: TowerBall's tb.oel are its mod folder's, and the 2-8 lobby plays WiderSet's levels from WiderSet's folder,
    # so the megamod's spawn-edited 6P-Versus copies are retired; a romfs from before loses them through the marker below)
    print("megamod romfs: no level files (TowerBall's and WiderSet's come from their mod folders)")

def clean(out):
    mk = os.path.join(out, MARKER)
    rels = [r for r in (open(mk).read().split("\n") if os.path.exists(mk) else []) if r]
    for rel in rels:
        if os.path.exists(os.path.join(out, rel)): os.remove(os.path.join(out, rel))
    if os.path.exists(mk): os.remove(mk)
    print(f"megamod romfs: {len(rels)} files removed (no extras)")

def main():
    if len(sys.argv) == 3 and sys.argv[1] == "--clean": return clean(sys.argv[2])
    if len(sys.argv) != 2: sys.exit(__doc__)
    out = sys.argv[1]
    for d in (GAME, ART):
        if not os.path.isdir(d): sys.exit(f"megamod romfs: no {d}")
    written = []
    for rel, keep in ATLASES.items(): atlas(rel, keep, out, written)
    levels(out, written)
    mk = os.path.join(out, MARKER)
    for rel in (open(mk).read().split("\n") if os.path.exists(mk) else []):
        if rel and rel not in written and os.path.exists(os.path.join(out, rel)): os.remove(os.path.join(out, rel))
    open(mk, "w").write("\n".join(sorted(written)) + "\n")

if __name__ == "__main__":
    main()
