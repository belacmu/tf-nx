// FortRise content mods on the Switch: OUR OWN CODE (not FortRise's), the framework FortRise's translated loaders and ModArchers run on.
// Compiled with FortRise's source (vendor/fortrise, MIT) and translated like it; translate.toml / overrides.toml map FortRise's
// interfaces here. TFEX-DIFF S-native: the Switch has no FortRise mod loader (no .NET runtime: code mods can't load). Content mods
// are folders on the SD card (sd:/xplay/mods/<Mod>/, a mod's zip extracted unchanged: meta.json, Content/...), loaded once at boot
// after the game's content: FortRise's registry queues invoke at once (its LoadState.Ready path), and only the archer path's
// registries exist (subtextures, sprites, archers; musics and character sounds are empty). Each piece below follows the FortRise file
// named on it; they can be replaced by FortRise's own code once the translator takes it (generic classes, ref locals).
using System;
using System.Collections.Generic;
using System.IO;
using System.Xml;
using FortRise;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Audio;
using Microsoft.Xna.Framework.Graphics;
using Monocle;
using MonoMod.Utils;
using TowerFall;

namespace TfexGlue.FortRiseSwitch
{
    // ---- a mod's files: FortRise's IResourceInfo (Core/ModLoader/Resource/ResourceInfo.cs, FileResourceInfo.cs) over the SD card
    public class SdResource
    {
        public string Root;   // the mod's folder, "sd:/xplay/mods/<Mod>/"
        public string Path;   // relative to it ("" = the mod itself)

        public SdResource(string root, string path) { Root = root; Path = path; }

        // one object per file, as FortRise's loader has one IResourceInfo per file (its ModSubtextures.RegisterTexture compares them by
        // reference: a second object for the same file read as "a different path")
        static readonly Dictionary<string, SdResource> made = new Dictionary<string, SdResource>();
        public static SdResource At(string root, string path)
        {
            var key = root + path;
            if (made.TryGetValue(key, out var r)) return r;
            r = new SdResource(root, path);
            made[key] = r;
            return r;
        }

        public string RootPath => Root + Path;

        public XmlDocument Xml
        {
            get
            {
                // TFEX-DIFF S-native: the text read from the SD card here, then XmlDocument.LoadXml. The game's FileStream can't open
                // sd:/ (Mono's Path.GetFullPath makes it "/sd:/...", and the runtime's path mapper only undoes that for rom:/ and save:/)
                var text = SwitchMods.ReadText(RootPath);
                if (text == null) return null;
                var doc = new XmlDocument();
                doc.LoadXml(text);
                return doc;
            }
        }

        public Stream Stream
        {
            get
            {
                // the file's bytes in a MemoryStream (TFEX-DIFF S-native: not a FileStream, as Xml above)
                int size = SwitchMods.FileSize(RootPath);
                if (size < 0) return null;
                var bytes = new byte[size];
                SwitchMods.ReadInto(RootPath, bytes);
                return SwitchMods.ManagedStream(bytes);
            }
        }

        public string Text
        {
            get
            {
                return SwitchMods.ReadText(RootPath);
            }
        }

        static string Combine(string a, string b) => a.Length == 0 ? b : a.EndsWith("/") ? a + b : a + "/" + b;

        public SdResource GetRelativePath(string path) => At(Root, Combine(Path, path));

        public bool ExistsRelativePath(string path) => SwitchMods.FileExists(Root + Combine(Path, path));

        public bool TryGetRelativePath(string path, out SdResource resource)
        {
            var full = Combine(Path, path);
            if (SwitchMods.FileExists(Root + full)) { resource = At(Root, full); return true; }
            resource = null;
            return false;
        }

        // FortRise: the resources under this one matching a path pattern ("Content/Atlas/atlas.xml", "Content/Music/*.ogg"): here the
        // exact file when there is no wildcard, else the folder's files whose names match (one '*' in the file name)
        public List<SdResource> EnumerateChildrens(string pattern)
        {
            var list = new List<SdResource>();
            var full = Combine(Path, pattern);
            int star = full.IndexOf('*');
            if (star < 0)
            {
                if (SwitchMods.FileExists(Root + full)) list.Add(At(Root, full));
                return list;
            }
            int slash = full.LastIndexOf('/');
            var dir = slash < 0 ? "" : full.Substring(0, slash + 1);
            var namePattern = full.Substring(slash + 1);
            int s = namePattern.IndexOf('*');
            var prefix = namePattern.Substring(0, s);
            var suffix = namePattern.Substring(s + 1);
            int n = SwitchMods.ListFiles(Root + dir);
            for (int i = 0; i < n; i++)
            {
                var file = SwitchMods.ListedFile(i);
                if (file.StartsWith(prefix) && file.EndsWith(suffix)) list.Add(At(Root, dir + file));
            }
            return list;
        }
    }

    // ---- a loaded mod: FortRise's IModContent (Core/Module/ModContent.cs) and the name its ModuleMetadata carries
    public class SwitchModContent
    {
        public string Name;
        public SdResource Root;
        // FortRise's ModuleMetadata as TF.EX reads it through IModInterop.LoadedMods (ModCollections): the registry's entry
        // (runtime/tfexgen_mods.cpp: the meta.json); a dependency is a SwitchModContent with its Name and Version only
        public string Version;
        public string DisplayName;
        public string Dll;
        public SwitchModContent[] Dependencies;
        public SwitchModContent[] OptionalDependencies;
        public string PathDirectory;   // ModuleMetadata.PathDirectory: the mod's folder, "sd:/xplay/mods/<folder>" (null built in)
        public SwitchModContent(string name, string folder) { Name = name; Root = SdResource.At(folder, ""); }
    }

    // ---- FortRise's IModRegistry for one mod (Core/Registry/ModRegistry.cs): the archer path's registries (subtextures, sprites: this
    // file's functions on the mod; archers: FortRise's own ModArchers, translated)
    public class SwitchModRegistry
    {
        public SwitchModContent Mod;
        public IModSubtextures Subtextures;   // FortRise's ModSubtextures for this mod (translated, Fr_ModSubtextures)
        public IModSprites Sprites;           // FortRise's ModSprites for this mod (translated, Fr_ModSprites)
        public object Archers;
        public object GameModes;   // FortRise's ModGameModes for this mod, made at first use (SwitchMods.RegistryGameModes)
        public object SFXs, CharacterSounds, Musics;   // FortRise's ModSFXs / ModCharacterSounds / ModMusics for this mod, made at first use
        public SwitchModRegistry(SwitchModContent mod) { Mod = mod; Subtextures = SwitchMods.NewModSubtextures(mod); Sprites = SwitchMods.NewModSprites(mod); Archers = SwitchMods.NewModArchers(mod); }
    }

    // ---- a custom variant: FortRise's VariantEntry (Core/Registry/Variants/VariantEntry.cs): its name ("<Mod>/<id>") and configuration
    public class SwitchVariantEntry
    {
        public string Name;
        public VariantConfiguration Configuration;
        public SwitchVariantEntry(string name, VariantConfiguration configuration) { Name = name; Configuration = configuration; }
    }

    // ---- what FortRise's patch_MatchVariants adds to a MatchVariants (InternalCustomVariants, Customs, StartWithVariants): the game's
    // object has no room for them, so they are kept here by the object (glue SwitchMods.MatchVariantsExt)
    public class SwitchMatchVariantsExt
    {
        public Dictionary<string, Variant> CustomVariants = new Dictionary<string, Variant>();
        public HashSet<Variant> Customs = new HashSet<Variant>();
        public List<(Variant, ArrowTypes)> StartWithVariants = new List<(Variant, ArrowTypes)>();
    }

    // ---- an immediate registry queue: FortRise's RegistryQueue<T> (Core/ModLoader/RegistryQueue.cs) once LoadState is Ready
    public class SwitchQueue
    {
        public Action<object> Invoker;
        public List<object> Pending;   // a queue held until the game's sounds are loaded (ModCharacterSounds': SwitchMods.LoadSounds)
    }

