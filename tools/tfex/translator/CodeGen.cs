// Stages 2-3: TF.EX methods -> C++ on the Switch game, from Roslyn's operation tree (types, overloads, defaults, conversions resolved).
// Units come from translate.toml [[unit]]: { name, file, type, members? }. Each becomes tfexgen_<name>.{hpp,cpp} in namespace
// tfex::gen::<name>. Game members are bound through Binder (bind.py); what has no mechanical mapping is a hand rule from
// translate.toml ([[rule]]) or is reported as unsupported (and emitted as a compile-time marker).
using System.Text;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using Microsoft.CodeAnalysis.Operations;
using Tomlyn.Model;

namespace TfexTranslator;

public sealed class CodeGen
{
    readonly Workspace ws;
    readonly Config cfg;
    public readonly Binder Binder;
    public readonly Report Report = new();
    public readonly ModelsGen Models;
    public readonly StructsGen Structs;
    readonly MetadataOrder order;

    /* FortRise's source (vendor/fortrise): its own compilation (Workspace.ForFortRise); units whose file starts "fortrise/" use it */
    readonly Workspace fortrise;
    /* FortRise code mods' source (vendor/teamplus: Teams+), a compilation each, by path prefix ("teamplus/"): units whose file starts so */
    readonly Dictionary<string, Workspace> mods;
    public Workspace WsOf(string file) => file == null ? ws : file.StartsWith("fortrise/") && fortrise != null ? fortrise
        : mods.FirstOrDefault(kv => file.StartsWith(kv.Key)).Value ?? ws;

    public CodeGen(Workspace ws, Config cfg, Workspace fortrise = null, Dictionary<string, Workspace> mods = null)
    {
        this.ws = ws;
        this.fortrise = fortrise;
        this.mods = mods ?? new();
        this.cfg = cfg;
        Binder = new Binder(cfg.Dir);
        var ov = Path.Combine(cfg.Dir, "overrides.toml");
        if (File.Exists(ov) && Tomlyn.Toml.ToModel(File.ReadAllText(ov)) is TomlTable ot && ot.TryGetValue("service", out var sv) && sv is TomlTable st)
            foreach (var (k, v) in st) if (v is TomlTable r) Services.Add((k, r));
        if (File.Exists(ov) && Tomlyn.Toml.ToModel(File.ReadAllText(ov)) is TomlTable oa && oa.TryGetValue("after", out var av) && av is TomlTable at)
            foreach (var (k, v) in at) if (v is TomlTable r) Afters[k] = r;
        // tools/tfex/state-pins.toml: the TF.State measurement's private-field leads (tools/re/privfield.py, not confirmed in the disassembly)
        var spf = Path.Combine(cfg.Dir, "state-pins.toml");
        if (File.Exists(spf) && Tomlyn.Toml.ToModel(File.ReadAllText(spf)) is TomlTable spt && spt.TryGetValue("pin", out var spp) && spp is TomlTable sps)
            foreach (var (k, v) in sps)
                if (v is TomlTable t && !Binder.Pins.ContainsKey(k)) Binder.Pins[k] = ("off", (long)t["off"], "MEASUREMENT LEAD, unconfirmed: " + (string)t["why"]);
        foreach (var (k, v) in cfg.Table("pin"))
            if (v is TomlTable vt && vt.ContainsKey("slot") && vt.ContainsKey("body"))   // a virtual method: its body (m_) and its vtable slot (v_)
            { Binder.Pins[k] = ("body", (long)vt["body"], (string)vt["why"]); Binder.SlotPins[k] = ("slot", (long)vt["slot"], (string)vt["why"]); }
            else if (v is TomlTable t) Binder.Pins[k] = t.ContainsKey("off") ? ("off", (long)t["off"], (string)t["why"]) : t.ContainsKey("slot") ? ("slot", (long)t["slot"], (string)t["why"]) :
                                                 t.ContainsKey("static") ? ("static", (long)t["static"], (string)t["why"]) : ("body", (long)t["body"], (string)t["why"]);   // static: a static field's storage
        order = new MetadataOrder(cfg);
        Models = new ModelsGen(ws, cfg);
        Structs = new StructsGen(ws, fortrise, this.mods.Select(kv => (kv.Value, cfg.Table("code_mods").TryGetValue(kv.Key, out var mv) && mv is TomlTable mt && mt["structs"] is string sp ? sp : "Mod_")).ToList());
        UnitEmitter.CanonProperty = Structs.CanonProperty;
    }

    /* the units emitted in the last pass and the type each translates (a [dispatch] interface's implementing classes) */
    readonly List<(string unit, INamedTypeSymbol type)> emittedTypes = new();

    /* translate.toml [dispatch."<interface>"]: an interface the Switch calls on objects of classes from different compilations (FortRise's
     * IVersusGameMode: TF.EX's NetplayVersusMode, Teams+'s and other code mods' modes), where .NET has interface dispatch and the translator
     * has none across compilations. Generated: for each member, the class of the object among the units' classes implementing it (any
     * compilation) and that unit's function; a class without the member, or an object of no such class, gets the interface's default
     * (`default`, the C++ of the default body; an unknown class is logged once). `sig`: the member's C++ function (the hand header's). */
    (string file, string text) Dispatch(string iface, TomlTable dt, List<(string file, string text)> outputs)
    {
        var impls = emittedTypes.Where(e => e.type.AllInterfaces.Any(i => i.ToDisplayString() == iface)).OrderBy(e => e.unit, StringComparer.Ordinal).ToList();
        string Hpp(string unit) => outputs.FirstOrDefault(o => o.file == $"tfexgen_{unit}.hpp").text ?? "";
        var sb = new StringBuilder($"/* GENERATED by tools/tfex/translator (translate.toml [dispatch.\"{iface}\"]): {iface} called on an object, by its class:\n");
        foreach (var (u, t) in impls) sb.Append($" *   {t.ToDisplayString()} ({u})\n");
        sb.Append(" * A member a class doesn't implement, or an object of none of these classes (logged once), is the interface's default. */\n");
        sb.Append($"#include \"{(string)dt["header"]}\"\n#include \"../../hwlog.hpp\"\n");
        foreach (var (u, _) in impls) sb.Append($"#include \"tfexgen_{u}.hpp\"\n");
        var tag = (string)dt["log"];
        sb.Append($"\nnamespace {(string)dt["ns"]} {{\nnamespace {{\nbool Is(void* m, void* t) {{ return m && t && gen::As(m, t); }}\n");
        sb.Append($"bool Known(void* m) {{ return {(impls.Count == 0 ? "false" : string.Join(" || ", impls.Select(e => $"Is(m, gen::{e.unit}::RuntimeType())")))}; }}\n");
        sb.Append($"void Unknown(void* m, const char* what) {{\n    static int logged = 0;\n    if (m && !Known(m) && logged++ < 4) hw::Log(\"{tag} ERROR: {iface.Split('.').Last()}.%s on an object of no known class: the interface's default\", what);\n}}\n}}  // namespace\n\n");
        foreach (var (member, mv) in (TomlTable)dt["members"])
        {
            var mt = (TomlTable)mv;
            var sig = (string)mt["sig"];
            var mm = System.Text.RegularExpressions.Regex.Match(sig, @"^(.*?)\s*\b(\w+)\((.*)\)$");
            var ret = mm.Groups[1].Value.Trim();
            var args = mm.Groups[3].Value.Split(',').Select(a => a.Trim().Split(' ', '*').Last()).ToList();   // "void* m" -> m
            var rest = string.Join("", args.Skip(1).Select(a => ", " + a));
            sb.Append(sig).Append(" {\n");
            foreach (var (u, t) in impls)
            {
                var sym = t.GetMembers(member).FirstOrDefault();
                if (sym == null) continue;   // the interface's default for this class
                var fn = sym is IPropertySymbol ? "get_" + member : member;
                if (!Hpp(u).Contains($" {fn}(")) { Report.Add("dispatch: an implementing unit lacks the member (the default instead)", $"{iface}.{member}: {u}"); continue; }
                var call = $"gen::{u}::{fn}({args[0]}{rest})";
                sb.Append($"    if (Is({args[0]}, gen::{u}::RuntimeType())) {(ret == "void" ? $"{{ {call}; return; }}" : $"return {call};")}\n");
            }
            sb.Append($"    Unknown({args[0]}, \"{member}\");\n");
            if (ret != "void") sb.Append($"    return {(string)mt["default"]};\n");
            sb.Append("}\n");
        }
        sb.Append($"}}  // namespace {(string)dt["ns"]}\n");
        Report.Add("dispatch: an interface across compilations, generated", $"{iface}: {string.Join(", ", impls.Select(e => e.unit))}");
        return ($"tfexgen_dispatch_{(string)dt["file"]}.cpp", sb.ToString());
    }

