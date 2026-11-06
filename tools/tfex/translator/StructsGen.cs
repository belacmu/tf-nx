// TF.EX value types that are plain data (NetworkStats, PingStats ...): C++ PODs in tfexgen_structs.hpp (namespace tfex::gen::st), the
// same fields in the same order (they are [StructLayout(Sequential)] where TF.EX hands them to native code). Every such struct of the
// compilation is emitted, used or not, so hand-written code (tfexgen_services.hpp) can name them. A struct qualifies when each instance
// field (auto-properties included, by the property's name) is a number, bool, char, enum, FNA value type, a reference (void*) or another
// such struct;
// methods on a struct are a unit's business, not this file's.
using System.Text;
using Microsoft.CodeAnalysis;

namespace TfexTranslator;

public sealed class StructsGen
{
    readonly Workspace ws;
    List<INamedTypeSymbol> pods;
    readonly Dictionary<INamedTypeSymbol, string> names = new(SymbolEqualityComparer.Default);
    readonly Dictionary<INamedTypeSymbol, bool> memo = new(SymbolEqualityComparer.Default);

    /* an FNA value type the runtime has a C++ struct for (gen::Rectangle ...): a value collection may hold it */
    public static bool IsFnaValue(ITypeSymbol t) => Fna.ContainsKey(t.ToDisplayString());
    static readonly Dictionary<string, string> Fna = new()
    {
        ["Microsoft.Xna.Framework.Vector2"] = "gen::Vector2", ["Microsoft.Xna.Framework.Color"] = "gen::Color",
        ["Microsoft.Xna.Framework.Rectangle"] = "gen::Rectangle", ["Microsoft.Xna.Framework.Point"] = "gen::Point",
        ["TowerFall.PlayerInventory"] = "gen::PlayerInventory",   // the Switch's 16 bytes: Arrows, then 4 bools (Player..ctor x4/x5, Session.GetPlayerInventory x0/x1)
    };

    /* FortRise's compilation (vendor/fortrise): its structs too, named "Fr_<Name>"; closed generic structs (SpriteConfiguration<string>,
     * Option<HairInfo>) on demand, their fields substituted, named "<Name>_<argument names>" */
    readonly Workspace fortrise;
    /* FortRise code mods' compilations (vendor/teamplus) and their struct name prefixes ("Tp_"): their structs too, as FortRise's */
    readonly List<(Workspace ws, string tag)> mods;
    public StructsGen(Workspace ws, Workspace fortrise = null, List<(Workspace, string)> mods = null) { this.ws = ws; this.fortrise = fortrise; this.mods = mods ?? new(); }
    string ModTag(ITypeSymbol t) => mods.FirstOrDefault(m => SymbolEqualityComparer.Default.Equals(t.ContainingAssembly, m.ws.Compilation.Assembly)).tag;
    bool Ours(ITypeSymbol t) => SymbolEqualityComparer.Default.Equals(t.ContainingAssembly, ws.Compilation.Assembly) ||
                                (fortrise != null && SymbolEqualityComparer.Default.Equals(t.ContainingAssembly, fortrise.Compilation.Assembly)) || ModTag(t) != null;
    bool IsFortRise(ITypeSymbol t) => fortrise != null && SymbolEqualityComparer.Default.Equals(t.ContainingAssembly, fortrise.Compilation.Assembly);
    static bool Closed(ITypeSymbol t) => t switch
    {
        ITypeParameterSymbol => false,
        IArrayTypeSymbol a => Closed(a.ElementType),
        INamedTypeSymbol n => n.TypeArguments.All(Closed),
        _ => true,
    };
    string ArgName(ITypeSymbol t) => t switch
    {
        IArrayTypeSymbol a => ArgName(a.ElementType) + "Arr",
        INamedTypeSymbol n when n.SpecialType != SpecialType.None => n.SpecialType.ToString().Replace("System_", "").ToLowerInvariant(),
        INamedTypeSymbol n => n.Name + string.Concat(n.TypeArguments.Select(x => "_" + ArgName(x))),
        _ => t.Name,
    };
    string NameOf(INamedTypeSymbol t) => (IsFortRise(t) ? "Fr_" : ModTag(t) ?? "") + t.Name + string.Concat(t.TypeArguments.Select(x => "_" + ArgName(x)));

    IEnumerable<INamedTypeSymbol> AllTypes(INamespaceSymbol ns)
    {
        foreach (var t in ns.GetTypeMembers()) { yield return t; foreach (var n in Nested(t)) yield return n; }
        foreach (var sub in ns.GetNamespaceMembers()) foreach (var t in AllTypes(sub)) yield return t;
    }
    static IEnumerable<INamedTypeSymbol> Nested(INamedTypeSymbol t)
    {
        foreach (var n in t.GetTypeMembers()) { yield return n; foreach (var m in Nested(n)) yield return m; }
    }