    public static class SwitchMods
    {
        // ---- the SD card (runtime/tfexgen_frmods.cpp)
        [Cpp("tfex::gen::frmods::ReadText($0)", Include = "tfexgen_frmods.hpp")] public static extern string ReadText(string path);
        [Cpp("tfex::gen::frmods::ModVersion($0)", Include = "tfexgen_frmods.hpp")] public static extern string ModVersion(string name);   // a mod's meta.json version
        [Cpp("tfex::gen::frmods::FileExists($0)", Include = "tfexgen_frmods.hpp")] public static extern bool FileExists(string path);
        [Cpp("tfex::gen::frmods::FileSize($0)", Include = "tfexgen_frmods.hpp")] public static extern int FileSize(string path);
        [Cpp("tfex::gen::frmods::ReadInto($0, $1)", Include = "tfexgen_frmods.hpp")] public static extern void ReadInto(string path, byte[] into);
        // the game's own MemoryStream(byte[]) (TF.EX's MemoryStream rule stands in a native buffer, which FNA's Texture2D.FromStream can't read)
        [Cpp("tfex::gen::frmods::ManagedStream($0)", Include = "tfexgen_frmods.hpp")] public static extern Stream ManagedStream(byte[] bytes);
        [Cpp("tfex::gen::frmods::ListFiles($0)", Include = "tfexgen_frmods.hpp")] public static extern int ListFiles(string folder);
        [Cpp("tfex::gen::frmods::ListedFile($0)", Include = "tfexgen_frmods.hpp")] public static extern string ListedFile(int i);
        [Cpp("tfex::gen::frmods::ModCount()", Include = "tfexgen_frmods.hpp")] static extern int ModCount();
        [Cpp("tfex::gen::frmods::ModFolder($0)", Include = "tfexgen_frmods.hpp")] static extern string ModFolder(int i);
        [Cpp("tfex::gen::frmods::ModName($0)", Include = "tfexgen_frmods.hpp")] static extern string ModName(int i);
        // the registry (runtime/tfexgen_mods.cpp): every mod, built-in or on the SD card, with its meta.json's fields
        [Cpp("tfex::gen::frmods::RegCount()", Include = "tfexgen_frmods.hpp")] static extern int RegCount();
        [Cpp("tfex::gen::frmods::RegLoaded($0)", Include = "tfexgen_frmods.hpp")] static extern bool RegLoaded(int i);
        [Cpp("tfex::gen::frmods::RegText($0, $1)", Include = "tfexgen_frmods.hpp")] static extern string RegText(int i, int field);
        [Cpp("tfex::gen::frmods::RegDepCount($0, $1)", Include = "tfexgen_frmods.hpp")] static extern int RegDepCount(int i, bool optional);
        [Cpp("tfex::gen::frmods::RegDepText($0, $1, $2, $3)", Include = "tfexgen_frmods.hpp")] static extern string RegDepText(int i, int k, bool optional, int field);
        [Cpp("tfex::gen::frmods::Log($0)", Include = "tfexgen_frmods.hpp")] public static extern void Log(string line);
        // FortRise's patch_ArcherData() (MonoMod's added empty constructor): an ArcherData with nothing parsed
        [Cpp("tfex::gen::frmods::NewArcherData()", Include = "tfexgen_frmods.hpp")] public static extern ArcherData NewArcherData();
        // FortRise's patch_CharacterSounds(): a CharacterSounds with nothing loaded (the game's object: it goes into Sounds.Characters)
        [Cpp("gen::Fn<void* (*)(void*)>(game::brute::NewObject)(gen::TypeOf(bind::t_CharacterSounds))")] public static extern CharacterSounds NewCharacterSounds();
        // FortRise's ModArchers, translated (Fr_ModArchers): made with this mod's content object as its "metadata" (Name: a rule)
        [Cpp("tfex::gen::Fr_ModArchers::New($0, nullptr)", Include = "tfexgen_Fr_ModArchers.hpp")] public static extern object NewModArchers(object metadata);
        // FortRise's ModSubtextures, translated (Fr_ModSubtextures): this mod's content object as its "metadata", no manager (its queue is
        // CreateQueue's: invoked at once), the Switch's log as its logger
        [Cpp("tfex::gen::Fr_ModSubtextures::New($0, nullptr, $1)", Include = "tfexgen_Fr_ModSubtextures.hpp")] static extern IModSubtextures NewModSubtextures(object metadata, object logger);
        public static IModSubtextures NewModSubtextures(object metadata) => NewModSubtextures(metadata, SwitchLogger());
        [Cpp("tfex::gen::svc::Service()", Include = "tfexgen_services.hpp")] static extern object SwitchLogger();   // FortRise's ILogger: the Switch's log
        public static IModSubtextures RegistrySubtextures(SwitchModRegistry r) => r.Subtextures;
        // FortRise's ModSprites, translated (Fr_ModSprites): this mod's content object as its "metadata", no manager (CreateQueue's queue)
        [Cpp("tfex::gen::Fr_ModSprites::New($0, nullptr)", Include = "tfexgen_Fr_ModSprites.hpp")] public static extern IModSprites NewModSprites(object metadata);
        public static IModSprites RegistrySprites(SwitchModRegistry r) => r.Sprites;
        // FortRise's ModGameModes, translated (Fr_ModGameModes): this mod's content object as its "metadata" (Name: a rule), no manager
        // (its registry queue is CreateQueue's: registered at once)
        [Cpp("tfex::gen::Fr_ModGameModes::New($0, nullptr)", Include = "tfexgen_Fr_ModGameModes.hpp")] static extern object NewModGameModes(object metadata);
        public static object RegistryGameModes(SwitchModRegistry registry) => registry.GameModes ??= NewModGameModes(registry.Mod);
        // FortRise's EnumPool.Obtain<Modes>(): from 11, as on the PC (FortRise's patch_Modes.Custom = 10 is the enum's highest)
        [Cpp("tfex::gen::frmods::ObtainMode()", Include = "tfexgen_frmods.hpp")] public static extern Modes ObtainMode();

        // FortRise.Content's loaders, translated (units Fr_*): the registry and the content are this file's objects (FortRise's
        // interfaces on them are overrides.toml rules), the Loader null (FortRise's default paths)
        [Cpp("tfex::gen::Fr_SubtextureLoader::Load($0, $1, $2, nullptr)", Include = "tfexgen_Fr_SubtextureLoader.hpp")] static extern void SubtextureLoad(object registry, object content, int destination);
        [Cpp("tfex::gen::Fr_MusicLoader::Load($0, $1, nullptr)", Include = "tfexgen_Fr_MusicLoader.hpp")] static extern void MusicLoad(object registry, object content);
        [Cpp("tfex::gen::Fr_SpriteDataLoader::LoadSpriteData_1($0, $1, $2, nullptr)", Include = "tfexgen_Fr_SpriteDataLoader.hpp")] static extern void SpriteDataLoad(object registry, object content, int type);
        [Cpp("tfex::gen::Fr_ArcherLoader::Load($0, $1, nullptr)", Include = "tfexgen_Fr_ArcherLoader.hpp")] static extern void ArcherLoad(object registry, object content);

        public static SwitchModContent Current;   // ContentModule.CurrentModMetadata: the mod being loaded
        // TF.EX's own FortRise registry (its module's IModRegistry, ModRegistryApi.Current): what its skin overlay registers streamed
        // skins' textures and sprites through (FortRise names them "TF.EX/<id>")
        static SwitchModRegistry tfexRegistry;
        // FortRise's patch_VersusModeButton.modeSelect (a field it adds to the GAME MODE button: the modes it scrolls through). The
        // Switch's objects carry no added fields; one GAME MODE button exists at a time (VERSUS's), so its list lives here (a GC root)
        public static List<Modes> VersusModeSelect { get; set; }
        // the code mods the Switch carries translated (Teams+: translate.toml's "Teams+" units), by their folder in sd:/xplay/mods: the
        // registry LoadAll made for that folder, which is also the mod's IModuleContext (overrides.toml IModuleContext.Registry), as FortRise
        // hands a code mod its own context. Null when the folder isn't on the card: the mod then isn't constructed (FrModsBoot.cpp)
        static readonly Dictionary<string, SwitchModRegistry> modRegistries = new();
        public static SwitchModRegistry ModRegistry(string folder) => modRegistries.TryGetValue(folder, out var r) ? r : null;
        // IModuleContext.Registry: a code mod's context is its registry; TF.EX's is the services' token or none (its own registry). By
        // identity, not a type test: the token isn't a managed object (a cast read its header and crashed RightStickShot, 2026-10-31)
        public static SwitchModRegistry ContextRegistry(object context)
        {
            foreach (var r in modRegistries.Values) if (ReferenceEquals(r, context)) return r;
            return TfexRegistry();
        }
        public static SwitchModRegistry TfexRegistry() => tfexRegistry ??= new SwitchModRegistry(TfexContent());
        // TF.EX's module content: its registry entry (the built-in meta.json: version, dependencies) over its folder on the SD card,
        // sd:/xplay/tfex/, laid out as the mod ships (CHANGELOG.md, imgs/netplay.png, imgs/tutorial/*.png: tools/devserve.list)
        static SwitchModContent TfexContent()
        {
            var c = new SwitchModContent("TF.EX", "sd:/xplay/tfex/");
            foreach (var m in LoadedMods())
            {
                if (m.Name != c.Name) continue;
                c.Version = m.Version; c.DisplayName = m.DisplayName; c.Dll = m.Dll;
                c.Dependencies = m.Dependencies; c.OptionalDependencies = m.OptionalDependencies;
            }
            return c;
        }

        // ---- TF.EX's module object (FortRise's Mod, TFEXModModule; its unit is hosted: no object): what it keeps between its constructor
        // and OnInitialize. FortRise's ModuleManager invokes OnInitialize once the mods are loaded and their settings read; the Switch's
        // boot calls Initialize after TF.EX's settings exist (tfex/MatchmakingService.cpp TfexModuleBoot)
        // FortRise's ModuleManager.Initialize: every loaded mod's OnInitialize, in load order, with that mod's context (a code mod's: its
        // registry; TF.EX's: none, its own). Mods set it in their constructors (overrides.toml Mod.OnInitialize), booted in FortRise's order
        static readonly List<object> initMods = new();
        static readonly List<object> initActions = new();   // (List<object>: the game's AOT build has no List<Action<T>>)
        static object tutorialSlides;
        public static void SetOnInitialize(object mod, Action<IModuleContext> a)
        {
            for (int i = 0; i < initMods.Count; i++) if (ReferenceEquals(initMods[i], mod)) { initActions[i] = a; return; }
            initMods.Add(mod);
            initActions.Add(a);
        }
        public static void Initialize()
        {
            int n = initMods.Count;
            for (int i = 0; i < n; i++)
            {
                var a = AsInit(initActions[i]);
                if (a != null) a(ModContext(initMods[i]));
            }
            Log($"[frmods] OnInitialize: {n} mod(s) in load order (FortRise's ModuleManager.Initialize)");
            initMods.Clear();
            initActions.Clear();
        }
        // a mod's IModuleContext: its registry (FortRise hands a code mod its own context), none for TF.EX's hosted module
        static IModuleContext ModContext(object mod)
        {
            foreach (var r in modRegistries.Values) if (ReferenceEquals(r.Mod, mod)) return AsContext(r);
            return null;
        }
        // the registry object is the mod's IModuleContext on the Switch (its members are overrides.toml rules over it, by identity)
        [Cpp("$0")] static extern IModuleContext AsContext(SwitchModRegistry registry);
        [Cpp("$0")] static extern Action<IModuleContext> AsInit(object action);   // what SetOnInitialize stored
        public static object GetTutorialSlides() => tutorialSlides;

