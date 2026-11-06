# FortRise mods on the Switch? (research, 2026-10-20)

Read-only research. Nothing was built, downloaded or run.

**Question (the user, on item 11, custom skins):** "custom skins is actually something I would really like support for, both in and
out, BUT, there are other mods I also will want support for, like Towerball. And that then raises the question, should we actually
just make something that makes fortrise mods possible for switch? Is that completely insane? We need to investigate."

**Short answer.**
- **Content mods (archers / skins, towers, textures): not insane.** A loader that reads FortRise mods from the SD card and
  registers their content in the game is a contained project. It is also the right foundation for custom skins in both directions.
- **Code mods (most mods, TowerBall among them): a general loader is insane.** The Switch game has no .NET runtime to load a DLL
  into. Code mods can only come one at a time, translated into the build the way TF.EX is, each with its author's permission.

## 1. What FortRise mods are

The browser's mod catalog (`~/Code/Towerfall Browser/mods/catalog.json`, 60 GameBanana mods, each checked under FortRise 5.x in the
browser):

| kind | count | examples | what it contains |
|---|---|---|---|
| content only (no DLL) | 15 | 12 archers (Among Us Red & Blue, Celeste, Yuu, Skeleton...), 2 towers, 1 texture | `meta.json`, `Content/`: atlas XML + PNG, sprite XML, archer XML, tower levels |
| code (a .NET DLL, almost all with Harmony patches) | 45 | variants and modes (Accelerate, Soccer, BaronMode, Tournament...), WiderSet, TF.EX, TowerBall | compiled C#: Harmony patches on TowerFall's methods, often content too |

- Content mods are loaded by **FortRise.Content**, a built-in FortRise mod: 28 files, 3.4k lines decompiled. That includes
  ArcherLoader (550 lines), SpriteDataLoader (238), SubtextureLoader (129), VersusLoader, ThemeLoader and BackgroundLoader. It feeds
  FortRise core's registries (archers, sprites, subtextures: a few hundred lines) and FortRise's patches that make the game accept
  more archers than its own (`.cache/fortrise-browser-mm`, 50k lines in all, most of it not needed for this).
- **TowerBall** ("TowerBall For FortRise", 2023, 122 KB) is a **code mod** (`TowerBall.dll`, Harmony). It was built for an older
  FortRise, and the catalog marks it broken: "FortRise didn't load it (check its dependencies)". So it doesn't run in the browser
  either today. I didn't download it to look inside: that needs your OK, and it isn't redistributable.

## 2. Why the Switch can't simply load FortRise mods

On PC, FortRise is a .NET mod loader: it loads mod DLLs into the game's runtime and applies their patches at startup (MonoMod,
Harmony). On the Switch, TowerFall is compiled ahead of time to native ARM code (BRUTE). There is no CLR, no JIT and no assembly
loading. A mod DLL is .NET bytecode with nothing to run it.

TF.EX runs on the Switch only because we translate its C# to C++ at build time (tools/tfex/translator) and bind it to the game's
native code by address. That works, but per codebase: every TF.EX release is re-translated, and new constructs need translator work.

## 3. Options

| option | covers | effort | verdict |
|---|---|---|---|
| A. **Content-mod loader**: mod folders on the SD card (`sd:/xplay/mods/<Mod>/`, the zip extracted as is), FortRise.Content's loaders and FortRise's archer / sprite / subtexture registries translated with our translator, the game's own PNG and XML loading underneath | the 15 content mods; **custom skins out** (a player's archer mod on the SD card is what TF.EX streams); **skins in** (streamed skins register into the same registries) | medium: a few weeks, mostly the archer path first | **recommended** |
| B. **Per-mod translation of code mods** (the TF.EX way): decompile, translate, bind and hook one mod at a time into the module, on by a setting | any single code mod someone asks for (TowerBall, Soccer, Accelerate...) | a few days to weeks per mod, by size; each must be kept in step with its updates | **case by case**, and only with the author's permission (most mods aren't redistributable, and the translated code would ship in our build) |
| C. **A general code-mod loader**: a .NET (CIL) interpreter on the Switch running mod DLLs against the native game, with a Harmony stand-in | all mods | a VM, a class library, the game's object model, and generics and delegates across native code: years | **not feasible** (the "completely insane" one) |