    void Prepare()
    {
        if (pods != null) return;
        pods = AllTypes(ws.Compilation.Assembly.GlobalNamespace).Where(t => t.TypeKind == TypeKind.Struct && Qualifies(t)).ToList();
        foreach (var g in pods.GroupBy(t => t.Name))
            foreach (var t in g)
                names[t] = g.Count() == 1 ? t.Name : (t.ContainingType != null ? t.ContainingType.Name + "_" : "") +
                           string.Join("_", t.ContainingNamespace.ToDisplayString().Split('.').Where(s => s is not ("TF" or "Domain" or "Models"))) + "_" + t.Name;
    }

    public static IEnumerable<IFieldSymbol> Fields(INamedTypeSymbol t) =>
        t.GetMembers().OfType<IFieldSymbol>().Where(f => !f.IsStatic && !f.IsConst);
    public static string FieldName(IFieldSymbol f) => f.AssociatedSymbol is IPropertySymbol p ? p.Name : f.Name;

    bool Qualifies(INamedTypeSymbol t)
    {
        if (memo.TryGetValue(t, out var r)) return r;
        memo[t] = false;   // a cycle doesn't qualify
        r = (!t.IsGenericType || (!t.IsUnboundGenericType && Closed(t) && !SymbolEqualityComparer.Default.Equals(t, t.OriginalDefinition))) && Ours(t) &&
            Fields(t).Any() && Fields(t).All(f => FieldOk(f.Type));
        memo[t] = r;
        return r;
    }
    bool FieldOk(ITypeSymbol t) =>
        t.SpecialType is SpecialType.System_Boolean or SpecialType.System_Char or SpecialType.System_SByte or SpecialType.System_Byte or
            SpecialType.System_Int16 or SpecialType.System_UInt16 or SpecialType.System_Int32 or SpecialType.System_UInt32 or
            SpecialType.System_Int64 or SpecialType.System_UInt64 or SpecialType.System_Single or SpecialType.System_Double or
            SpecialType.System_IntPtr or SpecialType.System_UIntPtr ||
        t.TypeKind == TypeKind.Enum || Fna.ContainsKey(t.ToDisplayString()) || UnitEmitter.GameValueTypes.ContainsKey(t.ToDisplayString()) ||
        // a reference (string, a game object): void*, as everywhere in generated code. Kept on the stack, in a struct parameter, or in
        // scanned memory (gen::ValMap/VList, coroutine frames) the GC sees it; a static of such a struct in module memory isn't scanned
        // (a delegate too: a managed object, void* like any reference; TF.EX's ScenarioSweeper.Run holds its Launch / Expect)
        (t.IsReferenceType && t is not IArrayTypeSymbol) ||
        (t is IArrayTypeSymbol { Rank: 1 } fa && FieldOk(fa.ElementType)) ||   // an array (FortRise's SpriteConfiguration<T>.Animations): gen::Arr of its elements
        (t is INamedTypeSymbol n && n.TypeKind == TypeKind.Struct && Qualifies(n));

    /* a FortRise type as TF.EX's compilation sees it (its FortRise.dll reference: SpriteConfiguration<string> that TF.EX's
     * SkinOverlayService builds) -> the same type of FortRise's own compilation (the source the translator translates FortRise from), its
     * type arguments mapped: one C++ struct for both sides */
    public ITypeSymbol Canon(ITypeSymbol t)
    {
        if (fortrise == null || t is not INamedTypeSymbol n || IsFortRise(n) || n.ContainingNamespace?.ToDisplayString() is not string ns ||
            !(ns == "FortRise" || ns.StartsWith("FortRise.")) || n.DeclaringSyntaxReferences.Length > 0)
            return t;
        var def = fortrise.Compilation.GetTypeByMetadataName(MetaName(n.OriginalDefinition));
        if (def == null) return t;
        if (!n.IsGenericType) return def;
        var args = n.TypeArguments.Select(MapArg).ToArray();
        return args.Any(x => x == null) ? t : def.Construct(args);
    }
    ITypeSymbol MapArg(ITypeSymbol a) => a switch
    {
        { SpecialType: not SpecialType.None } => fortrise.Compilation.GetSpecialType(a.SpecialType),
        IArrayTypeSymbol arr => MapArg(arr.ElementType) is ITypeSymbol e ? fortrise.Compilation.CreateArrayTypeSymbol(e, arr.Rank) : null,
        INamedTypeSymbol na when Canon(na) is ITypeSymbol c && !SymbolEqualityComparer.Default.Equals(c, na) => c,
        INamedTypeSymbol na => fortrise.Compilation.GetTypeByMetadataName(MetaName(na)),
        _ => null,
    };
    static string MetaName(INamedTypeSymbol t) =>
        t.ContainingType != null ? MetaName(t.ContainingType) + "+" + t.MetadataName
                                 : (t.ContainingNamespace.IsGlobalNamespace ? "" : t.ContainingNamespace.ToDisplayString() + ".") + t.MetadataName;
    /* the same property on the canonical type (an auto-property's test needs its syntax) */
    public IPropertySymbol CanonProperty(IPropertySymbol p) =>
        Canon(p.ContainingType) is INamedTypeSymbol c && !SymbolEqualityComparer.Default.Equals(c, p.ContainingType)
            ? c.GetMembers(p.Name).OfType<IPropertySymbol>().FirstOrDefault() : null;

