#!/usr/bin/env python3
"""ssb_to_fortrise.py: the SSB roster (and the reskinned Cyan alt) as FortRise 5 archer mods, one mod folder per unit.

The roster on the console used to be romfs edits (additional-characters/smash-roster-mod/build_unit.py merged each unit into the
game's own archerData and atlases). As FortRise 5 archer mods they load from sd:/xplay/mods/<folder>/ on the Switch (and from
FortRise's Mods/ on PC or the browser), and TF.EX plays them online: a lobby player carries "<mod>/<id>" and "<mod>@<version>".

Source: the PC pack "SSB X TF Archer Loader v1.1.5" (one folder per costume: archerData.xml, atlas.png/.xml, spriteData.xml), the
units of build_unit.py (a hub archer and its costumes). Output per unit, FortRise 5's default paths (FortRise.Content
ContentModule.GetDefaultLoaderConfiguration):
  meta.json
  Content/Atlas/atlas.png + atlas.xml           the costumes' sheets packed together (every sub-texture but the portraits)
  Content/Atlas/menuAtlas.png + menuAtlas.xml   the portraits (FortRise loads <Portraits> from the menu atlas)
  Content/Atlas/SpriteData/spriteData.xml       the costumes' sprites
  Content/Atlas/SpriteData/corpseSpriteData.xml the sprites an archer names as <Corpse>
  Content/Atlas/GameData/archerData.xml         <Archers>: <Archer id>, <AltArcher id Alt>, <SecretArcher id Secret>
  Content/SFX/...                               custom sounds (Rosalina), as <SFX>path/{action}</SFX> + <SFXFallback>

FortRise 5 looks a name up in the mod only when it starts with "@" (EntryExtensions.ResolveID: "@x" -> "<mod>/x"); a bare name is
the game's. So every reference to the mod's own textures and sprites is written "@name", and names only need to be unique inside one
mod: a costume that ships different pixels (or a different sprite) under a name its hub already uses gets a suffixed name. An Alt
or Secret names its hub in full ("<mod>/<hub id>"). The pack's old Archer Loader rules become FortRise 5's: For="hub" -> Alt="<hub id>" (an Alt or Secret
inherits what it leaves out), a repeated scalar tag keeps the last (FortRise reads the first), <Gender> -> <Genders>, <CustomSFX> ->
a path <SFX>, a gem only the PC has -> the Switch's Gem0-9, a sound set the Switch lacks -> 0..7.

The art is Nintendo's, from a third-party pack: the output goes outside any repository and is sideloaded, never hosted.
  tools/mods/ssb_to_fortrise.py [--out DIR] [unit ...]     (default: every unit and the Cyan alt)
"""
import argparse, json, os, re, shutil, struct, sys, zlib
from xml.dom import minidom

HERE = os.path.dirname(os.path.abspath(__file__))
MODS = HERE                                                            # "Towerfall Switch Mods": the first parent holding
while not os.path.isdir(os.path.join(MODS, "additional-characters")):  # additional-characters (a worktree sits deeper)
    if os.path.dirname(MODS) == MODS:
        sys.exit("ssb_to_fortrise: no additional-characters folder above this script")
    MODS = os.path.dirname(MODS)
ROSTER = os.path.join(MODS, "additional-characters/smash-roster-mod")
sys.path.insert(0, ROSTER)
import build_unit as bu                                                # the pack's unit table and its PNG / XML helpers

EXT = bu.EXT                                                           # the game's own content (the base extract)
CYAN_ART = os.path.join(MODS, "additional-characters/sprite-rips/cyan-alt-reskin/final")
DEFAULT_OUT = os.path.join(MODS, "additional-characters/fortrise-mods")
VERSION = "1.0.0"
SHEET_WIDTH = 2048
VANILLA_SFX = {"GREEN": 0, "BLUE": 1, "PINK": 2, "ORANGE": 3, "WHITE": 4, "YELLOW": 5, "CYAN": 6, "PURPLE": 7}
SCALARS = bu.DEDUPE_TAGS + ["Genders", "SFXFallback"]

def camel(s):
    return "".join(w[:1].upper() + w[1:] for w in re.split(r"[^A-Za-z0-9]+", s) if w)

def meta(name, description):
    return {"name": name, "description": description, "version": VERSION,
            "dependencies": [{"name": "FortRise", "version": "5.2.3"}, {"name": "FortRise.Content", "version": "5.2.3"}]}