        // ---- custom variants: FortRise's ModVariants.RegisterVariant + VariantRegistry (Core/Registry/Variants/ModVariants.cs,
        // VariantRegistry.cs): the name is "<Mod>/<id>", the header the mod's display name when the configuration has none
        public static readonly Dictionary<string, SwitchVariantEntry> Variants = new Dictionary<string, SwitchVariantEntry>();
        public static SwitchVariantEntry RegisterVariant(SwitchModContent mod, string id, VariantConfiguration configuration)
        {
            var name = mod.Name + "/" + id;
            var header = configuration.Header ?? (string.IsNullOrEmpty(mod.DisplayName) ? mod.Name : mod.DisplayName);
            var entry = new SwitchVariantEntry(name, new VariantConfiguration
            {
                Title = configuration.Title, Icon = configuration.Icon, Header = header, Description = configuration.Description,
                Flags = configuration.Flags, Exclusions = configuration.Exclusions, Links = configuration.Links, StartWith = configuration.StartWith,
            });
            Variants.Add(name, entry);
            Log($"[frmods] variant {name} registered ({configuration.Title}, flags {(int)configuration.Flags})");
            return entry;
        }
        public static VariantConfiguration VariantConfig(SwitchVariantEntry e) => e.Configuration;

        // the side table: DynamicData on the MatchVariants (a few made per match), so an entry goes with its object as a MonoMod field
        // would (was a map holding every MatchVariants alive: full at 64, ~20 matches, then no custom variants); counted in the log
        static int extsMade;
        public static SwitchMatchVariantsExt Ext(MatchVariants v)
        {
            if (DynamicData.For(v).TryGet<SwitchMatchVariantsExt>("fortrise_variants", out var e)) return e;
            e = new SwitchMatchVariantsExt();
            if (e == null || e.CustomVariants == null) { Log("[frmods] MatchVariants' FortRise fields: the side table's object or map wasn't made (no custom variants)"); return null; }
            DynamicData.For(v).Set("fortrise_variants", e);
            int n = ++extsMade;
            if (n <= 3 || n % 100 == 0) Log($"[frmods] MatchVariants with FortRise's custom variants: {n} made ({Variants.Count} registered)");
            return e;
        }
        public static Dictionary<string, Variant> CustomVariantsOf(MatchVariants v) => Ext(v).CustomVariants;
        // FortRise's VariantEntry.IsActive overloads (Core/Registry/Variants/VariantEntry.cs), over the Switch's entry: the variant by its name
        // in the match variants' custom ones (the current match settings' when none are given)
        public static bool VariantIsActiveIn(SwitchVariantEntry e, MatchVariants matchVariants)
        {
            if (matchVariants != null && CustomVariantsOf(matchVariants).TryGetValue(e.Name, out Variant variant)) return variant;
            return false;
        }
        public static bool VariantIsActiveFor(SwitchVariantEntry e, MatchVariants matchVariants, int playerIndex)
        {
            if (matchVariants != null && CustomVariantsOf(matchVariants).TryGetValue(e.Name, out Variant variant)) return variant[playerIndex];
            return false;
        }
        // a custom variant on or off by its registered name ("<mod>/<id>") in a MatchVariants (the local-play test driver: hw.txt localvariant)
        public static bool SetCustomVariant(MatchVariants v, string name, bool on)
        {
            if (v == null || !CustomVariantsOf(v).TryGetValue(name, out Variant variant)) return false;
            variant.Value = on;
            return true;
        }
        public static bool VariantIsActive(SwitchVariantEntry e) => MainMenu.CurrentMatchSettings != null && VariantIsActiveIn(e, MainMenu.CurrentMatchSettings.Variants);
        public static bool VariantIsActivePlayer(SwitchVariantEntry e, int playerIndex) => MainMenu.CurrentMatchSettings != null && VariantIsActiveFor(e, MainMenu.CurrentMatchSettings.Variants, playerIndex);
        public static HashSet<Variant> CustomsOf(MatchVariants v) => Ext(v).Customs;
        public static List<(Variant, ArrowTypes)> StartWithOf(MatchVariants v) => Ext(v).StartWithVariants;
        // patch_MatchVariants.StoreVanillaVariantsAsCustom: its reflection over the game's Variant fields, generated (glue GameFields)
        public static void StoreVanillaVariantsAsCustom(MatchVariants v)
        {
            var e = Ext(v);
            if (e != null) GameFieldsFr.VariantFields_MatchVariants(v, e.CustomVariants);
        }
        public static string VariantName(SwitchVariantEntry e) => e.Name;
        public static SwitchVariantEntry GetVariant(SwitchModContent mod, string id) => Variants.TryGetValue(mod.Name + "/" + id, out var e) ? e : null;
        public static Dictionary<string, SwitchVariantEntry> RegisteredVariants() => Variants;
        public static void SetTutorialSlides(object slides) => tutorialSlides = slides;
        public static string TfexModName() => "TF.EX";

        // TF.EX's ArcherDataExtensions.ResolveCustomArcher: RegisteredArchers.TryGetValue (FortRise's ArcherRegistry.ArcherEntries)
        public static bool RegisteredTryGet(Dictionary<string, IArcherEntry> registered, string id, out IArcherEntry entry) => registered.TryGetValue(id, out entry);

        // ---- the boot driver: FortRise's ContentModule.OnBeforeModInstantiation (InternalMods/FortRise.Content/ContentModule.cs) for
        // each mod on the SD card, with the default loaders' paths (a content.json isn't read yet), the archer path's loaders only
        // FortRise's IModInterop.LoadedMods (Core/Module/ModInterop.cs: the ModuleManager's mods): the registry's loaded mods, built-in
        // ones first, each with its metadata; made once
        static SwitchModContent[] loaded;
        public static SwitchModContent[] LoadedMods()
        {
            if (loaded != null) return loaded;
            var list = new List<SwitchModContent>();
            for (int i = 0; i < RegCount(); i++)
            {
                if (RegLoaded(i)) list.Add(FromRegistry(i));
            }
            loaded = list.ToArray();
            return loaded;
        }
        // registry entry i as FortRise's ModuleMetadata
        static SwitchModContent FromRegistry(int i)
        {
            var folder = RegText(i, 6);
            var m = new SwitchModContent(RegText(i, 0), string.IsNullOrEmpty(folder) ? "" : "sd:/xplay/mods/" + folder + "/");
            m.Version = RegText(i, 2);
            m.DisplayName = RegText(i, 1);
            m.Dll = RegText(i, 5);
            m.Dependencies = Deps(i, false);
            m.OptionalDependencies = Deps(i, true);
            m.PathDirectory = string.IsNullOrEmpty(folder) ? null : "sd:/xplay/mods/" + folder;
            return m;
        }

        // ---- FortRise's TOGGLE MODS (UIModToggler, translated: Fr_UIModToggler) over the registry. TFEX-DIFF S-native: FortRise's Mods
        // folder is sd:/xplay/mods (folders only: FortRise's zip mods aren't read on the Switch), its blacklist.txt the same file there
        // (runtime/tfexgen_mods.cpp reads it at start: a folder in it is listed, not loaded), and the restart FortRise does itself
        // (RiseCore.InternalRestart) the player's: a notice, as TF.NX's updater (the Switch has no game restart of its own here)
        [Cpp("tfex::gen::frmods::BlackCount()", Include = "tfexgen_frmods.hpp")] static extern int BlackCount();
        [Cpp("tfex::gen::frmods::BlackAt($0)", Include = "tfexgen_frmods.hpp")] static extern string BlackAt(int i);
        [Cpp("tfex::gen::frmods::BlackClear()", Include = "tfexgen_frmods.hpp")] static extern void BlackClear();
        [Cpp("tfex::gen::frmods::BlackAdd($0)", Include = "tfexgen_frmods.hpp")] static extern void BlackAdd(string folder);
        [Cpp("tfex::gen::frmods::BlackSave()", Include = "tfexgen_frmods.hpp")] static extern bool BlackSave();
        // ModuleManager.BlacklistedMods: blacklist.txt's folders as read at start
        public static HashSet<string> BlacklistedMods()
        {
            var set = new HashSet<string>();
            for (int i = 0; i < BlackCount(); i++) set.Add(BlackAt(i));
            return set;
        }
        // AppDomain.CurrentDomain.BaseDirectory (the folder holding FortRise's Mods): the Switch's sd:/xplay
        public static string GameFolder() => "sd:/xplay";
        // Directory.GetFiles(Mods): FortRise looks for its zip mods there; none on the Switch
        public static string[] ModsFolderFiles(string dir) => new string[0];
        // Directory.GetDirectories(Mods): the content mods' folders the registry found (each with a meta.json), turned off or not
        public static string[] ModsFolderDirectories(string dir)
        {
            var list = new List<string>();
            for (int i = 0; i < RegCount(); i++)
            {
                var folder = RegText(i, 6);
                if (!string.IsNullOrEmpty(folder)) list.Add("sd:/xplay/mods/" + folder);
            }
            return list.ToArray();
        }
        // ModuleMetadata.ParseMetadata(dir, metaPath): the registry's entry for that folder (it read the meta.json at start), or null
        public static SwitchModContent FolderMeta(string dir)
        {
            var name = Path.GetFileName(dir);
            for (int i = 0; i < RegCount(); i++)
                if (RegText(i, 6) == name) return FromRegistry(i);
            return null;
        }
        public static string ContentPathDirectory(SwitchModContent c) => c.PathDirectory;
        // RiseCore.WriteBlacklist (its path: sd:/xplay/mods/blacklist.txt)
        public static void WriteBlacklist(List<string> folders)
        {
            BlackClear();
            foreach (var f in folders) BlackAdd(f);
            BlackSave();
        }
        [Cpp("tfex::Notification::Create($0, \"RESTART THE GAME TO APPLY THE MOD CHANGES\", 20, 600)", Include = "../Notification.hpp")] static extern void RestartNotice(Scene scene);   // TF.EX's toast (10 s)
        // RiseCore.InternalRestart: the player restarts the game (the changed mods load then)
        public static void RestartForMods()
        {
            Log("[mods] TOGGLE MODS changed which mods load: the game must be restarted (the notice says so)");
            RestartNotice(Engine.Instance.Scene);
        }