    /* TF.EX type -> the unit that translates it (calls between units) */
    public readonly Dictionary<string, TomlTable> UnitOfType = new();
    /* units added while translating: a TF.EX type the code calls that no configured unit translates and no service rule maps */
    public readonly Queue<TomlTable> Pending = new();
    public bool AutoUnits = true;
    /* an auto unit holds only the members its callers use (and what those call): a member a caller asks for after the unit was
     * emitted makes it emit again (Run loops until no unit grows) */
    public bool WholeAutoUnits = false;   // coverage: whole types
    /* a TF.EX interface's implementing classes in TF.EX's network code (IGameContext -> GameContext): a call through the interface with
     * no [service] rule goes to the single implementation's generated method */
    Dictionary<INamedTypeSymbol, List<INamedTypeSymbol>> impls;
    /* a unit's key in UnitOfType: file|type, and for a configured generic class's instantiation (translate.toml typeargs = ["string"]:
     * FortRise's SpriteEntry<string>) its type arguments, as an auto unit's (InstSuffix) */
    public static string UnitKey(TomlTable un) =>
        (string)un["file"] + "|" + (string)un["type"] + (un.TryGetValue("typeargs", out var ta) && ta is TomlArray tarr && tarr.Count > 0 && !un.ContainsKey("auto")
            ? "<" + string.Join(",", tarr.Select(x => TypeArgDisplay((string)x))) + ">" : un.ContainsKey("auto") && un.TryGetValue("typeargs", out var ta2) && ta2 is TomlArray tarr2 && tarr2.Count > 0
            ? "<" + string.Join(",", tarr2.Select(x => (string)x)) + ">" : "");
    static string TypeArgDisplay(string a) => a;   // (C# keywords as written: "string", "int", as ToDisplayString shows them)
    /* a configured unit with typeargs: its class constructed with them (null otherwise) */
    public INamedTypeSymbol ConfiguredInst(TomlTable un)
    {
        if (un.ContainsKey("auto") || !un.TryGetValue("typeargs", out var ta) || ta is not TomlArray tarr || tarr.Count == 0) return null;
        var w = WsOf((string)un["file"]);
        var tree = w.Trees.FirstOrDefault(t => t.FilePath == (string)un["file"]);
        if (tree == null) return null;
        var sm = w.Model(tree);
        var def = tree.GetRoot().DescendantNodes().OfType<Microsoft.CodeAnalysis.CSharp.Syntax.ClassDeclarationSyntax>()
            .Select(d => sm.GetDeclaredSymbol(d)).OfType<INamedTypeSymbol>().FirstOrDefault(c => c.Name == (string)un["type"]);
        if (def == null || def.TypeParameters.Length != tarr.Count) return null;
        ITypeSymbol Arg(string a) => a switch
        {
            "string" => w.Compilation.GetSpecialType(SpecialType.System_String), "int" => w.Compilation.GetSpecialType(SpecialType.System_Int32),
            "float" => w.Compilation.GetSpecialType(SpecialType.System_Single), "bool" => w.Compilation.GetSpecialType(SpecialType.System_Boolean),
            _ => w.Compilation.GetTypeByMetadataName(a) ?? throw new Exception($"unit {un["name"]}: type argument {a} not found")
        };
        return def.Construct(tarr.Select(x => Arg((string)x)).ToArray());
    }
    /* the classes of TF.EX's that a configured (translate.toml, not auto) unit makes (a game type's DynamicData members by name look at
     * these subclasses of it too) */
    List<INamedTypeSymbol> classUnits;
    public List<INamedTypeSymbol> ConfiguredClassUnits()
    {
        if (classUnits != null) return classUnits;
        classUnits = new();
        var keys = UnitOfType.Where(kv => !kv.Value.ContainsKey("auto")).Select(kv => kv.Key).ToHashSet();
        foreach (var kv in UnitOfType.Where(kv => !kv.Value.ContainsKey("auto")))
            if (ConfiguredInst(kv.Value) is INamedTypeSymbol ci) classUnits.Add(ci);   // (typeargs units: the constructed class)
        // TF.EX's, and the code mods' (their own compilations: a mod's round logic, the test mode's, is a RoundLogic subclass too)
        foreach (var (cws, t) in new[] { ws }.Concat(fortrise != null ? new[] { fortrise } : Array.Empty<Workspace>()).Concat(mods.Values).SelectMany(w => w.Trees.Select(t => (w, t))).Where(x => keys.Any(k => k.StartsWith(x.t.FilePath + "|"))))
        {
            var sm = cws.Model(t);
            foreach (var d in t.GetRoot().DescendantNodes().OfType<Microsoft.CodeAnalysis.CSharp.Syntax.ClassDeclarationSyntax>())
                if (sm.GetDeclaredSymbol(d) is INamedTypeSymbol c && keys.Contains(t.FilePath + "|" + c.Name)) classUnits.Add(c);
        }
        return classUnits;
    }
    /* FortRise's ModuleSettings file for every translated settings class (a FortRise.ModuleSettings subclass: TF.EX's NetplaySettings, a
     * code mod's): FortRise saves the object's public properties as JSON and reads them back over the constructor's defaults. Per class,
     * Load_<unit> (the JSON text's members onto the object) and Save_<unit> (the object as JSON into a buffer: its length, -1 when it
     * doesn't fit), over the unit's generated accessors; bool, int, enum and string properties (others: a report line, not saved).
     * The file itself (sd:/xplay/<mod>.settings.json) is the runtime's (docs/PLAN-TFNX-2026-11-04.md section 10 step 3) */
    (string h, string c) ModSettings()
    {
        var h = new StringBuilder("/* GENERATED by tools/tfex/translator (CodeGen.ModSettings): FortRise's ModuleSettings as JSON, per settings class. Do not edit. */\n#pragma once\n\nnamespace tfex::gen::modsettings {\n");
        var c = new StringBuilder("/* GENERATED by tools/tfex/translator (CodeGen.ModSettings): FortRise's ModuleSettings as JSON, per settings class. Do not edit. */\n" +
                                  "#include \"tfexgen_modsettings.hpp\"\n#include \"tfexgen_code_rt.hpp\"\n#include \"../Json.hpp\"\n#include <cstring>\n");
        var body = new StringBuilder();
        foreach (var (unit, t) in emittedTypes.OrderBy(e => e.unit, StringComparer.Ordinal))
        {
            bool isSettings = false;
            for (var b = t.BaseType; b != null; b = b.BaseType) if (b.ToDisplayString() == "FortRise.ModuleSettings") isSettings = true;
            if (!isSettings) continue;
            var props = t.GetMembers().OfType<IPropertySymbol>()
                .Where(p => !p.IsStatic && p.DeclaredAccessibility == Accessibility.Public && p.GetMethod != null && p.SetMethod != null && !p.IsIndexer).ToList();
            c.Append($"#include \"tfexgen_{unit}.hpp\"\n");
            h.Append($"/* {t.ToDisplayString()} ({props.Count} properties) */\nbool Load_{unit}(void* obj, const char* text, int len);\nint Save_{unit}(void* obj, char* buf, int cap);\n");
            var ld = new StringBuilder(); var sv = new StringBuilder();
            foreach (var p in props)
            {
                var k = p.Name; var u = $"tfex::gen::{unit}";
                var pt = p.Type;
                if (pt.SpecialType == SpecialType.System_Boolean)
                { ld.Append($"    if (d.Get(r, \"{k}\")) {u}::set_{k}(obj, d.Bool(r, \"{k}\"));\n"); sv.Append($"    w.Bool(\"{k}\", {u}::get_{k}(obj));\n"); }
                else if (pt.SpecialType == SpecialType.System_Int32 || pt.TypeKind == TypeKind.Enum)
                {
                    var cast = "(int32_t)";   // (an enum's accessors take its underlying int32)
                    ld.Append($"    if (d.Get(r, \"{k}\")) {u}::set_{k}(obj, {cast}d.Int(r, \"{k}\"));\n"); sv.Append($"    w.Int(\"{k}\", (int64_t){u}::get_{k}(obj));\n");
                }
                else if (pt.SpecialType == SpecialType.System_String)
                {
                    ld.Append($"    if (d.Get(r, \"{k}\")) {{ static char t_[512]; int n_ = d.Str(r, \"{k}\", t_, sizeof t_); {u}::set_{k}(obj, gen::ManagedUtf8(t_, n_ < 0 ? 0 : (int)std::strlen(t_))); }}\n");
                    sv.Append($"    {{ static char t_[512]; t_[0] = 0; if (void* v_ = {u}::get_{k}(obj)) gen::Utf8Of(v_, t_, sizeof t_); w.String(\"{k}\", t_); }}\n");
                }
                else { Report.Add("settings: a property type not saved", $"{t.Name}.{k}: {pt.ToDisplayString()}"); continue; }
            }
            body.Append($"bool Load_{unit}(void* obj, const char* text, int len) {{\n    if (!obj || !text || len <= 0) return false;\n" +
                        $"    static tfex::json::Node nodes[{Math.Max(16, props.Count * 2 + 8)}];\n    tfex::json::Doc d{{nodes, (int)(sizeof nodes / sizeof *nodes), 0}};\n" +
                        "    if (!d.Parse(text, len) || !d.Root()) return false;\n    const tfex::json::Node* r = d.Root();\n")
                .Append(ld).Append("    return true;\n}\n")
                .Append($"int Save_{unit}(void* obj, char* buf, int cap) {{\n    if (!obj) return -1;\n    tfex::json::Writer w;\n    w.Init(buf, cap);\n    w.BeginObject();\n")
                .Append(sv).Append("    w.EndObject();\n    return w.ok && w.len < cap ? w.len : -1;\n}\n\n");
            Report.Add("settings: FortRise's ModuleSettings as JSON (CodeGen.ModSettings)", $"{t.ToDisplayString()}: {props.Count} properties");
        }
        h.Append("}\n");
        c.Append("\nnamespace tfex::gen::modsettings {\n").Append(body).Append("}\n");
        return (h.ToString(), c.ToString());
    }