Multiplayer, for any of them: a code mod that changes gameplay has to run on every peer, at the same version, or the match desyncs.
TF.EX shows a lobby's mods and warns about desync risk. Content skins are cosmetic and streamed, so they don't have this problem.

## 4. What this means for item 11 (custom skins)

Skins "in and out" both need part of option A:
- **out:** the Switch's own custom archer. On PC it comes from a FortRise archer mod; on the Switch it would be the same mod's folder
  on the SD card, loaded by A. TF.EX's SkinStreamService then builds and streams its bundle as on PC.
- **in:** TF.EX's SkinOverlayService registers a received skin's images and sprites through FortRise's registries
  (ModRegistry.Subtextures / Sprites). Those are the same registries A provides.

TF.EX's own skin code needs runtime pieces either way: binary MessagePack for the bundle, SHA-256, base64, inflate (miniz_oxide is
already linked for match reports), and textures from RGBA. FortRise's hair (`ExtraHairData`) is a FortRise addition to the game; it
would come with A's patches or be left out.

**Suggested order:** A's archer path (load archer mods from the SD card, register them, the roll call offers them), then TF.EX's skin
stream and overlay on top of it (item 11, both directions), then A's other content kinds (towers, textures). Code mods only on
request, one at a time (B). TowerBall would first need a version that loads on today's FortRise.

## 5. FortRise's source, and the first translation pass (2026-10-20)

**License:** FortRise is MIT (Copyright (c) 2023 Terria Katsura; `vendor/fortrise/LICENSE`). That allows translating it and shipping
the result, as long as the copyright and permission notice go with it (a NOTICE in the Switch build's files would carry it).

**Done:**
- `vendor/fortrise`: a submodule pinned at tag 5.5.0-beta.3 (commit 38da1c9), the FortRise version the browser runs.
- The translator compiles it as a second compilation (`Workspace.ForFortRise`), against FortRise's own stripped `TowerFall.exe`.
  Units whose file starts `fortrise/` translate from it. `translator diag-fortrise` reports its errors: 102, none on the archer path.
- General translator fixes found on the way: semantic models per syntax tree (partial types across files), and a recursion guard for
  self-referencing generics.

**First pass of the archer path** (FortRise.Content's ArcherLoader, SpriteDataLoader, SubtextureLoader, and FortRise's ModArchers),
not committed. It reaches 214 FortRise types through auto units:
- 84 registry types: FortRise's ModRegistry builds all 24 registries;
- 25 mod-loader and 24 module types (ModuleManager, events, the resource tree);
- 18 of FortRise's patches to Monocle (Atlas.SafeAdd, SpriteData registration);
- UI and utility types.

The code also uses what the translator doesn't do yet:
- generic structs and classes: `SpriteConfiguration<T>`, `SpriteEntry<T>`, `Animation<T>`, `Option<T>`, `RegistryQueue<T>`;
- readonly structs with init-only properties, copied with `with { ... }`;
- ref locals over `CollectionsMarshal.GetValueRefOrAddDefault`, `Unsafe.IsNullRef`, `Guid.CreateVersion7`.

The Game's side is what the PC already does: `ArcherData.get_Amount` is `ArcherData.Archers.Length` on the Switch too (+0x96be70), and
the roll call wraps by it (RollcallElement's CharacterIndex setter, +0xb33e20). So enlarging `Archers` / `AltArchers` /
`SecretArchers`, as FortRise's ModArchers.Invoke does, makes archer select offer the new archers.

**Ways on:**
1. Translate FortRise.Content's loaders and ModArchers, and cut at FortRise's framework with Switch rules (TFEX-DIFF S-native: content
   mods load once at boot, after the game's content, so FortRise's queues invoke at once and only the archer path's registries
   exist). FortRise's subtexture and sprite registries become small glue that follows their code (Atlas.SafeAdd, the SpriteData
   entries), plus generic structs in the translator. Days.
2. Translate all of it as is: generic classes and the constructs above in the translator first. Weeks.
3. Port the archer path as glue (option c). Days, and the least FortRise code.

## 6. The archer path on the Switch (2026-10-20, branch ws/tfex-update)

**Status: content-mod archers load from the SD card and play online.** In the emulator (hidden runs, the update slot), the
AmongUsRedBlue mod (GameBanana 667614, the zip unchanged) loads at boot. Its archer shows in archer select. A lobby match against the
browser with the same mod and both players on that archer runs in sync (`tools/smoke.sh lobby "lobbyarcher 10"` with
`LVB_MODS=TF.EX,AmongUsRedBlue LVB_PAGEPICK="ArrowLeft ArrowLeft"`: PASS, 0 of 599 ticks differ, `lobby start: archers 10 10`).

**Where a player puts mods:** `sd:/xplay/mods/<folder>/`, one folder per mod, holding the mod's zip extracted as is (its `meta.json`
and `Content/` side by side, the same files a PC player puts in FortRise's `Mods/`). A folder without `meta.json` is skipped. The mod's
name is meta.json's `name` (FortRise's ModuleMetadata.Name), and archer IDs are `<name>/<id>`, as on PC.