        static SwitchModContent[] Deps(int i, bool optional)
        {
            var d = new SwitchModContent[RegDepCount(i, optional)];
            for (int k = 0; k < d.Length; k++) { d[k] = new SwitchModContent(RegDepText(i, k, optional, 0), ""); d[k].Version = RegDepText(i, k, optional, 1); }
            return d;
        }
        public static string ContentVersion(SwitchModContent c) => c.Version;
        public static string ContentDisplayName(SwitchModContent c) => c.DisplayName;
        public static string ContentToString(SwitchModContent c) => c.Name + " " + c.Version;   // FortRise's ModuleMetadata.ToString
        public static bool ContentEquals(SwitchModContent a, SwitchModContent b) => a == null ? b == null : b != null && a.Name == b.Name && a.Version == b.Version;
        public static string ContentDll(SwitchModContent c) => c.Dll;
        public static SwitchModContent[] ContentDependencies(SwitchModContent c) => c.Dependencies;
        public static SwitchModContent[] ContentOptionalDependencies(SwitchModContent c) => c.OptionalDependencies;

        // ---- what FortRise's MODS menu (UIModMenu, UIModPanel: translated) reads from ModuleManager, RiseCore and patch_MainMenu
        // ModuleManager.InternalModuleMetadatas (a HashSet) and InternalFortModules (a List): the loaded mods, in FortRise's collections
        static HashSet<SwitchModContent> loadedSet;
        public static HashSet<SwitchModContent> LoadedModSet()
        {
            if (loadedSet == null) { loadedSet = new HashSet<SwitchModContent>(); foreach (var m in LoadedMods()) loadedSet.Add(m); }
            return loadedSet;
        }
        static List<SwitchModContent> loadedList;
        public static List<SwitchModContent> LoadedModList()
        {
            if (loadedList == null) { loadedList = new List<SwitchModContent>(); foreach (var m in LoadedMods()) loadedList.Add(m); }
            return loadedList;
        }
        // Mod.CreateSettings: a mod with a settings page. TF.EX's NetplaySettings is the only ModuleSettings on the Switch (content mods
        // have no code); null for the others, as FortRise's for a mod without settings
        public static object ContentSettings(SwitchModContent c) => c.Name == TfexModName() ? c : null;
        // ModuleManager.NameToIcon: each loaded mod's icon.png, as FortRise's patch_TFGame.LoadContent registers them (a mod without one:
        // the panel draws FortRise's default, variants/noArrows). Made at the first read (the MODS menu), when the graphics device is up as
        // at LoadContent; TF.EX's own from its folder sd:/xplay/tfex/ (TfexContent), the SD mods' from theirs
        static Dictionary<string, Subtexture> icons;
        public static Dictionary<string, Subtexture> ModIcons()
        {
            if (icons != null) return icons;
            icons = new Dictionary<string, Subtexture>();
            foreach (var m in LoadedMods())
            {
                var root = m.Name == TfexModName() ? "sd:/xplay/tfex/" : m.Root.Root;
                if (string.IsNullOrEmpty(root)) continue;
                var stream = new SdResource(root, "icon.png").Stream;
                if (stream == null) continue;
                var tex2D = Texture2D.FromStream(Engine.Instance.GraphicsDevice, stream);
                icons[m.Name] = new Subtexture(new Monocle.Texture(tex2D));
                Log($"[frmods] {m.Name}: icon.png loaded ({tex2D.Width}x{tex2D.Height}) for the MODS menu");
            }
            return icons;
        }
        // RiseCore.UpdateChecks.HasUpdates: FortRise's updater (GitHub / GameBanana) doesn't run on the Switch: always empty
        static readonly HashSet<SwitchModContent> hasUpdates = new HashSet<SwitchModContent>();
        public static HashSet<SwitchModContent> HasUpdates() => hasUpdates;
        // patch_MainMenu.FilterMod: FortRise's field on MainMenu (the mod whose settings OPTIONS shows); here a static, one main menu at a time
        static SwitchModContent filterMod;
        public static SwitchModContent GetFilterMod() => filterMod;
        public static void SetFilterMod(SwitchModContent m) { filterMod = m; }
        // patch_MainMenu's [MonoModPublic] accessors: the game's own members
        public static void SetToStartSelected(MainMenu menu, MenuItem item) { menu.ToStartSelected = item; }
        public static void TweenBGCameraToY(MainMenu menu, int y) { menu.TweenBGCameraToY(y); }
        // TextUtils.DrawOutlinedIconText (ReadOnlySpan<char> parsing): its body for text without <t=...> icon tags, the only text the
        // MODS menu passes on the Switch (the tag marks a mod with an update: none here)
        public static void DrawOutlinedIconText(SpriteFont font, string text, Vector2 position, Color color, Vector2 justify)
        {
            Draw.OutlineTextJustify(font, text, position, color, Color.Black, justify);
        }
        // FortRise's patch_MainMenu.InitModOptions: each loaded mod with settings (FilterMod: only that one) gets its name as a header, then
        // its entries. TF.EX's are NetplaySettings.Create's (tfex/NetplayOptions.cpp: TF.EX's compilation).
        // TFEX-DIFF S-update: on the full OPTIONS screen a TF.NX section comes first (the user, 2026-11-08): TF.NX's version and update
        // entry, and TF.EX's own AUTO UPDATE moved there from TF.EX's list (on the Switch TF.EX's updates arrive in TF.NX's releases, so the
        // switch decides TF.NX's downloads; it is still TF.EX's setting, NetplayPreferences.AutoUpdate). MODS > TF.EX shows TF.EX's list as is.
        public static void InitModOptions(List<OptionsButton> buttons)
        {
            List<OptionsButton> tfex = null;
            if (filterMod == null)
            {
                tfex = new List<OptionsButton>();
                UpdatesBeginOptions();
                CreateTfexEntries(tfex);
                buttons.Add(new OptionsButtonHeader("TF.NX"));
                UpdatesCreateEntry(buttons);
                SetTfnxEntry(buttons.Count - 1);
                OptionsButton autoUpdate = UpdatesTakeAutoUpdate();
                if (autoUpdate != null) buttons.Add(autoUpdate);
            }
            foreach (var mod in LoadedMods())
            {
                if (ContentSettings(mod) == null) continue;
                if (filterMod != null && filterMod.Name != mod.Name) continue;
                buttons.Add(new OptionsButtonHeader(mod.Name.ToUpperInvariant()));   // (FortRise's DisplayName test is inverted: it shows Name)
                int first = buttons.Count;
                if (tfex == null) CreateTfexEntries(buttons);
                else for (int i = 0; i < tfex.Count; i++) if (!UpdatesIsMoved(tfex[i])) buttons.Add(tfex[i]);
                TfexPlaced(buttons, first);
            }
        }
        // OptionsButtonExt.Description (a C# 14 extension property: not translated): its getter, the DynamicData key FortRise's
        // DescribeLastEntry and TF.EX's ServerOptionsButton set
        public static string ButtonDescription(OptionsButton button) => DynamicData.For(button).TryGet<string>("fortrise_description", out var d) ? d : null;
        // SpriteFontExt.WrapText (a C# 14 extension over spans: not translated): its algorithm over strings. Words (split on spaces) are added
        // to a line while the line's width stays within maxWidth; '\n' starts a paragraph. Lines keep the text's case, as FortRise's (it
        // uppercases a copy, then slices the original)
        public static string[] WrapText(SpriteFont font, string text, float maxWidth)
        {
            var result = new List<string>();
            foreach (var paragraph in text.Split('\n'))
            {
                int currentStart = 0;
                int currentLength = 0;
                int pos = 0;
                foreach (var word in paragraph.Split(' '))
                {
                    int wordStart = pos;
                    pos += word.Length + 1;
                    if (word.Length == 0) continue;
                    if (currentLength == 0)
                    {
                        currentStart = wordStart;
                        currentLength = word.Length;
                        continue;
                    }
                    int candidateLength = wordStart + word.Length - currentStart;
                    if (font.MeasureString(paragraph.Substring(currentStart, candidateLength)).X > maxWidth)
                    {
                        result.Add(paragraph.Substring(currentStart, currentLength));
                        currentStart = wordStart;
                        currentLength = word.Length;
                    }
                    else
                    {
                        currentLength = candidateLength;
                    }
                }
                if (currentLength > 0) result.Add(paragraph.Substring(currentStart, currentLength));
            }
            return result.ToArray();
        }
        [Cpp("tfex::NetplayOptions::CreateEntries($0)", Include = "../NetplayOptions.hpp")] static extern void CreateTfexEntries(List<OptionsButton> list);
        [Cpp("tfex::NetplayOptions::Placed($0, $1)", Include = "../NetplayOptions.hpp")] static extern void TfexPlaced(List<OptionsButton> buttons, int first);
        [Cpp("tfex::NetplayOptions::SetTfnxEntry($0)", Include = "../NetplayOptions.hpp")] static extern void SetTfnxEntry(int index);
        // glue Updates (the main glue's TF.NX entry and the AUTO UPDATE it moves)
        [Cpp("tfex::gen::GlueUpdates::BeginOptions()", Include = "tfexgen_GlueUpdates.hpp")] static extern void UpdatesBeginOptions();
        [Cpp("tfex::gen::GlueUpdates::CreateEntry($0)", Include = "tfexgen_GlueUpdates.hpp")] static extern void UpdatesCreateEntry(List<OptionsButton> buttons);
        [Cpp("tfex::gen::GlueUpdates::TakeAutoUpdate()", Include = "tfexgen_GlueUpdates.hpp")] static extern OptionsButton UpdatesTakeAutoUpdate();
        [Cpp("tfex::gen::GlueUpdates::IsMoved($0)", Include = "tfexgen_GlueUpdates.hpp")] static extern bool UpdatesIsMoved(OptionsButton button);
        // [MonoModLinkTo] base calls (OptionsButtonHeader): the game's MenuItem.Update (+0xac9390) and Entity.Render (+0x92f680), not virtual
        [Cpp("gen::Fn<void (*)(void*)>(0xac9390)($0)")] public static extern void MenuItemUpdate(OptionsButton self);
        [Cpp("gen::Fn<void (*)(void*)>(0x92f680)($0)")] public static extern void EntityRender(OptionsButton self);