    /* A boot per translated code mod (translate.toml [code_mods] entries with `mod`, its meta.json name), what FortRise's ModuleManager
     * does when it loads the mod's DLL: its registry (the SD folder's, glue SwitchMods.ModRegistry), its Mod subclass constructed with
     * its content, context and logger, then its Harmony patch classes on (HookGen's scan of its harmony.Patch calls), its postfixes on
     * load-time targets replayed ([hooks] load_time), and its IL edits' native patches when it has any (`native`). FrModsBoot.cpp boots
     * them in FortRise's order; mods/<name>/mod.toml `boot` names Boot_<mod> (docs/PLAN-TFNX-2026-11-04.md section 10) */
    (string h, string c) CodeModBoots(List<TomlTable> all)
    {
        var h = new StringBuilder("/* GENERATED by tools/tfex/translator (CodeGen.CodeModBoots): a boot per translated code mod. Do not edit. */\n#pragma once\n\nnamespace tfex::gen::codemods {\n");
        var c = new StringBuilder("/* GENERATED by tools/tfex/translator (CodeGen.CodeModBoots): a boot per translated code mod. Do not edit. */\n" +
                                  "#include \"tfexgen_codemods.hpp\"\n#include \"tfexgen_code_rt.hpp\"\n#include \"tfexgen_services.hpp\"\n#include \"tfexgen_hooks.hpp\"\n#include \"tfexgen_GlueFrMods.hpp\"\n#include \"tfexgen_modsettings.hpp\"\n#include \"tfexgen_frmods.hpp\"\n#include \"../../hwlog.hpp\"\n");
        var body = new StringBuilder();
        var defines = new StringBuilder();
        var natives = new List<string>();
        foreach (var (prefix, v) in cfg.Table("code_mods"))
        {
            if (v is not TomlTable mt || !mt.TryGetValue("mod", out var mo) || mo is not string mod) continue;
            var mine = all.Where(u => ((string)u["file"]).StartsWith(prefix)).ToList();
            // its Mod subclass (FortRise.Mod), as ModuleManager.LoadAssembly finds it: the unit translating it
            var module = mine.FirstOrDefault(u => emittedTypes.FirstOrDefault(e => e.unit == (string)u["name"]).type is INamedTypeSymbol t && t.BaseType?.ToDisplayString() == "FortRise.Mod");
            if (module == null) { Report.Add("code mod: PROBLEM", $"{mod}: no unit translates its FortRise.Mod subclass"); continue; }
            var classes = mine.Where(u => u.TryGetValue("hooks", out var hk) && hk is true).Select(u => emittedTypes.FirstOrDefault(e => e.unit == (string)u["name"]).type?.ToDisplayString() ?? (string)u["type"]).Distinct().OrderBy(x => x, StringComparer.Ordinal).ToList();   /* full names: hooks::Enable keys by them */
            var fn = "Boot_" + string.Concat(mod.Select(ch => char.IsLetterOrDigit(ch) ? ch : '_'));
            var native = mt.TryGetValue("native", out var nv) && nv is string ns ? ns : null;
            // its settings: FortRise's Mod.CreateSettings override, the ModuleSettings class it makes (its JSON load / save: ModSettings)
            var mtype = emittedTypes.FirstOrDefault(e => e.unit == (string)module["name"]).type;
            var cs = mtype?.GetMembers("CreateSettings").OfType<IMethodSymbol>().FirstOrDefault(x => x.IsOverride);
            string settingsUnit = null;
            if (cs?.DeclaringSyntaxReferences.FirstOrDefault()?.GetSyntax() is Microsoft.CodeAnalysis.SyntaxNode csn)
            {
                var csm = WsOf((string)module["file"]).Compilation.GetSemanticModel(csn.SyntaxTree, ignoreAccessibility: true);
                var st = csn.DescendantNodes().OfType<Microsoft.CodeAnalysis.CSharp.Syntax.ObjectCreationExpressionSyntax>().Select(o => csm.GetTypeInfo(o).Type).FirstOrDefault();
                settingsUnit = st == null ? null : emittedTypes.FirstOrDefault(e => SymbolEqualityComparer.Default.Equals(e.type, st)).unit;
                if (settingsUnit == null) Report.Add("code mod: PROBLEM", $"{mod}: CreateSettings makes {st?.ToDisplayString() ?? "?"}, which no unit translates");
            }
            if (native != null) natives.Add(native);
            h.Append($"void {fn}();   /* {mod} ({prefix}: {module["name"]}, {classes.Count} patch classes) */\n");
            defines.Append($"#define TFEX_CODEMOD_{fn[5..]} 1   /* {mod} is in this build (hand code that uses it tests this) */\n");
            c.Append($"#include \"tfexgen_{module["name"]}.hpp\"\n");
            var lit = string.Concat(mod.Select(ch => ch == '"' || ch == '\\' ? "\\" + ch : ch.ToString()));
            body.Append($"void {fn}() {{\n")
                .Append($"    static gen::LitN<{mod.Length + 1}> name{{u\"{lit}\"}};\n")
                .Append("    void* reg = tfex::gen::GlueFrMods::ModRegistry(name.P());\n")
                .Append($"    if (!reg) {{ hw::Log(\"[mods] {mod}: no registry (its folder isn't loaded): not constructed\"); return; }}\n")
                .Append("    void* content = tfex::gen::GlueFrMods::RegistryMod(reg);\n")
                .Append($"    tfex::gen::{module["name"]}::Ctor(content, content, reg, tfex::gen::svc::Service());   /* FortRise: Activator.CreateInstance(t, [content, context, logger]); its ILogger is the console's log (LoggerExtensions.Log* -> svc::Log) */\n")
                .Append(settingsUnit == null ? "" : $"    tfex::gen::frmods::AddModSettings(content, tfex::gen::{module["name"]}::CreateSettings(content), \"{lit}\", &tfex::gen::modsettings::Load_{settingsUnit}, &tfex::gen::modsettings::Save_{settingsUnit});   /* FortRise: its ModuleSettings, read from its file */\n")
                .Append($"    static const char* const kClasses[] = {{ {(classes.Count == 0 ? "nullptr" : string.Join(", ", classes.Select(x => $"\"{x}\"")))} }};\n")
                .Append($"    int on = 0, replayed = 0;\n    for (const char* cls : kClasses) if (cls) {{ on += tfex::gen::hooks::Enable(cls) ? 1 : 0; replayed += tfex::gen::hooks::ReplayLoadTime(cls); }}\n")
                .Append(native != null ? $"    {native}();   /* its IL edits as native patches */\n" : "")
                .Append($"    hw::Log(\"[mods] {mod} constructed: %d of {classes.Count} patch classes hooked, %d load-time postfixes replayed\", on, replayed);\n}}\n\n");
            Report.Add("code mod: a generated boot (FortRise's ModuleManager.LoadAssembly)", $"{mod}: {module["name"]}, {classes.Count} patch classes{(native != null ? ", native " + native : "")}");
        }
        foreach (var n in natives.Distinct())
        {
            var i = n.LastIndexOf("::");
            c.Append(i > 0 ? $"namespace {n[..i]} {{ void {n[(i + 2)..]}(); }}\n" : $"void {n}();\n");
        }
        h.Append("}\n"); h.Append(defines);
        c.Append("\nnamespace tfex::gen::codemods {\n").Append(body).Append("}\n");
        return (h.ToString(), c.ToString());
    }

    /* A game method that FortRise's MonoMod patch class declares (patch_LevelTiles.ReloadTiles: FortRise adds it to the game's LevelTiles,
     * so mods compiled against FortRise's TowerFall call it on the game type): the patch's member and its configured unit (a monomod unit),
     * for a call the Switch's binary has no body for */
    public (TomlTable unit, IMethodSymbol pm)? FortRisePatchMember(IMethodSymbol m)
    {
        if (fortrise == null || m.ContainingType == null) return null;
        var t = m.ContainingType;
        var pt = fortrise.Compilation.GetTypeByMetadataName($"{t.ContainingNamespace.ToDisplayString()}.patch_{t.Name}");
        if (pt == null) return null;
        var pm = pt.GetMembers(m.Name).OfType<IMethodSymbol>().FirstOrDefault(x => !x.IsExtern && x.Parameters.Length == m.Parameters.Length &&
                     x.Parameters.Zip(m.Parameters).All(z => z.First.Type.ToDisplayString() == z.Second.Type.ToDisplayString()) &&
                     !x.GetAttributes().Any(a => a.AttributeClass?.Name is "MonoModIgnore" or "MonoModIgnoreAttribute"));
        if (pm == null) return null;
        var f = pt.DeclaringSyntaxReferences.FirstOrDefault()?.SyntaxTree.FilePath;
        return f != null && UnitOfType.TryGetValue(f + "|" + pt.Name, out var u) ? (u, pm) : null;
    }

    /* FortRise's mod interop (IModInterop.GetApi<T>(mod)): the consumer mod's interface T over the object the provider mod's GetApi
     * returns, matched by member name and signature (FortRise proxies it; TF.EX's ApiSurfaceTests check the surfaces). translate.toml
     * [[interop]] pairs them; a call through the consumer's interface is a call of the provider class's generated member, with the
     * receiver the API object. Any mod's pair, not one rule per member (docs/PLAN-TFNX-2026-11-04.md section 9, gap 4) */
    Dictionary<INamedTypeSymbol, INamedTypeSymbol> interop;
    Dictionary<INamedTypeSymbol, string> interopAbsent;
    void LoadInterop()
    {
        if (interop != null) return;
        interop = new(SymbolEqualityComparer.Default); interopAbsent = new(SymbolEqualityComparer.Default);
        if (cfg.Root.TryGetValue("interop", out var v) && v is TomlTableArray arr)
            foreach (var t in arr)
            {
                // the consumer in any compilation that declares it (TF.EX's, a code mod's: Teams+'s copy of WiderSet's interface), the
                // provider likewise (TF.State's class, a Switch glue class standing in for a mod)
                var all = new[] { ws, fortrise }.Concat(mods.Values).Where(w => w != null).ToList();
                var pt = t.TryGetValue("provider", out var pv) ? all.Select(w => w.Compilation.GetTypeByMetadataName((string)pv)).FirstOrDefault(x => x != null) : null;
                foreach (var w in all)
                {
                    if (w.Compilation.GetTypeByMetadataName((string)t["consumer"]) is not INamedTypeSymbol ct) continue;
                    if (t.TryGetValue("absent", out var ab) && ab is string abs) interopAbsent[ct.OriginalDefinition] = abs;
                    else if (pt != null) interop[ct.OriginalDefinition] = pt;
                }
            }
    }
    public INamedTypeSymbol InteropProvider(INamedTypeSymbol consumer)
    {
        LoadInterop();
        return interop.TryGetValue(consumer.OriginalDefinition, out var p) ? p : null;
    }
    /* [[interop]] with `absent = "<why>"`: the provider mod isn't on the Switch (TF.Replay): its API is null, as FortRise's GetApi for a
     * mod that isn't loaded */
    public string InteropAbsent(INamedTypeSymbol consumer)
    {
        LoadInterop();
        return interopAbsent.TryGetValue(consumer.OriginalDefinition, out var w) ? w : null;
    }
    static string InteropSig(IMethodSymbol m) => m.ReturnType.ToDisplayString() + " " + m.Name + "(" + string.Join(", ", m.Parameters.Select(p => p.Type.ToDisplayString())) + ")";
    /* the provider's member answering the consumer interface's member: a public instance method of the same signature, or a property
     * of the same name and type */
    public ISymbol InteropImpl(INamedTypeSymbol consumer, ISymbol member)
    {
        if (InteropProvider(consumer) is not INamedTypeSymbol p) return null;
        for (var t = p; t != null; t = t.BaseType)
        {
            if (member is IMethodSymbol m &&
                t.GetMembers(m.Name).OfType<IMethodSymbol>().FirstOrDefault(x => !x.IsStatic && x.DeclaredAccessibility == Accessibility.Public && InteropSig(x) == InteropSig(m)) is IMethodSymbol pm)
                return pm;
            if (member is IPropertySymbol ps &&
                t.GetMembers(ps.Name).OfType<IPropertySymbol>().FirstOrDefault(x => !x.IsStatic && x.DeclaredAccessibility == Accessibility.Public && x.Type.ToDisplayString() == ps.Type.ToDisplayString()) is IPropertySymbol pp)
                return pp;
        }
        return null;
    }
    /* a code mod's source file ([code_mods]: its units' files start with the mod's key, "aijimmy/AI/AILogic.cs") */
    public bool IsCodeModFile(string path) => path != null && cfg.Table("code_mods").Any(kv => path.StartsWith(kv.Key));
    /* a generic method emitted only as copies per instantiation (MethodInsts), never as one shared body with its type arguments' runtime
     * types: TF.State's, the glue's and the code mods' static ones; FortRise's, static or instance (its T inside value types:
     * SpriteConfiguration<T>, typeof(T) checks) */
    public bool CopiesGeneric(IMethodSymbol m)
    {
        if (!m.IsGenericMethod) return false;
        var f = m.OriginalDefinition.DeclaringSyntaxReferences.FirstOrDefault()?.SyntaxTree.FilePath;
        if (f == null) return false;
        if (f.StartsWith("fortrise/") && !f.StartsWith("fortrise/glue/")) return m.MethodKind == MethodKind.Ordinary;
        return m.IsStatic && (f.StartsWith("state/") || f.StartsWith("glue/") || IsCodeModFile(f));
    }
    readonly Dictionary<string, List<INamedTypeSymbol>> frImpls = new();
    /* FortRise interfaces' translated implementations (by full name, from any compilation) */
    public List<INamedTypeSymbol> FrImplsOf(INamedTypeSymbol iface) { ImplsOf(iface); return frImpls.TryGetValue(iface.OriginalDefinition.ToDisplayString(), out var l) ? l : new(); }
    public List<INamedTypeSymbol> ImplsOf(INamedTypeSymbol iface)
    {
        if (impls == null)
        {
            impls = new(SymbolEqualityComparer.Default);
            var excl = new List<System.Text.RegularExpressions.Regex>();   // overrides.toml [scope.exclude]: never an implementation to call
            var ov = Path.Combine(cfg.Dir, "overrides.toml");
            if (File.Exists(ov) && Tomlyn.Toml.ToModel(File.ReadAllText(ov)) is TomlTable ot && ot.TryGetValue("scope", out var sc) && sc is TomlTable st &&
                st.TryGetValue("exclude", out var ex) && ex is TomlTable et)
                foreach (var (g, _) in et)
                    excl.Add(new("^" + System.Text.RegularExpressions.Regex.Escape(g).Replace("\\*\\*", ".*").Replace("\\*", "[^/]*") + (g.EndsWith("/") ? ".*" : "") + "$"));
            foreach (var t in ws.Trees.Where(t => (t.FilePath.StartsWith("network/") || t.FilePath.StartsWith("state/")) && !excl.Any(x => x.IsMatch(t.FilePath))))
            {
                var sm = ws.Model(t);
                foreach (var d in t.GetRoot().DescendantNodes().OfType<Microsoft.CodeAnalysis.CSharp.Syntax.ClassDeclarationSyntax>())
                    if (sm.GetDeclaredSymbol(d) is INamedTypeSymbol c && !c.IsAbstract)
                        foreach (var i in c.AllInterfaces) { if (!impls.TryGetValue(i.OriginalDefinition, out var l)) impls[i.OriginalDefinition] = l = new(); if (!l.Contains(c, SymbolEqualityComparer.Default)) l.Add(c); }
            }
            // FortRise's classes translate.toml makes units of ([[unit]], file under fortrise/): FortRise's own interfaces over them
            // (IModSubtextures -> ModSubtextures, ISubtextureEntry -> SubtextureEntry), as TF.EX's
            // (by full name: FortRise is its own compilation, its interfaces seen from TF.EX's or a mod's code are other symbols)
            foreach (var c in ConfiguredClassUnits().Where(c => !c.IsAbstract && c.DeclaringSyntaxReferences.Any(r => r.SyntaxTree.FilePath.StartsWith("fortrise/"))))
                foreach (var i in c.AllInterfaces.Where(i => i.DeclaringSyntaxReferences.Any(r => r.SyntaxTree.FilePath.StartsWith("fortrise/"))))
                { var k = i.OriginalDefinition.ToDisplayString(); if (!frImpls.TryGetValue(k, out var l)) frImpls[k] = l = new(); if (!l.Contains(c, SymbolEqualityComparer.Default)) l.Add(c); }
        }
        if (impls.TryGetValue(iface.OriginalDefinition, out var r)) return r;
        if (frImpls.TryGetValue(iface.OriginalDefinition.ToDisplayString(), out var fr)) return fr;
        return ModImplsOf(iface);
    }
    /* an interface a code mod declares: its implementations in every code mod, matched by full name. FortRise's interop proxies hand one
     * mod's object to another through each mod's own copy of the interface (AIJimmy's AILogic implements its copy of LoaderAI's
     * ILoaderAIModApi.IAgentLogic; LoaderAI calls it through its own) */
    Dictionary<string, List<INamedTypeSymbol>> modImpls;
    List<INamedTypeSymbol> ModImplsOf(INamedTypeSymbol iface)
    {
        var tree = iface.OriginalDefinition.DeclaringSyntaxReferences.FirstOrDefault()?.SyntaxTree;
        if (tree == null || !mods.Values.Any(w => w.Trees.Contains(tree))) return new();
        if (modImpls == null)
        {
            modImpls = new();
            foreach (var w in mods.Values)
                foreach (var t in w.Trees)
                {
                    var sm = w.Model(t);
                    foreach (var d in t.GetRoot().DescendantNodes().OfType<Microsoft.CodeAnalysis.CSharp.Syntax.ClassDeclarationSyntax>())
                        if (sm.GetDeclaredSymbol(d) is INamedTypeSymbol c && !c.IsAbstract)
                            foreach (var i in c.AllInterfaces)
                            {
                                var k = i.OriginalDefinition.ToDisplayString();
                                if (!modImpls.TryGetValue(k, out var l)) modImpls[k] = l = new();
                                if (!l.Contains(c, SymbolEqualityComparer.Default)) l.Add(c);
                            }
                }
        }
        return modImpls.TryGetValue(iface.OriginalDefinition.ToDisplayString(), out var r) ? r : new();
    }
    /* cls's implementation of an interface member, also when cls implements another mod's copy of that interface (ModImplsOf): the
     * copy's member of the same name and signature */
    public ISymbol ImplMember(INamedTypeSymbol cls, ISymbol member)
    {
        // (a constructed generic method, RegisterSprite<string>: its definition; Roslyn's lookup throws on the constructed one)
        if (member is IMethodSymbol { IsGenericMethod: true } gm0 && !SymbolEqualityComparer.Default.Equals(gm0, gm0.OriginalDefinition)) member = gm0.OriginalDefinition;
        if (cls.FindImplementationForInterfaceMember(member) is ISymbol s) return s;
        var name = member.ContainingType.OriginalDefinition.ToDisplayString();
        foreach (var i in cls.AllInterfaces.Where(i => i.OriginalDefinition.ToDisplayString() == name))
            foreach (var cm in i.GetMembers(member.Name))
                if ((member is IMethodSymbol m && cm is IMethodSymbol x && InteropSig(x) == InteropSig(m)) ||
                    (member is IPropertySymbol p && cm is IPropertySymbol y && y.Type.ToDisplayString() == p.Type.ToDisplayString()))
                    if (cls.FindImplementationForInterfaceMember(cm) is ISymbol s2) return s2;
        return null;
    }
    /* a constructed generic class of TF.EX's with concrete type arguments (TF.State's Sprite<int>, Sprite<string>): a unit per instantiation,
     * its type parameters substituted (UnitEmitter.subst); the generic definition itself stays one unit (T a placeholder) */
    public readonly Dictionary<string, INamedTypeSymbol> InstTypes = new();
    /* TF.State's generic methods called with concrete type arguments: unit name -> (definition, arguments), each emitted as its own copy */
    public readonly Dictionary<string, List<(IMethodSymbol def, ITypeSymbol[] args)>> MethodInsts = new();
    public void RequestMethodInst(TomlTable unit, IMethodSymbol def, ITypeSymbol[] args)
    {
        var n = (string)unit["name"];
        if (!MethodInsts.TryGetValue(n, out var l)) MethodInsts[n] = l = new();
        // (by the copy's name: the same type argument met as two symbols, TF.EX's and FortRise's compilations' `string`, is one copy)
        if (l.Any(x => SymbolEqualityComparer.Default.Equals(x.def, def) && UnitEmitter.MethodInstSuffix(x.args) == UnitEmitter.MethodInstSuffix(args))) return;
        l.Add((def, args));
        // (also while the unit itself is being emitted: a method asking for a copy of its own class's generic method, CalcExtensions'
        // OwnVectorShuffle -> OwnShuffle<Vector2>, arrives before "emitted" is set; the unit is emitted again with the copy)
        if (!Pending.Contains(unit)) Pending.Enqueue(unit);
    }
    public static string InstSuffix(INamedTypeSymbol t) =>
        t.IsGenericType && !SymbolEqualityComparer.Default.Equals(t, t.OriginalDefinition) && t.TypeArguments.All(a => a is not ITypeParameterSymbol && !ContainsTypeParam(a))
            ? "<" + string.Join(",", t.TypeArguments.Select(a => a.ToDisplayString())) + ">" : "";
    public static bool HasTypeParam(ITypeSymbol a) => ContainsTypeParam(a);
    static bool ContainsTypeParam(ITypeSymbol a) => a is ITypeParameterSymbol || (a is INamedTypeSymbol n && n.TypeArguments.Any(ContainsTypeParam)) || (a is IArrayTypeSymbol ar && ContainsTypeParam(ar.ElementType));
    public static string InstName(INamedTypeSymbol t) => InstSuffix(t).Length == 0 ? "" : "_" + string.Join("_", t.TypeArguments.Select(a => a.MetadataName.Replace("`", "")));
    TomlTable Emitting;   /* the unit being emitted now */
    /* every configured unit of a type (a class split over several, MainMenuPatch: NetplayMenus, MainMenuCreate, MainMenuVersusOptions);
     * UnitOfType keeps the last one, where members met later go */
    public readonly Dictionary<string, List<TomlTable>> UnitsOfType = new();
    public TomlTable AutoUnit(INamedTypeSymbol t, ISymbol member = null)
    {
        var f = t.DeclaringSyntaxReferences.FirstOrDefault()?.SyntaxTree.FilePath;
        if (f == null || !AutoUnits || t.IsAnonymousType || t.Name.Length == 0) return null;   // new { ... }: no unit (UnitEmitter reports it)
        var key = f + "|" + t.Name + InstSuffix(t);
        // a member a configured unit of a split class lists: that unit, so it and the static fields it uses live in one place (two units
        // each made their own copy of MainMenuPatch.copyCodeGuideEntity: one set it, the other's IsCopyCodeGuideVisible read its own, null)
        TomlTable owner = null;
        if (member != null && UnitsOfType.TryGetValue(key, out var all) && all.Count > 1)
        {
            var wanted = new List<string> { member.Name };
            if (member is IPropertySymbol wp) { if (wp.GetMethod != null) wanted.Add(wp.GetMethod.Name); if (wp.SetMethod != null) wanted.Add(wp.SetMethod.Name); }
            if (member is IMethodSymbol { AssociatedSymbol: IPropertySymbol ap }) wanted.Add(ap.Name);
            owner = all.FirstOrDefault(x => x.TryGetValue("members", out var mm) && mm is TomlArray ma && wanted.Any(w => ma.Contains(w)));
        }
        TomlTable u = owner;
        if (u == null && !UnitOfType.TryGetValue(key, out u))
        {
            if (Environment.GetEnvironmentVariable("TFEX_TRACE_AUTO") is string ta && ta == t.Name) Console.Error.WriteLine($"auto unit {t.Name}: {Environment.StackTrace}");
            var name = "Auto_" + t.Name + InstName(t);
            while (UnitOfType.Values.Any(x => (string)x["name"] == name)) name += "_";
            u = new TomlTable { ["name"] = name, ["file"] = f, ["type"] = t.Name, ["auto"] = true };
            // a FortRise MonoMod patch class of a game type (patch_SpriteData, patch_Atlas): no object of its own; its members run on the
            // game's object, its fields are the game's (hosted), as the configured patch units (hooks only where translate.toml says monomod)
            if (f.StartsWith("fortrise/") && t.Name.StartsWith("patch_") && t.BaseType is INamedTypeSymbol pb && pb.ContainingAssembly?.Name is "TowerFall" or "FNA") u["hosted"] = true;
            if (InstSuffix(t).Length > 0) u["typeargs"] = new TomlArray { };
            if (u.ContainsKey("typeargs")) foreach (var a in t.TypeArguments) ((TomlArray)u["typeargs"]).Add(a.ToDisplayString());
            if (u.ContainsKey("typeargs")) InstTypes[name] = t;
            bool instanceClass = t.TypeKind == TypeKind.Class && !t.IsStatic && !Models.IsModel(t) && t.GetMembers().Any(m => !m.IsStatic && !m.IsImplicitlyDeclared);
            if (!WholeAutoUnits && !instanceClass) u["members"] = new TomlArray();   // a class of ours needs all of itself (layout, vtable)
            UnitOfType[key] = u;
            Pending.Enqueue(u);
        }
        // an auto unit, or a configured unit of some members (translate.toml `members`): grows by what other units use of its type
        if (member != null && u.TryGetValue("members", out var ms) && ms is TomlArray arr)
        {
            var names = new List<string> { member.Name };
            if (member is IPropertySymbol ps) { if (ps.GetMethod != null) names.Add(ps.GetMethod.Name); if (ps.SetMethod != null) names.Add(ps.SetMethod.Name); }
            if (member is IMethodSymbol { MethodKind: MethodKind.Constructor }) names.Add(".ctor");
            foreach (var n in names)
                if (!arr.Contains(n)) { arr.Add(n); if ((u.ContainsKey("emitted") || ReferenceEquals(u, Emitting)) && !Pending.Contains(u)) Pending.Enqueue(u); }   // (a member the unit names for itself while emitting: by reflection, PlayerExtensions.ToActions)
        }
        return u;
    }

    /* overrides.toml [service]: a TF.EX member -> the Switch's (C++ of the hand-written services) or a fixed value with its ledger key */
    public readonly List<(string pattern, TomlTable rule)> Services = new();
    /* translate.toml [fortrise]: FortRise's additions to the game (they bind against the patched reference, the Switch's 1.4.1.6 has
     * none of them): "<Type>.<Member>" = { absent = true, key, why }. Reads give the default / an empty sequence; writes, calls on
     * the member and foreach over it do nothing: TF.EX's behaviour with no FortRise mod loaded. */
    public TomlTable AbsentRule(ISymbol m)
    {
        if (m?.ContainingType == null) return null;
        var id = m.ContainingType.ToDisplayString() + "." + m.Name;
        if (cfg.Table("fortrise").TryGetValue(id, out var v) && v is TomlTable t && t.TryGetValue("absent", out var ab) && ab is true) { Rules.Use("fortrise", id); return t; }
        // a whole type the Switch lacks (absent by its own name: the PC's GamepadConfig): every member of it too (no object of it exists)
        var tid = m.ContainingType.ToDisplayString();
        if (m is not INamedTypeSymbol && cfg.Table("fortrise").TryGetValue(tid, out var tv) && tv is TomlTable tt && tt.TryGetValue("absent", out var tab) && tab is true) { Rules.Use("fortrise", tid); return tt; }
        return null;
    }
    public TomlTable AbsentType(ITypeSymbol t)
    {
        if (t != null && cfg.Table("fortrise").TryGetValue(t.ToDisplayString(), out var v) && v is TomlTable tt && tt.ContainsKey("absent")) { Rules.Use("fortrise", t.ToDisplayString()); return tt; }
        return null;
    }
    public readonly RuleCheck Rules = new();
    /* the semantic model of a tree of either compilation (a FortRise struct's initializers, met through TF.EX's code: StructsGen.Canon) */
    /* a type of TF.EX's compilation by metadata name: the Switch's glue (TfexGlue.*), which a mod's units call through rewrites too */
    public INamedTypeSymbol MainType(string metadataName) => ws.Compilation.GetTypeByMetadataName(metadataName);
    /* a type of another compilation (a mod's: Teams+'s Vector2 from its FNA reference) as TF.EX's compilation has it, when it has it */
    public ITypeSymbol ToMain(ITypeSymbol t)
    {
        if (t.SpecialType != SpecialType.None) return ws.Compilation.GetSpecialType(t.SpecialType);
        if (t is IArrayTypeSymbol a) return ws.Compilation.CreateArrayTypeSymbol(ToMain(a.ElementType), a.Rank);
        if (t is not INamedTypeSymbol n || n.IsGenericType || SymbolEqualityComparer.Default.Equals(n.ContainingAssembly, ws.Compilation.Assembly)) return t;
        string Meta(INamedTypeSymbol x) => x.ContainingType != null ? Meta(x.ContainingType) + "+" + x.MetadataName
            : (x.ContainingNamespace.IsGlobalNamespace ? "" : x.ContainingNamespace.ToDisplayString() + ".") + x.MetadataName;
        return ws.Compilation.GetTypeByMetadataName(Meta(n)) ?? t;
    }
    public SemanticModel ModelOf(SyntaxTree t) => ws.Compilation.ContainsSyntaxTree(t) ? ws.Model(t)
        : mods.Values.FirstOrDefault(m => m.Compilation.ContainsSyntaxTree(t)) is Workspace mw ? mw.Model(t) : fortrise == null ? ws.Model(t) : fortrise.Model(t);

    /* overrides.toml [after] "<Type>.<Member>" = { cpp, include, key, why }: TF.EX's member is generated as usual and the Switch's code
     * runs on every way out of it (a gen::Defer at the top of the body): the Switch's own solution under TF.EX's logic (ClearNetplayMode:
     * TF.EX's variant restore, then the Switch's online-mode flag off) */
    public readonly Dictionary<string, TomlTable> Afters = new(StringComparer.Ordinal);
    public TomlTable AfterRule(IMethodSymbol m)
    {
        var id = m.ContainingType?.ToDisplayString() + "." + m.Name;
        if (!Afters.TryGetValue(id, out var r)) return null;
        Rules.Use("after", id); return r;
    }

    /* members whose rule lookup matched only a wildcard pattern (member -> pattern); RuleCheck counts the ones a unit emitted (Service) */
    public readonly Dictionary<string, string> WildPattern = new(StringComparer.Ordinal);
    public TomlTable ServiceRule(ISymbol m)
    {
        var id = m.ContainingType?.ToDisplayString() + "." + m.Name;
        // one overload: "<Type>.<Member>(<parameter types>)" (FortRise's IModSubtextures.RegisterTexture(string, FortRise.IResourceInfo, ...)), first
        if (m is IMethodSymbol ms && m.ContainingType != null)
        {
            var ps = string.Join(", ", ms.OriginalDefinition.Parameters.Select(x => x.Type.ToDisplayString()));
            foreach (var sid in new[] { $"{id}({ps})", $"{m.ContainingType.OriginalDefinition.ToDisplayString()}.{m.Name}({ps})" })
                foreach (var (p, r) in Services)
                    if (p == sid) { Rules.Use("service", p); return r; }
        }
        foreach (var (p, r) in Services)
            if (p == id || (p.EndsWith("*") && id.StartsWith(p[..^1]))) { Rules.Use("service", p); if (p != id) lock (WildPattern) WildPattern[id] = p; return r; }
        // a member of a generic type of ours, by its definition ("FortRise.RegistryQueue<T>.AddOrInvoke")
        if (m.ContainingType is { IsGenericType: true } gt && !SymbolEqualityComparer.Default.Equals(gt, gt.OriginalDefinition))
        {
            var did = gt.OriginalDefinition.ToDisplayString() + "." + m.Name;
            foreach (var (p, r) in Services)
                if (p == did) { Rules.Use("service", p); return r; }
        }
        // a member of a mod's interop interface whose provider the Switch doesn't carry ([[interop]] absent): FortRise's GetApi gives
        // null for a missing mod, so the consumer's calls (behind its null checks) answer the type's default
        if (m.ContainingType is INamedTypeSymbol ct && InteropAbsent(ct) is string why)
            return new TomlTable { ["noop"] = true, ["key"] = "S-absent", ["why"] = why };
        return null;
    }

    /* the bindings the hand-written runtime and glue name (bind::rt_*) */
    public void NeedRuntime()
    {
        Binder.NeedNamed("rt_String_InternalAllocateStr", "m", "System.String", "InternalAllocateStr", "int32", 0, 1);   // the runtime's string allocator
        Binder.NeedNamed("rt_MainMenu_Render", "m", "TowerFall.MainMenu", "Render", "", -1, 0);                      // tfexgen_glue.cpp's demo hook
        Binder.NeedNamed("rt_MainMenu_Update", "m", "TowerFall.MainMenu", "Update", "", -1, 0);                      // tfexgen_glue.cpp's notification test
        Binder.NeedNamed("rt_StringArray", "t", "System.String[]", "", "", -1, 0);
        Binder.NeedNamed("rt_ByteArray", "t", "System.Byte[]", "", "", -1, 0);                                        // byte[] (tfexgen_bcl.cpp: MemoryStream.ToArray, the log, UTF-8 bytes)                                    // string[] (gen::NewArr, gen::Split; C++ callers' params string[])
        // translate.toml [hand] need: game members hand-written code (tfex/*.cpp) takes from tfexgen_binding.hpp that no unit uses:
        // "<kind> <Type> [<Member> [<IL signature>]]" (kind f = field, m = method, t = type ...; the bind::<kind>_<Type>_<Member> name)
        if (cfg.Root.TryGetValue("hand", out var h) && h is TomlTable ht && ht.TryGetValue("need", out var nd) && nd is TomlArray na)
            foreach (var e in na.OfType<string>())
            {
                var p = e.Split(' ', 4, StringSplitOptions.RemoveEmptyEntries);
                Binder.Need(p[0], p[1], p.Length > 2 ? p[2] : "", p.Length > 3 ? p[3] : "");
            }
    }

    /* A code mod's entry points as units, read from its source ([code_mods] `auto = true`), as FortRise finds them in its DLL: its
     * FortRise.Mod subclass (hosted: constructed by its generated boot, CodeModBoots) and every class that registers Harmony patches
     * (a call of IHarmony / Harmony .Patch in it: its patch class, on with the mod). Everything else they use becomes a unit when called
     * (AutoUnit). Units the config lists for that file and type win. Names: the mod's struct prefix and the class name. */
    List<TomlTable> CodeModUnits(List<TomlTable> configured)
    {
        var have = configured.Select(x => (string)x["file"] + "|" + (string)x["type"]).ToHashSet();
        var outList = new List<TomlTable>();
        foreach (var (prefix, v) in cfg.Table("code_mods"))
        {
            if (v is not TomlTable mt || !mt.TryGetValue("auto", out var au) || au is not true || !mods.TryGetValue(prefix, out var mw)) continue;
            var sp = mt.TryGetValue("structs", out var spv) && spv is string sps ? sps : "Mod_";
            foreach (var tree in mw.Trees.Where(t => t.FilePath.StartsWith(prefix)))
            {
                var model = mw.Compilation.GetSemanticModel(tree, ignoreAccessibility: true);
                foreach (var cd in tree.GetRoot().DescendantNodes().OfType<Microsoft.CodeAnalysis.CSharp.Syntax.ClassDeclarationSyntax>())
                {
                    if (cd.Parent is Microsoft.CodeAnalysis.CSharp.Syntax.ClassDeclarationSyntax) continue;   // (nested: its outer class's)
                    if (model.GetDeclaredSymbol(cd) is not INamedTypeSymbol c) continue;
                    bool module = c.BaseType?.ToDisplayString() == "FortRise.Mod";
                    bool hooks = cd.DescendantNodes().OfType<Microsoft.CodeAnalysis.CSharp.Syntax.InvocationExpressionSyntax>().Any(inv =>
                        inv.Expression is Microsoft.CodeAnalysis.CSharp.Syntax.MemberAccessExpressionSyntax { Name.Identifier.ValueText: "Patch" } &&
                        model.GetOperation(inv) is Microsoft.CodeAnalysis.Operations.IInvocationOperation op && op.TargetMethod.ContainingType?.Name is "IHarmony" or "Harmony");
                    if (!module && !hooks) continue;
                    if (!have.Add(tree.FilePath + "|" + c.Name)) continue;
                    var un = new TomlTable { ["name"] = sp + c.Name, ["file"] = tree.FilePath, ["type"] = c.Name };
                    if (module) un["hosted"] = true;
                    if (hooks) { un["hooks"] = true; un["enable_all"] = false; }
                    outList.Add(un);
                    Report.Add("code mod: an entry point read from its source ([code_mods] auto)", $"{prefix}: {c.Name} ({(module ? "its Mod subclass" : "its Harmony patch class")})");
                }
            }
        }
        return outList;
    }

    public int Run(string outDir)
    {
        var units = cfg.Root.TryGetValue("unit", out var u) && u is TomlTableArray ta ? ta.ToList() : new List<TomlTable>();
        units.AddRange(CodeModUnits(units));
        foreach (var un in units)
        {
            var uk = UnitKey(un);
            UnitOfType[uk] = un;
            if (ConfiguredInst(un) is INamedTypeSymbol cinst) InstTypes[(string)un["name"]] = cinst;   // (typeargs: a generic class's instantiation)
            if (!UnitsOfType.TryGetValue(uk, out var ul)) UnitsOfType[uk] = ul = new();
            ul.Add(un);
        }
        Directory.CreateDirectory(outDir);
        var outputs = new List<(string file, string text)>();
        var replaced = (cfg.Root.TryGetValue("replace", out var rpv) && rpv is TomlTableArray rpa ? rpa : new TomlTableArray()).Select(r => (string)r["member"]).ToHashSet();
        var members = new SortedSet<string>(StringComparer.Ordinal);   // tfexgen_members.txt: the TF.EX members translated without a blocker
        // two passes: the first collects the game members used and binds them (bind.py); the second knows the binding (an
        // auto-property getter BRUTE inlined is read through its backing field instead)
        for (int pass = 0; pass < 2; pass++)
        {
            Report.Items.Clear();
            outputs.Clear();
            members.Clear();
            Binder.ClearRequests();
            NeedRuntime();
            foreach (var x in UnitOfType.Where(kv => kv.Value.ContainsKey("auto")).ToList()) UnitOfType.Remove(x.Key);
            MethodInsts.Clear(); InstTypes.Clear();
            foreach (var un in units) if (ConfiguredInst(un) is INamedTypeSymbol cinst2) InstTypes[(string)un["name"]] = cinst2;   // (configured instantiations stay)
            Pending.Clear();
            foreach (var unit in units) Pending.Enqueue(unit);
            var hooks = new HookGen(ws, cfg.Table("fortrise"));
            if (cfg.Table("hooks") is TomlTable ht && ht.TryGetValue("load_time", out var lt) && lt is TomlArray lta) foreach (var x in lta) hooks.LoadTime.Add((string)x);
            foreach (var mw in mods.Values) HookGen.ScanCalls(mw.Compilation);   // a mod's harmony.Patch(...) registrations
            UnitEmitter anyEm = null;
            emittedTypes.Clear();
            while (Pending.Count > 0)
            {
                var unit = Pending.Dequeue();
                var name = (string)unit["name"];
                var uws = WsOf((string)unit["file"]);
                if (uws.Tree((string)unit["file"]) == null) { Report.Add("unit's file not in this TF.EX tree (skipped)", name + ": " + unit["file"]); continue; }
                var em = new UnitEmitter(this, uws, cfg, unit, order);
                Emitting = unit;
                var (h, c) = em.Emit();
                Emitting = null;
                if (((string)unit["file"]).StartsWith("state/"))
                {   // TF.State's state path (the rollback measurement, docs/tfstate-rollback-measurement.md): what doesn't translate yet is a
                    // stub that logs its first hits (gen::StateGap) instead of failing the build; the report lists each one
                    c = c.Replace("TFEXGEN_UNSUPPORTED_EXPR(", "TFEXGEN_STATE_GAP_EXPR(").Replace("TFEXGEN_UNSUPPORTED;", "TFEXGEN_STATE_GAP;");
                    h = h.Replace("TFEXGEN_UNSUPPORTED_EXPR(", "TFEXGEN_STATE_GAP_EXPR(").Replace("TFEXGEN_UNSUPPORTED;", "TFEXGEN_STATE_GAP;");
                }
                unit["emitted"] = true;
                foreach (var sym in em.Methods.Where(mr => mr.Blockers.Count == 0).Select(mr => (ISymbol)mr.Method).Concat(em.DataMembers))
                    if (!replaced.Contains(sym.ContainingType.ToDisplayString() + "." + sym.Name)   // [[replace]]: the glue's body, not TF.EX's
                        && sym.DeclaringSyntaxReferences.FirstOrDefault() is SyntaxReference sr && !sr.SyntaxTree.FilePath.StartsWith("glue/"))
                    {
                        var ls = sr.GetSyntax().GetLocation().GetLineSpan();
                        var tn = sym.ContainingType.Name;
                        for (var ct = sym.ContainingType.ContainingType; ct != null; ct = ct.ContainingType) tn = ct.Name + "." + tn;
                        members.Add($"{sr.SyntaxTree.FilePath}\t{ls.StartLinePosition.Line + 1}\t{ls.EndLinePosition.Line + 1}\t{tn}.{sym.Name}");
                    }
                outputs.RemoveAll(o => o.file == $"tfexgen_{name}.hpp" || o.file == $"tfexgen_{name}.cpp");   // a re-emitted auto unit
                anyEm ??= em;
                if (unit.TryGetValue("monomod", out var mm) && mm is true) hooks.CollectMonoMod(em.Type, name, Config.Strings(unit, "members").ToList());   // FortRise's MonoMod patches
                if (unit.TryGetValue("hooks", out var hk) && hk is true && (uws == ws || mods.ContainsValue(uws)))
                {
                    if (unit.TryGetValue("enable_all", out var ea) && ea is false) hooks.OnDemand.Add(name);
                    hooks.Collect(em.Type, name, Config.Strings(unit, "exclude").ToList(), Config.Strings(unit, "targets").ToList(), Config.Strings(unit, "members").ToList());
                }
                if (unit.ContainsKey("auto")) Report.Add("auto unit (a TF.EX type the code calls)", $"{name}: {unit["file"]}");
                outputs.Add(($"tfexgen_{name}.hpp", h));
                outputs.Add(($"tfexgen_{name}.cpp", c));
                emittedTypes.RemoveAll(e => e.unit == name);
                if (em.Type != null) emittedTypes.Add((name, em.Type));
            }
            if (anyEm != null)
            {
                var (hh, hc) = hooks.Emit(anyEm.CppOf, anyEm.BodyOf, null, anyEm.FieldOf, Report, anyEm.TypeKeyOf);
                outputs.Add(("tfexgen_hooks.hpp", hh));
                outputs.Add(("tfexgen_hooks.cpp", hc));
                var (bh, bc) = CodeModBoots(units.Concat(UnitOfType.Values).Distinct().ToList());
                outputs.Add(("tfexgen_codemods.hpp", bh));
                outputs.Add(("tfexgen_codemods.cpp", bc));
                var (sh, sc) = ModSettings();
                outputs.Add(("tfexgen_modsettings.hpp", sh));
                outputs.Add(("tfexgen_modsettings.cpp", sc));
            }
            Binder.Resolve();
        }
        {   // the bindings TF.State's units use (state/... files, the generated MessagePack glue): unresolved ones are measurement gaps
            var stateNames = units.Concat(UnitOfType.Values).Distinct().Where(u => ((string)u["file"]).StartsWith("state/") || (string)u["name"] == "GlueStateMsgPack").Select(u => (string)u["name"]).ToHashSet();
            Binder.StateGapOk.Clear();
            foreach (var (f, t) in outputs)
                if (stateNames.Any(n => f == $"tfexgen_{n}.cpp" || f == $"tfexgen_{n}.hpp"))
                    foreach (System.Text.RegularExpressions.Match m in System.Text.RegularExpressions.Regex.Matches(t, @"bind::([a-z]+_[A-Za-z0-9_]+)")) Binder.StateGapOk.Add(m.Groups[1].Value);
        }
        File.WriteAllText(Path.Combine(outDir, "tfexgen_binding.hpp"), Binder.Header());
        outputs.Add(("tfexgen_structs.hpp", Structs.Header(Runtime.Label)));
        {   // TF.State's classes (the rollback measurement): EnsureClass of each, to call before a match (a class made inside the rollback
            // window is freed by a restore under the module's pointer)
            var st = units.Concat(UnitOfType.Values).Distinct().Where(u => u.ContainsKey("emitted") && (((string)u["file"]).StartsWith("state/") || (string)u["name"] == "GlueStateMsgPack"))
                .Select(u => (string)u["name"]).Distinct().Where(n => outputs.Any(o => o.file == $"tfexgen_{n}.hpp" && o.text.Contains("bool EnsureClass();"))).OrderBy(n => n, StringComparer.Ordinal).ToList();
            var sc = new System.Text.StringBuilder($"/* GENERATED by tools/tfex/translator ({Runtime.Label}): EnsureClass of every TF.State class ({st.Count}), for a menu (no rollback window) */\n");
            foreach (var n in st) sc.Append($"#include \"tfexgen_{n}.hpp\"\n");
            sc.Append("\nnamespace tfex::gen {\nint EnsureStateClasses() {\n    int ok = 0;\n");
            foreach (var n in st) sc.Append($"    ok += tfex::gen::{n}::EnsureClass() ? 1 : 0;\n");
            // and their statics made at first use (MakeStatics): made here too, outside any rollback window
            var ms = units.Concat(UnitOfType.Values).Distinct().Where(u => u.ContainsKey("emitted") && (((string)u["file"]).StartsWith("state/") || ((string)u["name"]).StartsWith("GlueState")))
                .Select(u => (string)u["name"]).Distinct().Where(n => outputs.Any(o => o.file == $"tfexgen_{n}.hpp" && o.text.Contains("void MakeStatics();"))).OrderBy(n => n, StringComparer.Ordinal).ToList();
            foreach (var n in ms.Where(n => !st.Contains(n))) sc.Insert(sc.ToString().IndexOf("\nnamespace tfex::gen {"), $"#include \"tfexgen_{n}.hpp\"\n");
            foreach (var n in ms) sc.Append($"    tfex::gen::{n}::MakeStatics();\n");
            sc.Append($"    return ok;   /* of {st.Count} */\n}}\n}}\n");
            outputs.Add(("tfexgen_state_classes.cpp", sc.ToString()));
        }
        // translate.toml [enums] export: TF.EX enums hand code reads (MenuState: the netplay screens' MainMenu states), each as unscoped
        // constants in a namespace of its name (gen::en::MenuState::LobbyBrowser, an int as the game's MainMenu.State), from TF.EX's source
        {
            var ex = new StringBuilder("/* GENERATED by tools/tfex/translator: TF.EX's enums the hand code reads (translate.toml [enums] export) */\n#pragma once\n#include <cstdint>\n\nnamespace tfex::gen::en {\n");
            if (cfg.Table("enums").TryGetValue("export", out var ev) && ev is TomlArray ea)
                foreach (var en in ea.OfType<string>())
                {
                    var et = ws.Compilation.GetTypeByMetadataName(en);
                    if (et is not { TypeKind: TypeKind.Enum }) { Report.Add("enums: not a TF.EX enum", en); continue; }
                    ex.Append($"/* {en} */\nnamespace {et.Name} {{ enum : int32_t {{ ");
                    ex.Append(string.Join(", ", et.GetMembers().OfType<IFieldSymbol>().Where(f => f.HasConstantValue).Select(f => $"{f.Name} = {f.ConstantValue}")));
                    ex.Append(" }; }\n");
                    if (et.DeclaringSyntaxReferences.FirstOrDefault()?.SyntaxTree is SyntaxTree etree)   // (in the ledger: upstream changes are generated)
                    {
                        var span = et.DeclaringSyntaxReferences[0].GetSyntax().GetLocation().GetLineSpan();
                        members.Add($"{etree.FilePath}\t{span.StartLinePosition.Line + 1}\t{span.EndLinePosition.Line + 1}\t{et.Name}.<type>");
                    }
                }
            outputs.Add(("tfexgen_enums.hpp", ex.Append("}\n").ToString()));
        }
        // TF.EX's version (network/TF.EX.Core/meta.json "version", what FortRise reads and TF.EX's IAutoUpdater.GetCurrentVersion returns):
        // the matchmaking URL's ?version= and the version texts
        {
            var meta = Path.Combine(ws.SrcRoot, "network", "TF.EX.Core", "meta.json");
            var ver = File.Exists(meta) ? System.Text.Json.JsonDocument.Parse(File.ReadAllText(meta)).RootElement.GetProperty("version").GetString() : null;
            if (ver == null) Report.Add("version: no network/TF.EX.Core/meta.json version", meta);
            outputs.Add(("tfexgen_version.hpp", $"/* GENERATED by tools/tfex/translator from TF.EX's network/TF.EX.Core/meta.json: the TF.EX version this build is */\n#pragma once\n#define TFEX_VERSION \"{ver ?? "0.0.0"}\"\n"));
        }
        // the mods built into the module (translate.toml [mods]): their FortRise meta.json as TF.EX ships it (builtin, paths under
        // vendor/tfex/src), FortRise's own (vendor/fortrise/version.txt) and the ones the Switch carries its own way (native: inline);
        // runtime/tfexgen_mods.cpp's registry reads them at boot, as FortRise reads a mod folder's meta.json
        {
            var mods = cfg.Table("mods");
            var sb = new StringBuilder("/* GENERATED by tools/tfex/translator (translate.toml [mods]): the FortRise metadata of the mods built into the module */\n#pragma once\n");
            sb.Append("namespace tfex::mods { struct BuiltinMod { const char* source; const char* json; };\ninline constexpr BuiltinMod kBuiltin[] = {\n");
            int nb = 0;
            void Add(string source, string json) { sb.Append($"    {{ \"{source}\", R\"tfexmeta({json.Trim()})tfexmeta\" }},\n"); nb++; }
            var frVer = Path.GetFullPath(Path.Combine(cfg.Dir, "..", "..", "vendor", "fortrise", "version.txt"));   // (as Program.cs finds vendor/fortrise)
            if (File.Exists(frVer))
                Add("vendor/fortrise/version.txt", System.Text.Json.JsonSerializer.Serialize(new Dictionary<string, string> {
                    ["name"] = "FortRise", ["version"] = File.ReadAllText(frVer).Trim(), ["displayName"] = "FortRise", ["author"] = "FortRise",
                    ["description"] = "The Switch's FortRise layer: content mods' archers, sprites and textures from the SD card (FortRise's loaders, translated)" }));
            else Report.Add("mods: no vendor/fortrise/version.txt", frVer);
            if (mods.TryGetValue("builtin", out var bl) && bl is TomlArray ba)
                foreach (var rel in ba.OfType<string>())
                {
                    var f = Path.Combine(ws.SrcRoot, rel);
                    if (File.Exists(f)) Add("vendor/tfex/src/" + rel, File.ReadAllText(f)); else Report.Add("mods: builtin meta.json missing", rel);
                }
            if (mods.TryGetValue("fortrise", out var fl) && fl is TomlArray fa)
                foreach (var rel in fa.OfType<string>())
                {
                    var f = Path.GetFullPath(Path.Combine(cfg.Dir, "..", "..", "vendor", "fortrise", rel));
                    if (File.Exists(f)) Add("vendor/fortrise/" + rel, File.ReadAllText(f)); else Report.Add("mods: fortrise meta.json missing", rel);
                }
            if (mods.TryGetValue("native", out var nv) && nv is TomlTable nt)
                foreach (var (k, v) in nt) if (v is string js) Add("translate.toml [mods.native] " + k, js);
            sb.Append("};\ninline constexpr int kBuiltinCount = " + nb + ";\n}\n");
            outputs.Add(("tfexgen_mods_builtin.hpp", sb.ToString()));
        }
        foreach (var (iface, v) in cfg.Table("dispatch")) if (v is TomlTable dt) outputs.Add(Dispatch(iface, dt, outputs));
        outputs.Add(("tfexgen_members.txt", "# GENERATED by tools/tfex/translator: every TF.EX member translated without a blocker (file, first and last line,\n" +
                      "# Type.Member). Upstream changes to these arrive by re-running the translator; tools/tfex/update.py reads this list.\n" + string.Join("\n", members) + "\n"));
        foreach (var (f, t) in outputs) File.WriteAllText(Path.Combine(outDir, f), t);
        Runtime.RemoveCopies(outDir);
        // the folder is the translator's: code units from an earlier run that this run didn't produce go (models stay)
        var keep = new HashSet<string>(outputs.Select(o => o.file)) { "tfexgen_binding.hpp", "tfexgen_report.txt", "tfexgen_Models.hpp", "tfexgen_Models.cpp" };
        foreach (var f in Directory.EnumerateFiles(outDir, "tfexgen_*"))
            if (!keep.Contains(Path.GetFileName(f))) { File.Delete(f); Console.Error.WriteLine("code: removed stale " + Path.GetFileName(f)); }
        foreach (var (k, e) in Binder.Unresolved) Report.Add("unresolved game member", k + ": " + e);
        foreach (var p in Binder.PinsUsed) Report.Add("hand rule: pinned game member (translate.toml [pin])", p);
        int interop = Rules.CheckInterop(ws, cfg, Report, new[] { fortrise }.Concat(mods.Values));
        Console.Error.WriteLine($"code: {units.Count} units -> {outDir}");
        Report.Print(Console.Error);
        File.WriteAllText(Path.Combine(outDir, "tfexgen_report.txt"), Report.Text());
        if (Environment.GetEnvironmentVariable("TFEX_RULES_OUT") is string ro && ro.Length > 0) return Rules.Write(ws, cfg, Services, units, ro);
        if (interop > 0) { Console.Error.WriteLine($"code: {interop} interop problem(s) (translate.toml [[interop]]): see the report"); return 4; }
        return 0;
    }
}