**What runs:**
- FortRise's own code, translated: FortRise.Content's ArcherLoader, SpriteDataLoader, SubtextureLoader and TextureUtilities;
  FortRise's ModArchers, ArcherRegistry, ArcherEntry and EntryExtensions (units `Fr_*`, `Auto_*` from `vendor/fortrise`, MIT).
- TF.EX's own custom-archer code, no longer cut: `ArcherDataExtensions.GetCustomArcherId`, `ResolveCustomArcher`,
  `GetInstalledArcherMods`, `IsSharedByAllPlayers`, `Exists`. `Interop.ArcherRegistryApi.Current` is FortRise's ModArchers. A
  lobby player carries `CustomArcherId` and `ArcherMods` (`"<mod>@<version>"`, the version from meta.json) as on PC, and the
  other side maps the ID to its own index.
- Our glue, marked as such (`tools/tfex/translator/glue/fortrise/SwitchMods.cs`; it can be translated from FortRise later): FortRise's
  framework where the loaders meet it. That covers the SD resources (IResourceInfo), the subtexture and sprite registries
  (SubtextureRegistry / SpriteRegistry: one sprite table by ID, vanilla entries cached), IDPool, the registry queues (invoked at once),
  and the boot driver (ContentModule's loader order). The runtime (`tfex/runtime/tfexgen_frmods.cpp`) lists `sd:/xplay/mods` and reads
  files with nn::fs.
- Boot: `tfex/FrModsBoot.cpp` hooks GameData.Load and loads the mods right after it, where FortRise's TFGame.Load calls
  RiseCore.Initialize. Arrow, Particles and the rest are initialized after the mod archers exist, as on PC.

**TFEX-DIFFs (each a Switch difference):**
- The Switch's game has 10 archers (its archerData.xml), the PC's 9. FortRise warms its archer IDs at 8, so here they start after
  the Switch's own (the first mod archer is index 10 on the Switch, 9 on PC). TF.EX's CustomArcherId maps one to the other.
- Files are read through nn::fs into memory: XML through `XmlDocument.LoadXml`, PNGs through a `MemoryStream` and
  `Texture2D.FromStream`. The game's FileStream can't open `sd:/`: Mono's `Path.GetFullPath` makes it `/sd:/...`, and the runtime's
  path mapper (+0x8d00) only undoes that for `rom:/` and `save:/`.
- FortRise's PlayerCorpse patch (a corpse ID missing from CorpseSpriteData is the main SpriteData's) is not a patch here. When the
  two SpriteData share an atlas, the glue puts the mod archer's corpse XML in CorpseSpriteData too (log: `[frmods] corpse ...`).
