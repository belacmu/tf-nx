#!/usr/bin/env python3
"""build_folder.py <out dir>: TowerBall's mod folder (sd:/xplay/mods/Switch.TowerBall/, mods/Switch.TowerBall/mod.toml): its meta.json,
its art as FortRise atlases holding only its own images, its levels.

  Content/Atlas/atlas.xml/.png       rim, net, rimR, netR, chest, netS1..3, netRS1..3, ball (FortRise names them Switch.TowerBall/<name>)
  Content/Atlas/menuAtlas.xml/.png   banner (the TOWERBALL game mode's icon)
  Content/Levels/{Versus,6P-Versus}/<tower>/tb.oel

The art and levels are the TowerBall mod's (third-party, never in git): taken from the combined mod's romfs (XPLAY_MEGAMOD_ART, default
../combined-mod/romfs), where tools/megamod/romfs.py used to composite them into the game's atlases. The folder's files are what the
catalog entry (mod.toml) lists as needed."""
import os, re, shutil, struct, sys, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
MODS = HERE
while not os.path.isdir(os.path.join(MODS, "combined-mod")):
    if os.path.dirname(MODS) == MODS:
        sys.exit("towerball folder: no combined-mod folder above this script")
    MODS = os.path.dirname(MODS)
sys.path.insert(0, os.path.join(MODS, "additional-characters/smash-roster-mod"))
import build_unit as bu                                     # PNG reading (load_atlas_rgba) and chunk writing

ART = os.environ.get("XPLAY_MEGAMOD_ART", os.path.join(MODS, "combined-mod/romfs"))
META = os.path.join(HERE, "meta.json")
# the folder's atlases: (its file, the combined mod's atlas, {its name: the combined mod's name})
ATLASES = [
    ("Content/Atlas/atlas", "DarkWorldContent/Atlas/atlas",
     {**{n: "towerball/" + n for n in ("rim", "net", "rimR", "netR", "chest", "netS1", "netS2", "netS3", "netRS1", "netRS2", "netRS3")},
      "ball": "arrows/squeakyArrow"}),
    ("Content/Atlas/menuAtlas", "Content/Atlas/menuAtlas", {"banner": "gameModes/warlord"}),
]


def rects(xml):
    t = open(xml, encoding="utf-8-sig").read()
    return {m.group(1): tuple(int(g) for g in m.groups()[1:]) for m in
            re.finditer(r'<SubTexture name="([^"]+)" x="(\d+)" y="(\d+)" width="(\d+)" height="(\d+)"\s*/>', t)}


def write_png(path, w, h, px):
    raw = b"".join(b"\0" + bytes(px[y * w * 4:(y + 1) * w * 4]) for y in range(h))
    open(path, "wb").write(bu.PNG_SIG + bu.chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
                           + bu.chunk(b"IDAT", zlib.compress(raw, 9)) + bu.chunk(b"IEND", b""))


def atlas(out, rel, src, names):
    have = rects(os.path.join(ART, src + ".xml"))
    aw, _, apx = bu.load_atlas_rgba(os.path.join(ART, src + ".png"))
    missing = [s for s in names.values() if s not in have]
    if missing: sys.exit(f"towerball folder: {src} has no {', '.join(missing)}")
    width = max(256, max(have[s][2] for s in names.values()))
    x = y = shelf = 0
    place = {}
    for n in sorted(names, key=lambda n: -have[names[n]][3]):   # shelf-packed, tallest first
        w, h = have[names[n]][2:]
        if x + w > width: x, y, shelf = 0, y + shelf + 1, 0
        place[n] = (x, y, w, h); x += w + 1; shelf = max(shelf, h)
    height = y + shelf
    px = bytearray(width * height * 4)
    for n, (dx, dy, w, h) in place.items():
        sx, sy = have[names[n]][:2]
        for r in range(h):
            s = ((sy + r) * aw + sx) * 4; d = ((dy + r) * width + dx) * 4
            px[d:d + w * 4] = apx[s:s + w * 4]
    os.makedirs(os.path.dirname(os.path.join(out, rel)), exist_ok=True)
    write_png(os.path.join(out, rel + ".png"), width, height, px)
    lines = [f'    <SubTexture name="{n}" x="{dx}" y="{dy}" width="{w}" height="{h}"/>' for n, (dx, dy, w, h) in sorted(place.items())]
    open(os.path.join(out, rel + ".xml"), "w", encoding="utf-8").write(
        f'<?xml version="1.0" encoding="utf-8"?>\n<TextureAtlas imagePath="{os.path.basename(rel)}.png">\n' + "\n".join(lines) + "\n</TextureAtlas>\n")
    return len(place)


def levels(out):
    n = 0
    for root, _, files in os.walk(os.path.join(ART, "Content/Levels")):
        if "tb.oel" not in files: continue
        rel = os.path.relpath(os.path.join(root, "tb.oel"), ART)
        os.makedirs(os.path.dirname(os.path.join(out, rel)), exist_ok=True)
        shutil.copyfile(os.path.join(root, "tb.oel"), os.path.join(out, rel)); n += 1
    return n


def main():
    if len(sys.argv) != 2: sys.exit(__doc__)
    out = sys.argv[1]
    if not os.path.isdir(ART): sys.exit(f"towerball folder: no {ART}")
    if os.path.isdir(out): shutil.rmtree(out)
    os.makedirs(out)
    shutil.copyfile(META, os.path.join(out, "meta.json"))
    counts = [atlas(out, rel, src, names) for rel, src, names in ATLASES]
    n = levels(out)
    print(f"towerball folder: {out}: {counts[0]} images in its atlas, {counts[1]} in its menu atlas, {n} levels")


main()