/* What the translator could not do mechanically, by construct: hand rules applied and unsupported constructs. */
public sealed class Report
{
    public readonly SortedDictionary<string, List<string>> Items = new(StringComparer.Ordinal);
    public void Add(string construct, string where)
    {
        if (!Items.TryGetValue(construct, out var l)) Items[construct] = l = new List<string>();
        if (!l.Contains(where)) l.Add(where);
    }
    public string Text()
    {
        var sb = new StringBuilder();
        // sections and their entries in ordinal order: the file doesn't depend on the order units were translated in (fewer merge conflicts)
        foreach (var (k, l) in Items) { sb.Append($"{k} ({l.Count})\n"); foreach (var w in l.OrderBy(x => x, StringComparer.Ordinal)) sb.Append("    " + w + "\n"); }
        return sb.ToString();
    }
    public void Print(TextWriter w) { foreach (var (k, l) in Items) w.WriteLine($"  {k}: {l.Count}" + (l.Count <= 3 ? " (" + string.Join("; ", l) + ")" : "")); }
}

/* Overload order of non-TowerFall types (corlib, FNA) as the Switch's binary has them: the declaring assembly's metadata order of
 * Mono's mscorlib / FNA (translate.toml [refs] mono). BRUTE emits method records in IL order (tools/re sigs.py). */