        // FortRise's CustomMenuStateRegistry makes a state's CustomMenuState by reflection (its constructor taking the MainMenu): the MODS one
        public static CustomMenuState NewModsMenu(MainMenu menu) => new UIModMenu(menu);

        public static void LoadAll()
        {
            // FortRise's RiseCore: IDPool.WarmIndex("archers", 8), the PC's 9 archers. TFEX-DIFF S-native: the Switch's game has 10 (its
            // archerData.xml), so mod archers start after its own (index 10), as FortRise's do after the PC's; TF.EX's CustomArcherId
            // maps them across (the page's same archer is index 9)
            int firstModArcher = ArcherData.Archers.Length;   // (the archer check below covers what the mods add)
            WarmIndex("archers", ArcherData.Archers.Length - 1);
            // RiseCore's IDPool.WarmIndex("characterSounds", 13): the PC's 14 character sounds; the Switch's own count (TFEX-DIFF, as archers)
            WarmIndex("characterSounds", VanillaCharacterSounds - 1);   // (Sounds.Characters isn't loaded yet: LoadSounds checks the count)
            int n = ModCount();
            Log($"[frmods] {n} mod folder(s) in sd:/xplay/mods");
            for (int i = 0; i < n; i++)
            {
                var content = new SwitchModContent(ModName(i), ModFolder(i));
                Current = content;
                var registry = new SwitchModRegistry(content);
                modRegistries[content.Name] = registry;
                int archersBefore = ArcherData.Archers.Length;
                // ContentModule.OnBeforeModInstantiation's order: the four atlases, the five sprite data kinds, then the archers
                SubtextureLoad(registry, content, (int)SubtextureAtlasDestination.Atlas);
                SubtextureLoad(registry, content, (int)SubtextureAtlasDestination.MenuAtlas);
                SubtextureLoad(registry, content, (int)SubtextureAtlasDestination.BGAtlas);
                SubtextureLoad(registry, content, (int)SubtextureAtlasDestination.BossAtlas);
                Log($"[frmods] {content.Name}: atlases loaded");
                SpriteDataLoad(registry, content, (int)ContainerSpriteType.Main);
                SpriteDataLoad(registry, content, (int)ContainerSpriteType.Menu);
                SpriteDataLoad(registry, content, (int)ContainerSpriteType.BG);
                SpriteDataLoad(registry, content, (int)ContainerSpriteType.Boss);
                SpriteDataLoad(registry, content, (int)ContainerSpriteType.Corpse);
                Log($"[frmods] {content.Name}: sprite data loaded");
                MusicLoad(registry, content);   // (ContentModule: the music before the archers, whose <VictoryMusic> may name it)
                if (NativeAlias(content)) continue;   // a PC mod of an archer the Switch has as its own (below): its sprites, not its archers
                if (NativeReskin(content)) reskinOwnArcher = ArcherData.Archers.Length;   // our reskin of a game archer (below): also its own archer, next
                ArcherLoad(registry, content);
                CorpseFallback(archersBefore);
                Log($"[frmods] {content.Name}: archers {archersBefore} -> {ArcherData.Archers.Length}");
            }
            Current = null;
            CheckArchers(firstModArcher);
        }

        // ---- every mod archer checked once, after loading: what the game builds a player, a portrait and a statue from (a null there
        // crashes the game later, in a match: 2026-10-30, BeardedPuppet's null aimer in the Player constructor). One line per archer,
        // the problems named; never fatal here (the archer stays, the log says what will fail)
        static void CheckArchers(int first)
        {
            int ok = 0, bad = 0;
            for (int i = first; i < ArcherData.Archers.Length; i++)
            {
                if (CheckArcher(ArcherData.Archers[i], i, "")) ok++; else bad++;
                if (ArcherData.AltArchers[i] != null) { if (CheckArcher(ArcherData.AltArchers[i], i, " alt")) ok++; else bad++; }
                if (ArcherData.SecretArchers[i] != null) { if (CheckArcher(ArcherData.SecretArchers[i], i, " secret")) ok++; else bad++; }
            }
            Log($"[frmods] archer check: {ok} complete, {bad} with problems (archers {first}..{ArcherData.Archers.Length - 1})");
        }
        static bool CheckArcher(ArcherData a, int index, string kind)
        {
            if (a == null) { Log($"[frmods] ERROR: archer {index}{kind}: no ArcherData"); return false; }
            var missing = new List<string>();
            var main = TFGame.SpriteData.sprites;
            if (a.Aimer == null) missing.Add("aimer");
            string[] whats = { "body", "head", "no-hat head", "crown head", "bow" };
            string[] ids = { a.Sprites.Body, a.Sprites.HeadNormal, a.Sprites.HeadNoHat, a.Sprites.HeadCrown, a.Sprites.Bow };
            for (int k = 0; k < ids.Length; k++)
                if (ids[k] == null || !main.ContainsKey(ids[k])) missing.Add($"{whats[k]} sprite {ids[k]}");
            if (a.Corpse == null || !(TFGame.CorpseSpriteData.sprites.ContainsKey(a.Corpse) || main.ContainsKey(a.Corpse))) missing.Add($"corpse {a.Corpse}");
            if (a.Portraits.NotJoined == null || a.Portraits.Joined == null || a.Portraits.Win == null || a.Portraits.Lose == null) missing.Add("a portrait");
            if (a.Statue.Image == null || a.Statue.Glow == null) missing.Add("statue");
            if (a.Gems.Menu == null || !TFGame.MenuSpriteData.sprites.ContainsKey(a.Gems.Menu)) missing.Add($"menu gem {a.Gems.Menu}");
            if (a.Gems.Gameplay == null || !main.ContainsKey(a.Gems.Gameplay)) missing.Add($"gem {a.Gems.Gameplay}");
            if (!a.StartNoHat && (a.Hat.Normal == null || a.Hat.Blue == null || a.Hat.Red == null)) missing.Add("hat");
            if (missing.Count == 0) { Log($"[frmods] archer {index}{kind} {a.Name0} {a.Name1}: complete"); return true; }
            Log($"[frmods] ERROR: archer {index}{kind} {a.Name0} {a.Name1}: {string.Join(", ", missing)} missing (it will fail in a match)");
            return false;
        }