    public bool IsPod(ITypeSymbol t)
    {
        Prepare();
        t = Canon(t);
        if (t is not INamedTypeSymbol n || n.TypeKind != TypeKind.Struct) return false;
        if (pods.Contains(n, SymbolEqualityComparer.Default)) return true;
        // a FortRise struct, or a closed generic struct of ours, first seen now (only the used ones are emitted)
        if ((IsFortRise(n) || ModTag(n) != null || n.IsGenericType) && Closed(n) && Ours(n) && Qualifies(n)) { pods.Add(n); names[n] = NameOf(n); return true; }
        return false;
    }
    public string CppName(ITypeSymbol t) { Prepare(); t = Canon(t); IsPod(t); return "tfex::gen::st::" + names[(INamedTypeSymbol)t]; }

    string FieldCpp(ITypeSymbol t) => t is IArrayTypeSymbol fa ? $"gen::Arr<{FieldCpp(fa.ElementType)}>" : t.SpecialType switch
    {
        SpecialType.System_Boolean => "bool", SpecialType.System_Char => "char16_t", SpecialType.System_SByte => "int8_t",
        SpecialType.System_Byte => "uint8_t", SpecialType.System_Int16 => "int16_t", SpecialType.System_UInt16 => "uint16_t",
        SpecialType.System_Int32 => "int32_t", SpecialType.System_UInt32 => "uint32_t", SpecialType.System_Int64 => "int64_t",
        SpecialType.System_UInt64 => "uint64_t", SpecialType.System_Single => "float", SpecialType.System_Double => "double",
        SpecialType.System_IntPtr => "intptr_t", SpecialType.System_UIntPtr => "uintptr_t",
        _ => t.TypeKind == TypeKind.Enum ? FieldCpp(((INamedTypeSymbol)t).EnumUnderlyingType) : t.IsReferenceType ? "void*" : Fna.TryGetValue(t.ToDisplayString(), out var f) ? f : UnitEmitter.GameValueTypes.TryGetValue(t.ToDisplayString(), out var gv) ? $"gen::GameVal<{gv}>" : (IsPod(t) ? "::tfex::gen::st::" + names[(INamedTypeSymbol)t] : "/*?*/ void*"),   // qualified: a field may share its type's name (ActionResult.Status)
    };

    public string Header(string label)
    {
        Prepare();
        var sb = new StringBuilder($"/* GENERATED by tools/tfex/translator from TF.EX (vendor/tfex) {label}: do not edit; re-run tools/tfex/translate.sh.\n" +
                                   " * TF.EX's plain-data value types (translator/StructsGen.cs). */\n#pragma once\n#include \"tfexgen_code_rt.hpp\"\n\nnamespace tfex::gen::st {\n");
        var done = new HashSet<string>(StringComparer.Ordinal);   // by C++ name: one type met as two symbols (a configured instantiation unit's and a
        void Emit(INamedTypeSymbol t)                            // caller's, SpriteConfiguration<string>) is one struct
        {
            if (!done.Add(names[t])) return;
            foreach (var f in Fields(t)) if ((f.Type is IArrayTypeSymbol fa0 ? fa0.ElementType : f.Type) is INamedTypeSymbol n && IsPod(n)) Emit(n);
            if (IsFortRise(t)) sb.Append("/* FortRise (vendor/fortrise, MIT) */\n");
            var path = t.DeclaringSyntaxReferences.FirstOrDefault()?.SyntaxTree.FilePath ?? "";
            var i = path.IndexOf("/src/"); if (i >= 0) path = path[(i + 5)..];
            sb.Append($"struct {names[t]} {{   /* {t.ToDisplayString()} ({path}) */\n");
            foreach (var f in Fields(t)) sb.Append($"    {FieldCpp(f.Type)} {FieldName(f)};\n");
            sb.Append("};\n");
        }
        foreach (var t in pods.ToList().OrderBy(t => names[t], StringComparer.Ordinal)) Emit(t);
        return sb.Append("} // namespace tfex::gen::st\n").ToString();
    }
}