- A mod's own music (2026-10-31): FortRise.Content's MusicLoader (Content/Music/*.ogg|wav) and ModMusics / MusicRegistry / MusicEntry,
  and patch_ArcherData.PlayVictoryMusic, translated. FortRise's patch_Music (its own music engine: XACT cues, OGG / WAV streamed through
  DynamicSoundEffectInstance, cross-fades) is not: the game's Music stays for its cues and a registered track plays beside it as the
  game's SoundEffectInstance, read whole (OGG decoded to WAV by stb_vorbis, FAudio's copy: hw-probe/.../thirdparty/stb_vorbis), the way
  the mods' sounds load. The game's Music.Play / PlayImmediate / Stop end it; Music.Update plays PlayNext's queue (TheArchives after a
  victory). TFEX-DIFF S-native (overrides.toml's patch_Music rules; tfex/FrModsBoot.cpp). Log: `[frmods] music: <mod>/<id> plays`.
  Checked: localrun with AmongUs given an OGG `<VictoryMusic>` (hw.txt `localvictory`): decoded (48 kHz, 2 channels, 11.2 s), played,
  then TheArchives. Not heard (the emulator runs without audio). The Celeste mod's tracks register but its archers are the Switch's own
  archer 9, whose victory music is the game's.
- Not loaded yet: content.json
  loader settings (FortRise's default paths are used), and the other content kinds (towers, themes, backgrounds, Dark World, quests).
  (A mod archer's own sounds and FortRise hair: since 2026-10-30, docs/PLAN-MEGAMOD-2026-10-30.md step 3.)

**Madeline online (2026-10-30).** TF.EX knows 9 vanilla archers (ArcherDataExtensions.VanillaArcherCount); the Switch's archer 9
(Madeline, TENACIOUS TRAVELER; alt Badeline) went online as "index 9, no mod name", so every side played a stand-in. PC players have
her as GameBanana's "Madeline & Badeline" (FortRise mod `Celeste` 1.2.0, mod 619431, CC BY-NC-ND: each player installs it; never
hosted or changed by us). With that mod on the SD card (sd:/xplay/mods/Celeste/), glue SwitchMods.NativeAlias (TFEX-DIFF S-native):
its archers aren't loaded (no second Madeline), its names (`Celeste/Madeline`, `/Badeline`, `/MadelineButPink`) are ArcherRegistry
entries on the Switch's archer 9, and the Switch's Madeline and Badeline use the mod's sprites (TF.State saves animation frames: the
mod's dodge has 7 frames to the Switch's 9, fall and glide 6 to 7; the native sprites desynced smoke 170507 at frame 1140). Hair stays
the Switch's own. Checked: smoke lobby, the Switch on Madeline (lobbyarcher 9), the page with TF.EX + Celeste: in sync (171208). A PC
player without the mod: a stand-in, with the Switch's Madeline streamed as a skin (TF.EX's SkinStreamService).

**Console check (next hardware session):** copy the mod folder to `sd:/xplay/mods/AmongUsRedBlue/`. The log must show
`[frmods] mod 0: AmongUsRedBlue 1.0.0`, `[frmods] AmongUsRedBlue: archers 10 -> 11`, `[frmods] corpse AmongUsRedBlue/Grey: ...` and
`[frmods] done`. Then in a lobby against the browser with the same mod, pick the red crewmate (left of the first archer). Both sides
should show it, and the match should run in sync.

## 7. Custom skins on the Switch (item 11, 2026-10-20, branch ws/tfex-update)

TF.EX's SkinStreamService and SkinOverlayService are generated whole and on by default, both directions, as TF.EX's README describes.
The OPTIONS screen's CUSTOM SKINS (TF.EX's, default FULL) applies. The `todo` [service] rules are replaced by per-member rules onto
the two generated services (`GlueSkins::Stream()` / `Overlay()`: one instance each, as TF.EX's ServiceCollections).
- **out:** a Switch player who readies on a mod archer from `sd:/xplay/mods` publishes it (TF.EX's PublishCustomSkin). The bundle is
  built from the mod's textures and sprites and sent over the lobby server as `SkinChunk` messages. A page without the mod plays
  against the red crewmate.
- **in:** a received bundle is checked and kept in memory only. TF.EX's ArcherData.Get postfix (ArcherDataPatch, inside a SkinSlot
  seat scope) gives that seat a clone of its fallback archer with the streamed textures and sprites, registered through the glue's
  TF.EX FortRise registry (`ModRegistryApi.Current`).
- Seat scopes on: ArcherDataPatch whole, PlayerPatch's ctor prefix and finalizer (its DoWrapRender prefix and Update postfix stay as
  tfex/PlayerPatch.cpp has them), VersusPlayerMatchResultsPatch except its ctor (the end-of-match portrait's scope only).

**What the Switch's runtime lacks, and what stands in (our own code, `tfex/runtime/tfexgen_skins.*`, `translator/glue/Skins.cs`):**
- MessagePack-CSharp: the bundle's formatters, written by hand in the glue from TF.EX's `[MessagePackObject]` keys (each object is an
  array of its keys in order). The writer and reader are in the runtime. The bundle id (SHA-256 of those bytes) matches the PC's.
- `SHA256.HashData`, `Convert.ToHexString`, raw DEFLATE (Rust's miniz_oxide, `ggrs-switch` `tfx_deflate_*` / new `tfx_inflate`).
- `Convert.FromBase64String`: the game's Mono has no decoding icall (InternalFromBase64String +0x6cb0 always throws). It is rewritten
  to the runtime's decoder everywhere, which also fixes TokenService's use.
- Textures. `GetData<Color>(level, rect, data, start, count)` has no Switch instantiation, so it calls the pair the game's
  `GetData<Color>(Color[])` uses: +0xb8bf0 resolves the `Rectangle?`, +0x9106e0 reads that rectangle into TF.EX's array.
  `SetData<byte>` (RGBA) calls the game's `SetData<Color>` (+0xb8bf0 / +0xb9120). `MemberwiseClone` and the Texture2D ctor are pinned.
- Large lobby messages (a chunk is up to 192 KB of base64). The lobby's inbound and outbound buffers (MatchmakingService.cpp:
  2 x 280 KB pools for messages past the inline size), the WebSocket frame writer (64-bit lengths, net.cpp), SendJson and
  JsonBytes (280-300 KB). A model's big string field is `MStr`, a managed string in a GC root slot (the model rings hold 8 per type).
- Hair (`ExtraHairData`) is absent, as in section 6.

**Found on the way:**
- A whole-atlas readback (16 MB of `Color[]` for one subtexture on the game's 2048x2048 atlas) grew the heap past the rollback page
  table (28000 pages). The rollback was then inexact and the guest crashed after the first rollback. The rect read above fixed it:
  the heap is the same 95.6 MB as without skins.
- The vanilla hat on a streamed crewmate is TF.EX's behaviour. Its bundle has no StartNoHat, and this mod has no hat images, so
  the fallback archer's hat stays.
- On the page, the streamed archer is a clone of its fallback vanilla archer. The page can't ready on that same archer (the game's
  roll call), so the harness picks another one (`LVB_PAGEPICK=ArrowRight` with the Switch on the mod archer).

**Emulator checks (hidden, slot update, against the browser page, lobby scenario):**
- Page with the mod, Switch without: PASS, 0 of 592 ticks differ. The Switch builds the streamed archer ("Skinned archer built for
  <bundle>|1|0"), and the mid-match shot shows the page's player as the red crewmate.
- Switch with the mod (`lobbyarcher 10`), page without: PASS, 0 of 596 ticks differ. The page's mid-match shot (smoke.sh now also
  takes the page's screenshot at the shot line: `smoke-<scenario>-page.png`) shows the Switch's player as the red crewmate.
- Control (Switch with the mod on a vanilla archer): PASS, 0 of 593.

**Console check (next hardware session; mod folder on the SD card as in section 6):**
- **Out:** lobby against a browser page without the mod, Switch on the red crewmate. The log shows `[tfex] Skin bundle
  AmongUsRedBlue/AmongUsRedBlue sent (<n> bytes, 1 chunks)` and `[tfex] Skinned archer built for local|AmongUsRedBlue/...`. The page
  shows the crewmate in the roll call and the match. The match runs in sync, and there is no `[rb] ERROR: the page table is full`.
- **In:** remove the mod folder from the SD card. The page with the mod plays the crewmate. The log shows `[tfex] Skin bundle
  received for AmongUsRedBlue/AmongUsRedBlue (<n> bytes)` and `Skinned archer built for <bundle id>|<i>|<alt>`. The Switch draws the
  crewmate (with the fallback's hat).