public sealed class MetadataOrder
{
    readonly CSharpCompilation mono;
    public MetadataOrder(Config cfg)
    {
        var refs = new List<MetadataReference>();
        var r = cfg.Table("refs");
        foreach (var f in Config.Strings(r, "mono").Select(Config.Expand).Where(File.Exists))
            refs.Add(MetadataReference.CreateFromFile(f));
        // private members too: BRUTE's records include them, so the overload index counts them
        mono = CSharpCompilation.Create("mono", references: refs, options: new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary, metadataImportOptions: MetadataImportOptions.All));
    }

    /* (index among the same-named methods, their count); kPub/countPub among the public ones (the Switch's FNA has extra internal
     * overloads the shipped PC FNA lacks: bind.py falls back to public-only when the counts disagree) */
    public (int k, int count, int kPub, int countPub) Of4(IMethodSymbol m)
    {
        var t = m.ContainingType.OriginalDefinition;
        var mt = mono.GetTypeByMetadataName(MetadataName(t));
        if (mt == null) return (-1, 0, -1, 0);
        var same = mt.GetMembers(m.Name).OfType<IMethodSymbol>().ToList();
        var pub = same.Where(x => x.DeclaredAccessibility == Accessibility.Public).ToList();
        // without nullable annotations: FortRise is compiled with them (string?), Mono's metadata has none
        static string D(ITypeSymbol t) => t.WithNullableAnnotation(NullableAnnotation.None).ToDisplayString(SymbolDisplayFormat.CSharpErrorMessageFormat.WithMiscellaneousOptions(SymbolDisplayMiscellaneousOptions.UseSpecialTypes));
        var want = string.Join(",", m.OriginalDefinition.Parameters.Select(p => D(p.Type)));
        string S(IMethodSymbol x) => string.Join(",", x.Parameters.Select(p => D(p.Type)));
        return (same.FindIndex(x => S(x) == want), same.Count, pub.FindIndex(x => S(x) == want), pub.Count);
    }

    public (int k, int count) Of(IMethodSymbol m)
    {
        var t = m.ContainingType.OriginalDefinition;
        var meta = MetadataName(t);
        var mt = mono.GetTypeByMetadataName(meta);
        if (mt == null) return (-1, 0);
        var same = mt.GetMembers(m.Name).OfType<IMethodSymbol>().ToList();
        var want = string.Join(",", m.OriginalDefinition.Parameters.Select(p => p.Type.ToDisplayString()));
        for (int i = 0; i < same.Count; i++)
            if (string.Join(",", same[i].Parameters.Select(p => p.Type.ToDisplayString())) == want) return (i, same.Count);
        return (-1, same.Count);
    }

    static string MetadataName(INamedTypeSymbol t) =>
        t.ContainingType != null ? MetadataName(t.ContainingType) + "+" + t.MetadataName
                                 : (t.ContainingNamespace.IsGlobalNamespace ? "" : t.ContainingNamespace.ToDisplayString() + ".") + t.MetadataName;
}