        // ---- an archer the Switch has as its own, which PC players have as a FortRise mod (TFEX-DIFF S-native): GameBanana's
        // "Madeline & Badeline" (FortRise mod Celeste, ported from the Switch release) is the Switch's archer 9 (TENACIOUS TRAVELER, alt
        // RELENTLESS REFLECTION). With that mod on the SD card, its archers are not loaded (no second Madeline in the roll call): its
        // atlases and sprites are, and the Switch's Madeline and Badeline use the mod's body, heads, bow and corpse (TF.State saves the
        // animation frames, and the mod's differ: dodge 7 frames to the Switch's 9, fall and glide 6 to 7, which desynced smoke lobby
        // 170507 at frame 1140), so both sides play the same sprites; the Switch's own hair stays. Its names are entries in FortRise's
        // ArcherRegistry pointing at the Switch's own archer, which is all TF.EX reads
        // (ArcherDataExtensions: GetCustomArcherId, ResolveCustomArcher, GetInstalledArcherMods; SkinStreamService.Build). Online, a
        // Switch player on Madeline is "Celeste/Madeline" (a PC player with the mod sees the mod's Madeline; without it, a stand-in with
        // the Switch's art streamed), and a PC player's mod Madeline is the Switch's own here. The secret pink Madeline has no Switch
        // archer: TF.EX's ResolveArchers falls back to the normal one. No entry ever grows the game's archer arrays (ModArchers.Invoke).
        static readonly string[] AliasMods = { "Celeste" };
        static readonly string[] AliasIds = { "Madeline", "Badeline", "MadelineButPink" };
        static readonly int[] AliasTypes = { (int)ArcherEntryType.Normal, (int)ArcherEntryType.Alt, (int)ArcherEntryType.Secret };
        const int AliasArcher = 9;
        // the mod's sprite ids for each alias (its archerData.xml: Body, HeadNormal, HeadNoHat, HeadCrown, Bow, Corpse; "@" = the mod's own)
        static readonly string[][] AliasSprites = {
            new[] { "Celeste", "CelesteNoHat", "CelesteNoHat", "CelesteCrown", "CelesteBow", "CelesteCorpse" },
            new[] { "Celeste_Alt", "Celeste_AltNoHat", "Celeste_AltNoHat", "Celeste_AltCrown", "Celeste_AltBow", "Celeste_AltCorpse" },
        };
        static bool NativeAlias(SwitchModContent content)
        {
            string mod = content.Name;
            if (Array.IndexOf(AliasMods, mod) < 0) return false;
            var native = AliasArcher < ArcherData.Archers.Length ? ArcherData.Archers[AliasArcher] : null;
            if (native == null || native.Name0 != "TENACIOUS")
            {
                Log($"[frmods] ERROR: {mod}: the Switch's archer {AliasArcher} isn't TENACIOUS TRAVELER: the mod's archers load as their own");
                return false;
            }
            for (int k = 0; k < AliasIds.Length; k++)
                ArcherRegistry.AddArcher(new ArcherEntry($"{mod}/{AliasIds[k]}", new ArcherConfiguration(), AliasArcher, (ArcherEntryType)AliasTypes[k]));
            UseModSprites(ArcherData.Archers[AliasArcher], mod, AliasSprites[0]);
            UseModSprites(ArcherData.AltArchers[AliasArcher], mod, AliasSprites[1]);
            Log($"[frmods] {mod} {ModVersion(mod)}: its archers are the Switch's own archer {AliasArcher} ({AliasIds.Length} names, not loaded)");
            return true;
        }

        // ---- our own reskin of a game archer, Switch-only (TFEX-DIFF S-native): MexicanPuppet (tools/mods/ssb_to_fortrise.py, combined-mod's
        // bearded portraits). Local play: the game's Prancing Puppet alt (archer 6, alt) is MEXICAN PUPPET (its names and four portraits) and
        // the mod's own archer is hidden from the roll call. Online: the mod's own archer, so TF.EX sends it by name with its art (a PC with
        // the mod shows it, one without a stand-in with the streamed skin), the game's alt is its vanilla self again and can't be picked on
        // the Switch (tfex/FrModsBoot.cpp: GetArcherUnlocked and the roll call's alt; ReskinOnline from pcgameplay::Set). The mod's
        // archerData.xml gives the names and portrait files.
        static readonly string[] ReskinMods = { "MexicanPuppet" };
        const int ReskinArcher = 6;
        static int reskinOwnArcher = -1;   // the mod's own archer's index (the roll call hides it offline)
        public static int ReskinOwnArcher() => reskinOwnArcher;
        public static int ReskinGameArcher() => reskinOwnArcher >= 0 ? ReskinArcher : -1;   // the game's archer whose alt is reskinned
        static Subtexture[] vanillaPortraits, reskinPortraits;
        static string[] vanillaNames, reskinNames;
        // online: the game's alt as it ships; local play: the reskin (pcgameplay::Set)
        public static void ReskinOnline(bool online)
        {
            if (reskinPortraits == null) return;
            var native = ArcherData.AltArchers[ReskinArcher];
            var p = online ? vanillaPortraits : reskinPortraits;
            var n = online ? vanillaNames : reskinNames;
            native.Portraits.NotJoined = p[0]; native.Portraits.Joined = p[1]; native.Portraits.Win = p[2]; native.Portraits.Lose = p[3];
            native.Name0 = n[0]; native.Name1 = n[1];
            Log($"[frmods] the game's alt archer {ReskinArcher}: {(online ? "its own (online)" : $"{n[0]} {n[1]} (local play)")}");
        }
        static bool NativeReskin(SwitchModContent content)
        {
            string mod = content.Name;
            if (Array.IndexOf(ReskinMods, mod) < 0) return false;
            var native = ReskinArcher < ArcherData.AltArchers.Length ? ArcherData.AltArchers[ReskinArcher] : null;
            var xml = new SdResource(content.Root.Root, "Content/Atlas/GameData/archerData.xml").Xml;
            var archer = xml?["Archers"]?["Archer"];
            if (native == null || archer == null) { Log($"[frmods] ERROR: {mod}: no game alt archer {ReskinArcher} or no <Archer> in its archerData.xml: loaded as its own archer"); return false; }
            var portraits = archer["Portraits"];
            Subtexture Portrait(string tag)
            {
                var path = portraits?[tag]?.InnerText?.Trim();
                var entry = path == null ? null : modRegistries[content.Name].Subtextures.RegisterTexture((IResourceInfo)(object)SdResource.At(content.Root.Root, path), SubtextureAtlasDestination.MenuAtlas);
                return entry?.Subtexture;
            }
            var notJoined = Portrait("NotJoined"); var joined = Portrait("Joined"); var win = Portrait("Win"); var lose = Portrait("Lose");
            if (notJoined == null || joined == null || win == null || lose == null) { Log($"[frmods] ERROR: {mod}: a portrait didn't load: the game's alt {ReskinArcher} unchanged, the mod only its own archer"); return false; }
            vanillaPortraits = new[] { native.Portraits.NotJoined, native.Portraits.Joined, native.Portraits.Win, native.Portraits.Lose };
            vanillaNames = new[] { native.Name0, native.Name1 };
            reskinPortraits = new[] { notJoined, joined, win, lose };
            reskinNames = new[] { archer["Name0"]?.InnerText?.Trim() ?? native.Name0, archer["Name1"]?.InnerText?.Trim() ?? native.Name1 };
            ReskinOnline(false);
            Log($"[frmods] {mod} {ModVersion(mod)}: local play's alt archer {ReskinArcher}; online its own archer {ArcherData.Archers.Length} (loaded next)");
            return true;
        }

        static void UseModSprites(ArcherData native, string mod, string[] ids)
        {
            if (native == null) return;
            string Id(string s) => $"{mod}/{s}";
            foreach (var s in ids)
                if (!TFGame.SpriteData.sprites.ContainsKey(Id(s))) { Log($"[frmods] ERROR: {mod}: sprite {Id(s)} not loaded: {native.Name0} keeps the Switch's sprites (online play desyncs)"); return; }
            native.Sprites.Body = Id(ids[0]);
            native.Sprites.HeadNormal = Id(ids[1]);
            native.Sprites.HeadNoHat = Id(ids[2]);
            native.Sprites.HeadCrown = Id(ids[3]);
            native.Sprites.Bow = Id(ids[4]);
            native.Corpse = Id(ids[5]);
            CorpseOf(native, TFGame.CorpseSpriteData, TFGame.SpriteData);
            Log($"[frmods] {native.Name0} {native.Name1}: the sprites of {mod}'s {ids[0]} (the PC's)");
        }

        // ---- subtextures: FortRise's ModSubtextures + SubtextureRegistry, translated (Fr_ModSubtextures, Fr_SubtextureRegistry, Fr_SubtextureEntry)

        // FortRise's patch_Atlas.SafeAdd (+ TFGame's ConvertToFastLookup): the texture into the game's atlas (overrides.toml's rule says why)
        public static void AtlasAdd(Atlas atlas, string id, Subtexture texture) { if (texture != null) atlas.SubTextures[id] = texture; }

        // TF.NX's catalog `replaces` (translate.toml [mods.catalog]; TFEX-DIFF S-native: a catalog mod's logic is compiled in, so the game
        // textures it restyles are named in the catalog): the game's texture `id` becomes the loaded mod's own `modId` (its <mod>/<name>).
        // False when the mod's texture isn't there (its folder's atlas lacks it).
        public static bool ReplaceSubtexture(string id, string modId, int dest)
        {
            var atlas = AtlasOf(dest);
            if (!atlas.SubTextures.ContainsKey(modId)) return false;
            atlas.SubTextures[id] = atlas[modId];
            return true;
        }

        // A texture the game lacks, as another of its own (TFEX-DIFF S-native: WiderSet's 8 seats on the Switch need controller icons for
        // players 7-8, which the game's atlas doesn't have and a release can't ship): `id` = `from` when `id` is missing; true when it is there now
        public static bool AliasMissingSubtexture(string id, string from, int dest)
        {
            var atlas = AtlasOf(dest);
            if (atlas.SubTextures.ContainsKey(id)) return true;
            if (!atlas.SubTextures.ContainsKey(from)) return false;
            atlas.SubTextures[id] = atlas[from];
            return true;
        }