# ---------- PNG ----------
def write_png(path, w, h, px):
    raw = b"".join(b"\0" + bytes(px[y * w * 4:(y + 1) * w * 4]) for y in range(h))
    open(path, "wb").write(bu.PNG_SIG + bu.chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
                           + bu.chunk(b"IDAT", zlib.compress(raw, 9)) + bu.chunk(b"IEND", b""))

def rect_bytes(sheet, x, y, w, h):
    W, _, px = sheet
    return b"".join(bytes(px[((y + r) * W + x) * 4:((y + r) * W + x + w) * 4]) for r in range(h))

def xml_attr_rects(path):
    t = open(path, encoding="utf-8-sig").read()
    return [(m.group(1), *(int(g) for g in m.groups()[1:]))
            for m in re.finditer(r'<SubTexture name="([^"]+)" x="(\d+)" y="(\d+)" width="(\d+)" height="(\d+)"', t)]

def pad_origins(b, folder):
    """HeadXOrigins / HeadYOrigins padded with 0 to the sheet's frame count (build_unit.py: the Switch's sprite reader indexes
    them per frame; a shorter list than the frames was out of range)."""
    tex = re.search(r"<Texture>([^<]*)</Texture>", b)
    fw, fh = re.search(r"<FrameWidth>(\d+)</FrameWidth>", b), re.search(r"<FrameHeight>(\d+)</FrameHeight>", b)
    if not (tex and fw and fh and int(fw.group(1)) and int(fh.group(1))):
        return b
    st = re.search(rf'<SubTexture name="{re.escape(tex.group(1))}"[^/]*width="(\d+)" height="(\d+)"', open(f"{folder}/atlas.xml", encoding="utf-8-sig").read())
    if not st:
        return b
    n = (int(st.group(1)) // int(fw.group(1))) * (int(st.group(2)) // int(fh.group(1)))
    def pad(m):
        vals = [v for v in m.group(2).split(",") if v.strip() != ""]
        vals += ["0"] * (n - len(vals))
        return f"<{m.group(1)}>" + ",".join(vals[:max(n, len(vals))]) + f"</{m.group(1)}>"
    return re.sub(r"<(HeadXOrigins|HeadYOrigins)>([^<]*)</\1>", pad, b)

# ---------- archerData blocks ----------
def keep_last(inner):
    """The pack relies on 'last one wins' for a repeated scalar tag (<SFX>9</SFX> ... <SFX>0</SFX>); FortRise reads the first."""
    for tag in SCALARS:
        ms = list(re.finditer(rf"\s*<{tag}>[^<]*</{tag}>", inner))
        for m in reversed(ms[:-1]):
            inner = inner[:m.start()] + inner[m.end():]
    return inner

def custom_sfx(inner, sfx_dir_of):
    """<CustomSFX><Name>DIR\\PREFIX</Name><Fallback>BLUE</Fallback></CustomSFX> -> <SFX>Content/SFX/DIR/PREFIX_{action}.wav</SFX> and
    <SFXFallback> (FortRise ArcherLoader: a non-numeric <SFX> is a path with {action}; missing actions use the fallback set)."""
    m = re.search(r"<CustomSFX>(.*?)</CustomSFX>", inner, re.S)
    if not m:
        return inner, None
    name = re.search(r"<Name>([^<]*)</Name>", m.group(1)).group(1).replace("\\", "/")
    fb = re.search(r"<Fallback>([^<]*)</Fallback>", m.group(1))
    fallback = VANILLA_SFX.get((fb.group(1) if fb else "GREEN").strip().upper(), 0)
    inner = re.sub(r"\s*<CustomSFX>.*?</CustomSFX>", "", inner, flags=re.S)
    inner = re.sub(r"\s*<SFX>[^<]*</SFX>", "", inner)
    inner = re.sub(r"\s*<SFXFallback>[^<]*</SFXFallback>", "", inner)
    d, prefix = os.path.split(name)
    inner = f"\n    <SFX>Content/SFX/{name}_{{action}}.wav</SFX>\n    <SFXFallback>{fallback}</SFXFallback>" + inner
    return inner, (sfx_dir_of(d), d)

def fix_gems(inner, defined):
    """A gem the mod defines stays; a gem only the PC has (Gem5alt, Gem3_MENU...) becomes the Switch's Gem<digit>."""
    def fix(m):
        val = m.group(2).strip()
        if re.fullmatch(r"Gem[0-9]", val) or val in defined:
            return m.group(0)
        d = re.search(r"[0-9]", val)
        return f"<{m.group(1)}>Gem{d.group(0) if d else '0'}</{m.group(1)}>"
    return re.sub(r"<(Menu|Gameplay)>([^<]*)</\1>", fix, inner)

TEX_TAGS = ("Texture", "RedTexture", "BlueTexture", "RedTeam", "BlueTeam", "Flash")
def at_refs(text, own, tags=None):
    """"@" in front of each tag value that names something of this mod (tags None: every simple <Tag>value</Tag>)"""
    pat = r"<(%s)>\s*([^<@\s][^<]*?)\s*</\1>" % ("|".join(tags) if tags else r"\w+")
    return re.sub(pat, lambda m: f"<{m.group(1)}>@{m.group(2)}</{m.group(1)}>" if m.group(2) in own else m.group(0), text)

def clean_inner(inner, sfx_dir_of, sprite_ids):
    inner, sfx = custom_sfx(inner, sfx_dir_of)
    inner = keep_last(inner)
    if sfx is None:
        inner = bu.clamp_sfx(inner)
    inner = re.sub(r"<Gender>([^<]*)</Gender>", r"<Genders>\1</Genders>", inner)
    inner = fix_gems(inner, sprite_ids)
    return inner, sfx

# ---------- a hub that is a costume in the pack: what it inherits ----------
INHERITED = ["ColorA", "ColorB", "LightbarColor", "Aimer", "Corpse", "SFX", "VictoryMusic", "Gender", "Sprites", "Hat", "Portraits",
             "Statue", "Gems", "Breathing"]

def inherit(folder, inner):
    """The pack's AltArcher / SkinArcher leaves out what it shares with its For= archer (the old Archer Loader copied it); promoted to
    an archer of its own (build_unit.py's units) it needs them: each missing element from its pack parent, up the chain. Returns the
    completed block and the parent folders whose art it now uses."""
    used, f = [], folder
    while True:
        _, forv, _ = bu.parse_block(f)
        parent = os.path.join(os.path.dirname(f), forv) if forv else None
        if not parent or not os.path.isdir(parent):
            break
        p_inner = bu.parse_block(parent)[2]
        add = []
        for tag in INHERITED:
            if not re.search(rf"<{tag}>", inner):
                m = re.search(rf"<{tag}>.*?</{tag}>", p_inner, re.S)
                if m:
                    add.append(m.group(0))
        if add:
            used.append(parent)
            inner = "\n    " + "\n    ".join(add) + inner
        f = parent
    return inner, used

# ---------- one unit -> one mod ----------
def build_unit(key, out_root):
    u = bu.UNITS[key]
    hub_dir = u["hub"]
    mod = "SSB." + camel(os.path.basename(hub_dir))
    folders = [("Archer", hub_dir)] + list(u["costumes"])
    hub_id = camel(os.path.basename(hub_dir))
    out = os.path.join(out_root, mod)
    if os.path.exists(out):
        shutil.rmtree(out)
    os.makedirs(os.path.join(out, "Content/Atlas/SpriteData"))
    os.makedirs(os.path.join(out, "Content/Atlas/GameData"))

    hub_inner, parents = inherit(hub_dir, bu.parse_block(hub_dir)[2])
    art = [f for _, f in folders] + [p for p in parents if p not in [f for _, f in folders]]   # (parents: art only, no archer)
    sheets = {f: bu.load_atlas_rgba(f"{f}/atlas.png") for f in art}
    ren = {f: {} for f in art}                 # per folder: original name -> name in this mod
    placed = {}                                # sub-texture name -> pixels (to share identical ones)
    subs = []                                  # (name, folder, x, y, w, h)
    for f in art:
        for name, x, y, w, h in xml_attr_rects(f"{f}/atlas.xml"):
            px = rect_bytes(sheets[f], x, y, w, h)
            final, i = name, 1
            while final in placed and placed[final] != px:
                i += 1
                final = f"{name}_{i}"
            if final != name:
                ren[f][name] = final
            if final not in placed:
                placed[final] = px
                subs.append((final, f, x, y, w, h))

    # sprites: this mod's ids (a costume's sprite that differs from its hub's under the same id gets a suffix)
    sprite_xml, sprite_ids = {}, set()
    for f in art:
        p = f"{f}/spriteData.xml"
        if not os.path.exists(p):
            continue
        for b in re.findall(r"<sprite_(?:string|int)\b.*?</sprite_(?:string|int)>", open(p, encoding="utf-8-sig").read(), re.S):
            for old, new in ren[f].items():
                b = b.replace(f">{old}<", f">{new}<")
            b = pad_origins(b, f)
            sid = re.search(r'id="([^"]+)"', b).group(1)
            final, i = sid, 1
            while final in sprite_xml and sprite_xml[final][1] != b.replace(f'id="{sid}"', f'id="{final}"', 1):
                i += 1
                final = f"{sid}_{i}"
            if final != sid:
                ren[f][sid] = final
                b = b.replace(f'id="{sid}"', f'id="{final}"', 1)
            sprite_xml.setdefault(final, (f, b))
            sprite_ids.add(final)

    # archer blocks
    sfx_copies, blocks, corpse_ids, portrait_names, menu_gems = [], [], set(), set(), set()
    ids = {}
    for tag, f in folders:
        inner = hub_inner if f == hub_dir else bu.parse_block(f)[2]
        for src in ([f] + parents if f == hub_dir else [f]):   # (an inherited element names its own folder's art)
            for old, new in ren[src].items():
                inner = inner.replace(f">{old}<", f">{new}<")
        for old, new in ren[f].items():
            inner = inner.replace(f">{old}<", f">{new}<")
        inner, sfx = clean_inner(inner, lambda d: os.path.join(os.path.dirname(hub_dir), "Last Of The Toadstools", "SFX", d), sprite_ids)
        if sfx:
            sfx_copies.append(sfx)
        aid = hub_id if tag == "Archer" else camel(os.path.basename(f))
        ids[f] = aid
        attrs = f'id="{aid}"' + ("" if tag == "Archer" else f' {"Alt" if tag == "AltArcher" else "Secret"}="{mod}/{hub_id}"')
        blocks.append(f"  <{tag} {attrs}>{inner.rstrip()}\n  </{tag}>")
        corpse_ids.update(re.findall(r"<Corpse>([^<]*)</Corpse>", inner))
        menu_gems.update(g for g in re.findall(r"<Menu>([^<]*)</Menu>", inner) if g in sprite_ids)
        portrait_names.update(re.findall(r"<(?:NotJoined|Joined|Win|Lose)>([^<]+)</", inner))

    for g in menu_gems:                        # a menu gem draws from the menu atlas (FortRise: menuSpriteData)
        portrait_names.update(re.findall(r"<Texture>([^<]+)</Texture>", sprite_xml[g][1]))
    # atlases: portraits (and menu gems) to the menu atlas, the rest to the game atlas, each shelf-packed
    def pack(items, path_png, path_xml):
        pos, height = bu.shelf_pack([(s[0], s[4], s[5]) for s in items], SHEET_WIDTH)
        canvas = bytearray(SHEET_WIDTH * 4 * max(height, 1))
        lines = []
        for name, f, x, y, w, h in items:
            dx, dy = pos[name]
            W, _, px = sheets[f]
            bu.blit(canvas, SHEET_WIDTH, px, W, x, y, w, h, dx, dy)
            lines.append(f'  <SubTexture name="{name}" x="{dx}" y="{dy}" width="{w}" height="{h}"/>')
        write_png(path_png, SHEET_WIDTH, max(height, 1), canvas)
        open(path_xml, "w").write('<?xml version="1.0" encoding="utf-8"?>\n<TextureAtlas imagePath="' + os.path.basename(path_png) + '">\n' + "\n".join(lines) + "\n</TextureAtlas>\n")
    game = sorted([s for s in subs if s[0] not in portrait_names], key=lambda s: -s[5])
    menu = sorted([s for s in subs if s[0] in portrait_names], key=lambda s: -s[5])
    pack(game, f"{out}/Content/Atlas/atlas.png", f"{out}/Content/Atlas/atlas.xml")
    pack(menu, f"{out}/Content/Atlas/menuAtlas.png", f"{out}/Content/Atlas/menuAtlas.xml")

    tex_names = {s[0] for s in subs}
    sprite_xml = {sid: (f, at_refs(b, tex_names, TEX_TAGS)) for sid, (f, b) in sprite_xml.items()}
    REF_TAGS = ("Body", "HeadNormal", "HeadNoHat", "HeadCrown", "Bow", "HeadBack", "Corpse", "Menu", "Gameplay", "Normal", "Blue", "Red",
                "NotJoined", "Joined", "Win", "Lose", "Image", "Glow", "Aimer", "Texture", "TextureEnd")
    blocks = [at_refs(b, tex_names | sprite_ids, REF_TAGS) for b in blocks]
    player = [b for sid, (_, b) in sprite_xml.items() if sid not in corpse_ids and sid not in menu_gems]
    corpse = [b for sid, (_, b) in sprite_xml.items() if sid in corpse_ids]
    menus = [b for sid, (_, b) in sprite_xml.items() if sid in menu_gems]
    for name, items in (("spriteData", player), ("corpseSpriteData", corpse), ("menuSpriteData", menus)):
        if not items and name != "spriteData":
            continue
        open(f"{out}/Content/Atlas/SpriteData/{name}.xml", "w").write('<?xml version="1.0" encoding="utf-8"?>\n<SpriteData>\n  ' + "\n  ".join(items) + "\n</SpriteData>\n")
    open(f"{out}/Content/Atlas/GameData/archerData.xml", "w").write("<Archers>\n" + "\n\n".join(blocks) + "\n</Archers>\n")

    for src, d in sfx_copies:
        dst = f"{out}/Content/SFX/{d}"
        if os.path.isdir(src) and not os.path.isdir(dst):
            shutil.copytree(src, dst)
    names = " / ".join(re.search(r"<Name0>([^<]*)</Name0>", b).group(1) + " " + re.search(r"<Name1>([^<]*)</Name1>", b).group(1)
                       for b in blocks if "<Name0>" in b)
    json.dump(meta(mod, f"SSB x TF: {names}"), open(f"{out}/meta.json", "w"), indent=2)
    return mod, [ids[f] for _, f in folders], out

# ---------- the reskinned Cyan alt, MEXICAN PUPPET (was BeardedPuppet): on the Switch the game's own Prancing Puppet alt takes its names
# and portraits (glue SwitchMods.NativeReskin: no second puppet, online it is the vanilla alt); the archer data is whole, so a FortRise
# without that glue loads it as a standalone archer ----------
def build_cyan(out_root):
    mod = "MexicanPuppet"
    old = os.path.join(out_root, "BeardedPuppet")                             # (its former name)
    if os.path.exists(old):
        shutil.rmtree(old)
    out = os.path.join(out_root, mod)
    if os.path.exists(out):
        shutil.rmtree(out)
    os.makedirs(f"{out}/Content/Atlas/GameData")
    os.makedirs(f"{out}/Content/images")
    ad = open(f"{EXT}/Content/Atlas/GameData/archerData.xml", encoding="utf-8-sig").read()
    main = re.findall(r"<Archer>(.*?)</Archer>", ad, re.S)[6]
    alt = re.findall(r"<AltArcher>(.*?)</AltArcher>", ad, re.S)[6]
    inner = main
    for blk in ("Corpse", "Sprites", "Hat", "Statue"):                         # the alt's own, over the main archer's
        m = re.search(rf"<{blk}>.*?</{blk}>", alt, re.S)
        if m:
            inner = re.sub(rf"<{blk}>.*?</{blk}>", lambda _m, s=m.group(0): s, inner, count=1, flags=re.S)
    portraits = "<Portraits>\n" + "".join(
        f"      <{tag}>Content/images/{png}</{tag}>\n" for tag, png in
        (("NotJoined", "notJoinedAlt6.png"), ("Joined", "joinedAlt6.png"), ("Win", "winAlt6.png"), ("Lose", "loseAlt6.png"))) + "    </Portraits>"
    inner = re.sub(r"<Portraits>.*?</Portraits>", lambda _m: portraits, inner, count=1, flags=re.S)
    inner = re.sub(r"<Gender>([^<]*)</Gender>", r"<Genders>\1</Genders>", inner)
    inner = re.sub(r"<Name0>[^<]*</Name0>", "<Name0>MEXICAN</Name0>", inner, count=1)
    for png in os.listdir(CYAN_ART):
        shutil.copy(os.path.join(CYAN_ART, png), f"{out}/Content/images/{png}")
    open(f"{out}/Content/Atlas/GameData/archerData.xml", "w").write(f'<Archers>\n  <Archer id="{mod}">{inner.rstrip()}\n  </Archer>\n</Archers>\n')
    json.dump(meta(mod, "MEXICAN PUPPET: Prancing Puppet's alt costume with the bearded portraits (combined-mod); on the Switch the game's own alt"), open(f"{out}/meta.json", "w"), indent=2)
    return mod, [mod], out

# ---------- checks: every name an archer uses resolves in the mod or the game ----------
def game_names():
    names = set()
    for p in ("Content/Atlas/atlas.xml", "DarkWorldContent/Atlas/atlas.xml", "Content/Atlas/menuAtlas.xml"):
        if os.path.exists(f"{EXT}/{p}"):
            names |= set(re.findall(r'<SubTexture name="([^"]+)"', open(f"{EXT}/{p}", encoding="utf-8-sig").read()))
    sprites = set()
    for p in ("spriteData", "corpseSpriteData", "menuSpriteData"):
        sprites |= set(re.findall(r'<sprite_(?:string|int) id="([^"]+)"', open(f"{EXT}/Content/Atlas/SpriteData/{p}.xml", encoding="utf-8-sig").read()))
    return names, sprites

def check(out, g_tex, g_spr):
    """FortRise's lookup: "@x" in this mod, a bare x in the game (a file path in the mod for textures)."""
    problems = []
    def read(p):
        return open(p).read() if os.path.exists(p) else ""
    for p in ("Content/Atlas/GameData/archerData.xml", "Content/Atlas/atlas.xml", "Content/Atlas/menuAtlas.xml",
              "Content/Atlas/SpriteData/spriteData.xml", "Content/Atlas/SpriteData/corpseSpriteData.xml", "Content/Atlas/SpriteData/menuSpriteData.xml"):
        if os.path.exists(f"{out}/{p}"):
            try:
                minidom.parse(f"{out}/{p}")
            except Exception as e:
                problems.append(f"{p}: not XML ({e})")
    def ok(name, mine, game):
        return name[1:] in mine if name.startswith("@") else name in game
    tex = set(re.findall(r'<SubTexture name="([^"]+)"', read(f"{out}/Content/Atlas/atlas.xml") + read(f"{out}/Content/Atlas/menuAtlas.xml")))
    spr = set(re.findall(r'<sprite_(?:string|int) id="([^"]+)"', read(f"{out}/Content/Atlas/SpriteData/spriteData.xml") + read(f"{out}/Content/Atlas/SpriteData/corpseSpriteData.xml") + read(f"{out}/Content/Atlas/SpriteData/menuSpriteData.xml")))
    ad = read(f"{out}/Content/Atlas/GameData/archerData.xml")
    for b in re.findall(r"<Sprites>(.*?)</Sprites>", ad, re.S):
        for name in re.findall(r">([^<]+)<", b):
            if name.strip() and not ok(name.strip(), spr, g_spr):
                problems.append(f"sprite {name.strip()} not in the mod or the game")
    for c in re.findall(r"<Corpse>([^<]*)</Corpse>", ad):
        if not ok(c, spr, g_spr):
            problems.append(f"corpse {c} not in the mod or the game")
    for blk in ("Portraits", "Hat", "Statue"):
        for b in re.findall(rf"<{blk}>(.*?)</{blk}>", ad, re.S):
            for name in re.findall(r"<(?!Material)\w+>([^<]+)</\w+>", b):
                n = name.strip()
                if not ok(n, tex, g_tex) and not os.path.exists(f"{out}/{n}"):
                    problems.append(f"{blk} texture {n} not in the mod or the game")
    for name in re.findall(r"<(?:Menu|Gameplay)>([^<]*)</(?:Menu|Gameplay)>", ad):
        if not ok(name, spr, g_spr):
            problems.append(f"gem {name} not in the mod or the game")
    for name in re.findall(r"<Aimer>([^<]*)</Aimer>", ad):
        if not ok(name, tex, g_tex):
            problems.append(f"aimer {name} not in the mod or the game")
    for s in re.findall(r"<sprite_string\b.*?</sprite_string>", read(f"{out}/Content/Atlas/SpriteData/spriteData.xml") + read(f"{out}/Content/Atlas/SpriteData/corpseSpriteData.xml"), re.S):
        for t in re.findall(r"<(?:Texture|RedTexture|BlueTexture|RedTeam|BlueTeam|Flash)>([^<]+)<", s):
            if not ok(t, tex, g_tex):
                problems.append(f"sprite texture {t} not in the mod or the game")
    return sorted(set(problems))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("units", nargs="*")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    keys = a.units or list(bu.UNITS) + ["cyan"]
    g_tex, g_spr = game_names()
    bad = 0
    for k in keys:
        mod, ids, out = build_cyan(a.out) if k == "cyan" else build_unit(k, a.out)
        problems = check(out, g_tex, g_spr)
        bad += len(problems)
        print(f"{mod}: archers {', '.join(ids)}" + ("" if not problems else "\n  " + "\n  ".join(problems)))
    print(f"{len(keys)} mods in {a.out}; {bad} problem(s)")
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main())