        // VanillaXmlCacher.GetAllAvailableSubtexturesID (InternalMods/FortRise.Content/Core/Utilities/VanillaXmlCacher.cs) and
        // SubtextureRegistry's (translate.toml [[rewrite]]): FortRise reads the PC's atlas XML files into a HashSet<string> (none in the
        // Switch's build); the loaded atlases hold the same IDs
        public static bool IsVanillaSubtexture(string id) =>
            TFGame.Atlas.SubTextures.ContainsKey(id) || TFGame.MenuAtlas.SubTextures.ContainsKey(id) || TFGame.BGAtlas.SubTextures.ContainsKey(id) ||
            (TFGame.BossAtlas != null && TFGame.BossAtlas.SubTextures.ContainsKey(id));

        // ---- sprites: FortRise's ModSprites, SpriteRegistry, SpriteContainerEntry, SpriteEntry<T>, translated (Fr_ModSprites ...)

        // FortRise's PlayerCorpse patch (Patches/PlayerCorpse.cs): a corpse ID not in CorpseSpriteData is the main SpriteData's (a content
        // mod's corpse sprite is in its spriteData.xml). TFEX-DIFF S-native: the game's PlayerCorpse constructor is not patched here; when
        // both SpriteData share an atlas, the mod archers' corpse XML in CorpseSpriteData too makes the game's own lookup give the same sprite
        static void CorpseFallback(int firstNew)
        {
            var corpses = TFGame.CorpseSpriteData;
            var main = TFGame.SpriteData;
            if (corpses.atlas != main.atlas) { Log("[frmods] CorpseSpriteData has its own atlas: a mod archer's main-sprite corpse isn't found (no PlayerCorpse patch)"); return; }
            for (int i = firstNew; i < ArcherData.Archers.Length; i++)
            {
                CorpseOf(ArcherData.Archers[i], corpses, main);
                CorpseOf(ArcherData.AltArchers[i], corpses, main);
                CorpseOf(ArcherData.SecretArchers[i], corpses, main);
            }
        }
        static void CorpseOf(ArcherData archer, SpriteData corpses, SpriteData main)
        {
            if (archer == null || archer.Corpse == null || corpses.sprites.ContainsKey(archer.Corpse) || !main.sprites.ContainsKey(archer.Corpse)) return;
            corpses.sprites[archer.Corpse] = main.sprites[archer.Corpse];
            Log($"[frmods] corpse {archer.Corpse}: the main SpriteData's (as FortRise's PlayerCorpse)");
        }

        // FortRise's enums as numbers here (SubtextureAtlasDestination: Atlas, BGAtlas, MenuAtlas, BossAtlas; ContainerSpriteType: Main,
        // Menu, Corpse, BG, Boss): the game's atlas of each (FortRise's SubtextureEntry does the same switch)
        static Atlas AtlasOf(int dest) =>
            dest == (int)SubtextureAtlasDestination.MenuAtlas ? TFGame.MenuAtlas : dest == (int)SubtextureAtlasDestination.BGAtlas ? TFGame.BGAtlas :
            dest == (int)SubtextureAtlasDestination.BossAtlas ? TFGame.BossAtlas : TFGame.Atlas;

        // ---- FortRise's IDPool (Core/Utils/IDPool.cs): a counter per kind
        static readonly Dictionary<string, int> pool = new Dictionary<string, int>();
        public static void WarmIndex(string id, int offset) { pool[id] = offset; }
        public static int Obtain(string id)
        {
            if (!pool.ContainsKey(id)) { pool[id] = 0; return 0; }
            pool[id] = pool[id] + 1;
            return pool[id];
        }

        // ---- FortRise's RegistryQueue<T> once ready: ModuleManager.CreateQueue -> a queue that invokes at once
        public static SwitchQueue CreateQueue(object manager, Action<object> invoker)
        {
            var q = new SwitchQueue();
            q.Invoker = invoker;
            if (manager == SoundsManager && !soundsLoaded) { q.Pending = new List<object>(); soundQueues.Add(q); }
            return q;
        }
        public static SwitchQueue CreateQueue(object manager, Action<object> invoker, int batch) => CreateQueue(manager, invoker);
        public static void AddOrInvoke(SwitchQueue queue, object entry) { if (queue.Pending != null) queue.Pending.Add(entry); else queue.Invoker(entry); }

        // ---- a mod archer's own sounds: FortRise's queue for ModCharacterSounds waits for the game's sounds (on the PC, RegistryQueue until
        // the load finished, then TFGame.MainMenuLoadWait's patch_Sounds.LoadModdedCharacterSounds). Here the mods load inside GameData.Load,
        // before Sounds.Load (Sounds.Characters is still null): the character sounds' queue is held until the first main-menu frame
        // (tfexgen_glue.cpp's MainMenu.Update hook calls LoadSounds), each entry's CharacterSounds built then (its fallback archer's sounds
        // read from Sounds.Characters), then LoadModdedCharacterSounds as the PC's
        public static readonly object SoundsManager = new object();   // the "manager" ModCharacterSounds is made with (its queue is held)
        static readonly List<SwitchQueue> soundQueues = new List<SwitchQueue>();
        static bool soundsLoaded;
        // the game's vanilla character sounds, which RiseCore warms the IDs past (the PC's: 14, IDPool.WarmIndex("characterSounds", 13));
        // the Switch's count, checked against Sounds.Characters in LoadSounds (TFEX-DIFF S-native, as the archers' 10)
        const int VanillaCharacterSounds = 16;   // (measured: the Switch's Sounds.Characters, 2026-10-30)
        public static void LoadSounds()
        {
            // FortRise's MainMenuLoadWait: after the game's loading (TaskHelper.WaitForAll). The console's menu runs while the game still
            // loads (Sounds.Characters null: the 974d98ec boot crash in ArcherLoader's sound fallbacks), so wait for TFGame.Loaded
            if (soundsLoaded || !TFGame.Loaded) return;
            soundsLoaded = true;
            int vanilla = Sounds.Characters.Length;
            if (vanilla != VanillaCharacterSounds)
                Log($"[frmods] ERROR: the game has {vanilla} character sounds, the mod archers' SFXIDs counted from {VanillaCharacterSounds} (their sounds are off)");
            int n = 0;
            foreach (var q in soundQueues)
            {
                foreach (var e in q.Pending) { q.Invoker(e); n++; }
                q.Pending = null;
            }
            patch_Sounds.LoadModdedCharacterSounds();
            Log($"[frmods] character sounds {vanilla} -> {Sounds.Characters.Length} ({n} mod archers' own, built on the first menu frame)");
        }

        // ---- what overrides.toml's rules for FortRise's interfaces call (IModContent, ModuleMetadata, IResourceInfo, IModRegistry, the entries)
        public static SdResource ContentRoot(SwitchModContent c) => c.Root;
        public static string ContentName(SwitchModContent c) => c.Name;
        public static SwitchModContent CurrentMod() => Current;
        public static SwitchModContent RegistryMod(SwitchModRegistry r) => r.Mod;
        public static object RegistryArchers(SwitchModRegistry r) => r.Archers;
        public static object RegistrySFXs(SwitchModRegistry r) => r.SFXs ??= NewModSFXs(r.Mod);
        public static object RegistryCharacterSounds(SwitchModRegistry r) => r.CharacterSounds ??= NewModCharacterSounds(r.Mod);
        public static object RegistryMusics(SwitchModRegistry r) => r.Musics ??= NewModMusics(r.Mod);
        [Cpp("tfex::gen::Fr_ModMusics::New($0, nullptr)", Include = "tfexgen_Fr_ModMusics.hpp")] static extern object NewModMusics(object metadata);
        // FortRise's ModSFXs / ModCharacterSounds, translated (Fr_ModSFXs, Fr_ModCharacterSounds): this mod's content object as their
        // "metadata" (Name: a rule), no manager (CreateQueue's queue: registered at once)
        [Cpp("tfex::gen::Fr_ModSFXs::New($0, nullptr)", Include = "tfexgen_Fr_ModSFXs.hpp")] static extern object NewModSFXs(object metadata);
        [Cpp("tfex::gen::Fr_ModCharacterSounds::New($0, $1)", Include = "tfexgen_Fr_ModCharacterSounds.hpp")] static extern object NewModCharacterSoundsWith(object metadata, object manager);
        static object NewModCharacterSounds(object metadata) => NewModCharacterSoundsWith(metadata, SoundsManager);
        // IBaseSFXEntry.BaseSFX: the three entry kinds FortRise registers (CharacterSounds' configuration holds each as its interface)
        public static string SfxId(string id, string type) => id + "_" + type + "_SFX";   // ArcherLoader's sound ids ([invalid] rules)
        public static string SfxEntryName(object e) => e is SFXVariedEntry v ? v.Name : e is SFXLoopedEntry l ? l.Name : e is SFXEntry s ? s.Name : null;
        public static SFX BaseSfx(object e) => e is SFXVariedEntry v ? v.BaseSFX : e is SFXLoopedEntry l ? l.BaseSFX : e is SFXEntry s ? s.BaseSFX : null;
        // `new patch_SFX(stream, ...)` and kin (FortRise's [MonoModConstructor]s): the game's object (its type: SFX, SFXVaried, SFXLooped
        // dispatch Play / Stop), then FortRise's constructor body (Fr_SFX ...), whose first call is the game's SFX(bool) on it
        [Cpp("gen::Fn<void* (*)(void*)>(game::brute::NewObject)(gen::TypeOf(bind::t_SFX))")] static extern SFX AllocSfx();
        [Cpp("gen::Fn<void* (*)(void*)>(game::brute::NewObject)(gen::TypeOf(bind::t_SFXVaried))")] static extern SFXVaried AllocSfxVaried();
        [Cpp("gen::Fn<void* (*)(void*)>(game::brute::NewObject)(gen::TypeOf(bind::t_SFXLooped))")] static extern SFXLooped AllocSfxLooped();
        public static SFX NewSfx(System.IO.Stream stream, bool obeysMasterPitch) { var s = AllocSfx(); ((patch_SFX)s).ctor(stream, obeysMasterPitch); return s; }
        public static patch_SFXVaried NewSfxVaried(System.IO.Stream[] streams, int amount, bool obeysMasterPitch) { var s = (patch_SFXVaried)(object)AllocSfxVaried(); s.ctor(streams, amount, obeysMasterPitch); return s; }
        public static patch_SFXLooped NewSfxLooped(System.IO.Stream stream, bool obeysMasterPitch) { var s = (patch_SFXLooped)(object)AllocSfxLooped(); s.ctor(stream, obeysMasterPitch); return s; }
        public static string ResPath(SdResource r) => r.Path;
        public static string ResRootPath(SdResource r) => r.RootPath;
        public static XmlDocument ResXml(SdResource r) => r.Xml;
        public static Stream ResStream(SdResource r) => r.Stream;
        public static string ResText(SdResource r) => r.Text;
        public static string ResName(SdResource r) { int slash = r.Path.LastIndexOf('/'); return slash < 0 ? r.Path : r.Path.Substring(slash + 1); }   // FortRise: the file's name
        public static SdResource ResGetRelative(SdResource r, string path) => r.GetRelativePath(path);
        public static bool ResExists(SdResource r, string path) => r.ExistsRelativePath(path);
        public static bool ResTryGet(SdResource r, string path, out SdResource resource) => r.TryGetRelativePath(path, out resource);
        public static List<SdResource> ResEnumerate(SdResource r, string pattern) => r.EnumerateChildrens(pattern);
        public static HashSet<string> VanillaSubtextureIds()
        {
            var ids = new HashSet<string>();
            foreach (var kv in TFGame.Atlas.SubTextures) ids.Add(kv.Key);
            foreach (var kv in TFGame.MenuAtlas.SubTextures) ids.Add(kv.Key);
            foreach (var kv in TFGame.BGAtlas.SubTextures) ids.Add(kv.Key);
            foreach (var kv in TFGame.BossAtlas.SubTextures) ids.Add(kv.Key);
            return ids;
        }
        // Monocle's Calc.ChildEnum<T> (a generic game method: no instantiation to bind) per enum type, its body: the child's text by name,
        // else the default
        public static TFGame.Genders ChildEnumGenders(XmlElement xml, string childName, TFGame.Genders defaultValue) =>
            xml[childName] != null && Enum.TryParse<TFGame.Genders>(xml[childName].InnerText, out var v) ? v : defaultValue;
        public static ArcherData.HatMaterials ChildEnumHatMaterials(XmlElement xml, string childName, ArcherData.HatMaterials defaultValue) =>
            xml[childName] != null && Enum.TryParse<ArcherData.HatMaterials>(xml[childName].InnerText, out var v) ? v : defaultValue;
        public static ArcherData[] GetArchers() => ArcherData.Archers;
        public static ArcherData[] GetAltArchers() => ArcherData.AltArchers;
        public static ArcherData[] GetSecretArchers() => ArcherData.SecretArchers;

        // ---- patch_ArcherData's statics are the game's (FortRise's [MonoModIgnore] [MonoModPublic] setters)
        public static void SetArchers(ArcherData[] value) { ArcherData.Archers = value; }
        public static void SetAltArchers(ArcherData[] value) { ArcherData.AltArchers = value; }
        public static void SetSecretArchers(ArcherData[] value) { ArcherData.SecretArchers = value; }

        // ---- FortRise's patch_Music for a mod's own tracks (TFEX-DIFF S-native, overrides.toml's patch_Music rules). FortRise replaces the
        // game's music engine on the PC (XACT cues, OGG / WAV tracks streamed through DynamicSoundEffectInstance, cross-fades); here the game's
        // Music stays for its cues and a registered track (ModMusics.Invoke: TrackMap) plays as the game's SoundEffectInstance, its file read
        // whole (an OGG decoded to WAV by FortRise's decoder, stb_vorbis: frmods OggToWav), the way the mods' sounds load
        // (SoundEffect.FromStream). The game's Music.Play / PlayImmediate / Stop end it (tfex/FrModsBoot.cpp MusicHooks: GameMusic), and its
        // Music.Update plays PlayNext's queue once the track ends (MusicUpdate).
        static readonly Dictionary<string, TrackInfo> trackMap = new Dictionary<string, TrackInfo>();
        public static Dictionary<string, TrackInfo> TrackMap() => trackMap;
        static SoundEffectInstance track;
        static string trackName;
        static readonly List<string> nextSongs = new List<string>();
        static readonly List<int> nextLooping = new List<int>();   // (1: looping; the game has no List<bool>)
        static int musicLogged;
        public static void MusicPlayImmediate(string name, bool looping)
        {
            StopTrack();
            if (!trackMap.TryGetValue(name, out var info))
            {
                Music.PlayImmediate(name);   // a game cue (its looping is the cue's own, as FortRise's VanillaMusicSystem)
                return;
            }
            Music.Stop();
            var effect = LoadTrack(info);
            if (effect == null) return;
            track = effect.CreateInstance();
            track.IsLooped = looping;
            track.Volume = Music.MasterVolume;
            track.Play();
            trackName = name;
            if (musicLogged++ < 8) Log($"[frmods] music: {name} plays (a mod's track{(looping ? ", looping" : "")})");
        }
        public static void MusicPlayNext(string name, bool looping) { nextSongs.Add(name); nextLooping.Add(looping ? 1 : 0); }
        static SoundEffect LoadTrack(TrackInfo info)
        {
            var res = (SdResource)info.Resource;
            int size = FileSize(res.RootPath);
            if (size <= 0) { Log($"[frmods] ERROR: music: {info.Name}: no file {res.RootPath}"); return null; }
            var bytes = new byte[size];
            ReadInto(res.RootPath, bytes);
            if (res.Path.EndsWith(".ogg", StringComparison.OrdinalIgnoreCase)) bytes = OggToWav(bytes);
            else if (!res.Path.EndsWith(".wav", StringComparison.OrdinalIgnoreCase)) { Log($"[frmods] ERROR: music: {info.Name}: {res.Path} is no .ogg or .wav"); return null; }
            if (bytes.Length == 0) return null;
            return SoundEffect.FromStream(ManagedStream(bytes));
        }
        [Cpp("tfex::gen::frmods::OggToWav($0)", Include = "tfexgen_frmods.hpp")] static extern byte[] OggToWav(byte[] ogg);
        static void StopTrack()
        {
            if (track == null) return;
            track.Stop();
            track.Dispose();
            track = null;
            trackName = null;
        }
        // the game's own Music.Play / PlayImmediate / Stop (FortRise's replacements stop the playing track the same way); PlayNext's queue
        // belongs to the track it followed
        public static void GameMusic()
        {
            if (track == null) return;
            StopTrack();
            nextSongs.Clear();
            nextLooping.Clear();
        }
        public static void MusicUpdate()
        {
            if (track == null || track.State != SoundState.Stopped) return;
            var ended = trackName;
            StopTrack();
            if (nextSongs.Count == 0) return;
            var song = nextSongs[0]; var looping = nextLooping[0] != 0;
            nextSongs.RemoveAt(0); nextLooping.RemoveAt(0);
            if (musicLogged++ < 8) Log($"[frmods] music: {ended} ended, then {song}");
            MusicPlayImmediate(song, looping);
        }

        // ---- FortRise's patch_PlayerCorpse(Vector2, ArcherData, ...): its hair half, after the game's constructor (tfex/FrModsBoot.cpp
        // CorpseHook). TFEX-DIFF S-native: FortRise replaces the whole constructor; the Switch's (+0xafa140) is the same but for the hair
        // it makes (the Switch's PlayerHair constructor takes the archer's link count and more, ArcherData +0x44..), so the game's stays
        // and this is the rest of FortRise's body verbatim: the archer's HairInfo onto the corpse's hair.
        public static void CorpseHair(PlayerCorpse corpse, ArcherData archerData)
        {
            if (corpse.Hair == null || !archerData.Hair)
            {
                return;
            }

            var parcherData = (patch_ArcherData)archerData;
            if (!parcherData.ExtraHairData.TryGetValue(out HairInfo hairData))
            {
                return;
            }

            var hair = (patch_PlayerHair)corpse.Hair;

            hair.Color = hairData.Color;
            hair.DuckingOffset = hairData.DuckingOffset;
            hair.Offset = hairData.Offset;
            hair.OutlineColor = hairData.OutlineColor;

            hair.ChangeHairTexture(hairData.Texture?.Subtexture, hairData.TextureEnd?.Subtexture);
        }
    }
}
