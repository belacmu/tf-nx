// Stage 1: TF.EX's lobby-server models (TF.EX.Domain/Models/WebSocket/**) -> C++ structs + JSON read/write, as TF.EX's
// MessagePack does it (keyAsPropertyName / [DataMember(Name)] keys, declaration order, null where C# holds a reference).
// Output: tfexgen_Models.hpp / .cpp in namespace tfex::gen::ws. Capacities: translate.toml [models] / [models.caps].
using System.Text;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using Tomlyn.Model;

namespace TfexTranslator;

public sealed class ModelsGen
{
    readonly Workspace ws;
    readonly Config cfg;
    readonly TomlTable caps;
    readonly int defString, defList, defMap, defMapKey, defMapValue;
    public const string Dir = "network/TF.EX.Domain/Models/WebSocket/";
    public readonly List<string> Notes = new();
    readonly Dictionary<INamedTypeSymbol, ClassInfo> classes = new(SymbolEqualityComparer.Default);
    readonly List<INamedTypeSymbol> enums = new();

    public ModelsGen(Workspace ws, Config cfg)
    {
        this.ws = ws;
        this.cfg = cfg;
        var m = cfg.Table("models");
        caps = m.TryGetValue("caps", out var c) && c is TomlTable t ? t : new TomlTable();
        defString = (int)cfg.Int(m, "string", 64);
        defList = (int)cfg.Int(m, "list", 8);
        defMap = (int)cfg.Int(m, "map", 8);
        defMapKey = (int)cfg.Int(m, "map_key", 24);
        defMapValue = (int)cfg.Int(m, "map_value", 48);
    }

    public sealed class Member
    {
        public IPropertySymbol Prop;
        public string Name, Key, CppType, Init;
        public PropertyDeclarationSyntax Syntax;
        public bool Local;   /* [IgnoreMember] with a setter: the client's own state (Lobby.CanJoin), in the struct, never in JSON */
    }

    public sealed class ClassInfo
    {
        public INamedTypeSymbol Type;
        public List<Member> Members = new();
        public List<(string name, string type, string value)> Consts = new();
        public List<INamedTypeSymbol> Deps = new();
        public string File;
    }

    /* for code units: which TF.EX classes are lobby models, and how each serialized member is held */
    bool prepared;
    public void Prepare() { if (!prepared) { prepared = true; Collect(); } }
    public bool IsModel(ITypeSymbol t) { Prepare(); return t is INamedTypeSymbol n && classes.ContainsKey(n.OriginalDefinition); }
    /* the largest capacity the models give a list of this model (List<ws::LeaderboardEntry, 16>), for a temporary list of them; 8 if none */
    public int ListCapacityOf(string modelName)
    {
        Prepare();
        int cap = 0;
        var pre = $"List<ws::{modelName}, ";
        foreach (var ci in classes.Values)
            foreach (var m in ci.Members)
                if (m.CppType != null && m.CppType.StartsWith(pre) && int.TryParse(m.CppType[pre.Length..].TrimEnd('>', ' '), out var n) && n > cap) cap = n;
        return cap > 0 ? cap : 8;
    }
    public Member MemberOf(ITypeSymbol t, string name) { Prepare(); return t is INamedTypeSymbol n && classes.TryGetValue(n.OriginalDefinition, out var ci) ? ci.Members.FirstOrDefault(m => m.Name == name) : null; }

    public int Run(string outDir)
    {
        Prepare();
        Directory.CreateDirectory(outDir);
        var order = Order();
        File.WriteAllText(Path.Combine(outDir, "tfexgen_Models.hpp"), Header(order));
        File.WriteAllText(Path.Combine(outDir, "tfexgen_Models.cpp"), Source(order));
        Runtime.RemoveCopies(outDir);
        Console.Error.WriteLine($"models: {classes.Count} classes, {enums.Count} enums -> {outDir}");
        foreach (var n in Notes) Console.Error.WriteLine("  note: " + n);
        return 0;
    }

    void Collect()
    {
        foreach (var tree in ws.Trees.Where(t => t.FilePath.StartsWith(Dir)))
        {
            var model = ws.Model(tree);
            foreach (var td in tree.GetRoot().DescendantNodes().OfType<BaseTypeDeclarationSyntax>())
            {
                var sym = model.GetDeclaredSymbol(td) as INamedTypeSymbol;
                if (sym == null) continue;
                if (sym.TypeKind == TypeKind.Enum) { enums.Add(sym); continue; }
                if (sym.TypeKind != TypeKind.Class) continue;
                classes[sym] = new ClassInfo { Type = sym, File = tree.FilePath };
            }
        }
        foreach (var ci in classes.Values) Fill(ci);
    }

    static bool HasAttr(ISymbol s, string name) => s.GetAttributes().Any(a => a.AttributeClass?.Name == name || a.AttributeClass?.Name == name + "Attribute");

    void Fill(ClassInfo ci)
    {
        bool dataContract = HasAttr(ci.Type, "DataContract");
        foreach (var decl in ci.Type.DeclaringSyntaxReferences.Select(r => r.GetSyntax()).OfType<TypeDeclarationSyntax>())
        {
            var model = ws.Model(decl.SyntaxTree);
            foreach (var md in decl.Members)
            {
                if (md is FieldDeclarationSyntax fd && fd.Modifiers.Any(SyntaxKind.ConstKeyword))
                    foreach (var v in fd.Declaration.Variables)
                    {
                        var f = (IFieldSymbol)model.GetDeclaredSymbol(v);
                        ci.Consts.Add((f.Name, f.Type.SpecialType == SpecialType.System_String ? "const char*" : CppScalar(f.Type), Literal(f.ConstantValue)));
                    }
                if (md is not PropertyDeclarationSyntax pd) continue;
                var p = model.GetDeclaredSymbol(pd);
                if (p == null || p.IsStatic || p.DeclaredAccessibility != Accessibility.Public) continue;
                if (p.SetMethod == null) continue;                       // computed (get-only): not serialized
                bool local = HasAttr(p, "IgnoreMember") || HasAttr(p, "IgnoreDataMember");
                if (local && !(pd.AccessorList?.Accessors.All(a => a.Body == null && a.ExpressionBody == null) ?? false)) continue;   // a computed setter
                var dm = p.GetAttributes().FirstOrDefault(a => a.AttributeClass?.Name is "DataMember" or "DataMemberAttribute");
                if (dataContract && dm == null) continue;
                var key = dm?.NamedArguments.FirstOrDefault(a => a.Key == "Name").Value.Value as string ?? p.Name;
                var path = ci.Type.Name + "." + p.Name;
                var m = new Member { Prop = p, Name = p.Name, Key = key, Syntax = pd, Local = local };
                m.CppType = CppType(p.Type, path, ci, top: true);
                m.Init = Init(p, pd, model, m.CppType);
                ci.Members.Add(m);
            }
        }
    }

    int Cap(string path, int def)
    {
        if (caps.TryGetValue(path, out var v) && v is long l) return (int)l;
        // "Type.Prop" also matches "*.Prop" entries (a field name shared by several classes)
        var star = "*." + path[(path.IndexOf('.') + 1)..];
        return caps.TryGetValue(star, out var s) && s is long ls ? (int)ls : def;
    }

    static string CppScalar(ITypeSymbol t) => t.SpecialType switch
    {
        SpecialType.System_Int32 => "int32_t",
        SpecialType.System_UInt32 => "uint32_t",
        SpecialType.System_Int64 => "int64_t",
        SpecialType.System_Boolean => "bool",
        SpecialType.System_Double => "double",
        SpecialType.System_Single => "float",
        _ => null,
    };

    string CppType(ITypeSymbol t, string path, ClassInfo owner, bool top)
    {
        var s = CppScalar(t);
        if (s != null) return s;
        // "Class.Prop" = "managed": a managed string reference (MStr) instead of fixed storage (TF.EX's SkinChunk.Data: 262 KB)
        if (t.SpecialType == SpecialType.System_String) return caps.TryGetValue(path, out var mv) && mv is string ms && ms == "managed" ? "MStr" : $"Str<{Cap(path, defString)}>";
        if (t is INamedTypeSymbol nt && nt.OriginalDefinition.SpecialType == SpecialType.System_Nullable_T)
            return $"Opt<{CppType(nt.TypeArguments[0], path, owner, false)}>";
        if (t is IArrayTypeSymbol at)
            return $"List<{CppType(at.ElementType, path + ".item", owner, false)}, {Cap(path, defList)}>";
        if (t is INamedTypeSymbol g && g.IsGenericType)
        {
            var name = g.OriginalDefinition.ToDisplayString();
            if (name.StartsWith("System.Collections.Generic.Dictionary") || name.StartsWith("System.Collections.Generic.IDictionary"))
            {
                if (g.TypeArguments.All(a => a.SpecialType == SpecialType.System_String))
                    return $"StrMap<{Cap(path, defMap)}, {Cap(path + ".key", defMapKey)}, {Cap(path + ".value", defMapValue)}>";
                // string keys, any other value (PlayerProfile.Stats: StatTotal; SeatStats.RawStats: double): KeyMap
                if (g.TypeArguments[0].SpecialType == SpecialType.System_String && CppType(g.TypeArguments[1], path + ".value", owner, false) is string vt && !vt.Contains("unsupported"))
                    return $"KeyMap<{Cap(path, defMap)}, {Cap(path + ".key", defMapKey)}, {vt}>";
                Notes.Add($"{path}: dictionary of {g.TypeArguments[0]}, {g.TypeArguments[1]} not supported");
                return "/*unsupported*/ int32_t";
            }
            if (name.StartsWith("System.Collections.Generic.") && g.TypeArguments.Length == 1)
                return $"List<{CppType(g.TypeArguments[0], path + ".item", owner, false)}, {Cap(path, defList)}>";
        }
        if (t is INamedTypeSymbol c && classes.ContainsKey(c))
        {
            if (!owner.Deps.Contains(c, SymbolEqualityComparer.Default)) owner.Deps.Add(c);
            // qualified: a member named like its type (Opt<Lobby> Lobby) would otherwise change the name's meaning in the class (GCC)
            return top ? $"Opt<ws::{c.Name}>" : $"ws::{c.Name}";    // a reference property can be null; list elements are objects
        }
        if (t.TypeKind == TypeKind.Enum) return "ws::" + t.Name;
        Notes.Add($"{path}: type {t} not supported");
        return "/*unsupported*/ int32_t";
    }

    static string Literal(object v) => v switch
    {
        null => "nullptr",
        string s => "\"" + s.Replace("\\", "\\\\").Replace("\"", "\\\"") + "\"",
        bool b => b ? "true" : "false",
        _ => Convert.ToString(v, System.Globalization.CultureInfo.InvariantCulture),
    };

    // The C# initializer as a C++ default member initializer ("" = the type's default).
    string Init(IPropertySymbol p, PropertyDeclarationSyntax pd, SemanticModel model, string cpp)
    {
        var e = pd.Initializer?.Value;
        bool reference = !p.Type.IsValueType;
        if (e == null || e is LiteralExpressionSyntax { RawKind: (int)SyntaxKind.NullLiteralExpression })
        {
            if (!reference) return "{}";                                    // C# default: zero
            return cpp.StartsWith("Opt<") ? "" : $"{{{cpp}::Null()}}";     // C# default of a reference: null
        }
        var cv = model.GetConstantValue(e);
        if (cv.HasValue) return $"{{{Literal(cv.Value)}}}";
        if (e is CollectionExpressionSyntax ce && ce.Elements.Count == 0) return "";
        if (e is BaseObjectCreationExpressionSyntax oc && (oc.ArgumentList == null || oc.ArgumentList.Arguments.Count == 0) && oc.Initializer == null)
            return cpp.StartsWith("Opt<") ? $"{{{cpp}::Some()}}" : "";
        Notes.Add($"{p.ContainingType.Name}.{p.Name}: initializer `{e}` not translated (type default used)");
        return "";
    }

    List<INamedTypeSymbol> Order()
    {
        var done = new HashSet<INamedTypeSymbol>(SymbolEqualityComparer.Default);
        var order = new List<INamedTypeSymbol>();
        void Visit(INamedTypeSymbol t)
        {
            if (!done.Add(t)) return;
            foreach (var d in classes[t].Deps) Visit(d);
            order.Add(t);
        }
        foreach (var t in classes.Keys.OrderBy(t => classes[t].File, StringComparer.Ordinal).ThenBy(t => t.Name, StringComparer.Ordinal)) Visit(t);
        return order;
    }

    public static string Banner(Workspace ws, IEnumerable<string> files) =>
        "/* GENERATED by tools/tfex/translator from TF.EX (vendor/tfex) " + Runtime.Label + ": do not edit; re-run tools/tfex/translate.sh.\n" +
        " * From " + files.Distinct().Count() + " files under " + CommonDir(files) + " (each struct names its file). */\n";

    static string CommonDir(IEnumerable<string> files)
    {
        var l = files.Distinct().ToList();
        var p = l.Count == 0 ? "" : Path.GetDirectoryName(l[0]).Replace('\\', '/');
        while (p.Length > 0 && !l.All(f => f.StartsWith(p + "/"))) p = Path.GetDirectoryName(p)?.Replace('\\', '/') ?? "";
        return "vendor/tfex/src/" + p;
    }

    string Header(List<INamedTypeSymbol> order)
    {
        var sb = new StringBuilder();
        sb.Append(Banner(ws, classes.Values.Select(c => c.File)));
        sb.Append("#pragma once\n#include \"tfexgen_rt.hpp\"\n\nnamespace tfex::gen::ws {\nusing tfex::gen::JRead;   /* the runtime's overloads (int, bool, Str, List...) beside the generated ones */\nusing tfex::gen::JWrite;\n\n");
        foreach (var e in enums)
        {
            sb.Append($"enum class {e.Name} : int32_t {{ ");
            sb.Append(string.Join(", ", e.GetMembers().OfType<IFieldSymbol>().Where(f => f.HasConstantValue).Select(f => $"{f.Name} = {f.ConstantValue}")));
            sb.Append(" };\n");
        }
        sb.Append('\n');
        foreach (var t in order)
        {
            var ci = classes[t];
            sb.Append($"/* {ci.File} */\nstruct {t.Name} {{\n");
            foreach (var (n, ty, v) in ci.Consts) sb.Append($"    static constexpr {ty} {n} = {v};\n");
            foreach (var m in ci.Members)
                sb.Append($"    {m.CppType} {m.Name}{m.Init};" + (m.Local ? "   /* [IgnoreMember]: local state, not in JSON */" : m.Key != m.Name ? $"   /* \"{m.Key}\" */" : "") + "\n");
            sb.Append("};\n");
            sb.Append($"void JRead(const Doc& d, const Node* n, {t.Name}& v);\n");
            sb.Append($"void JWrite(Writer& w, const char* key, const {t.Name}& v);\n\n");
        }
        sb.Append("/* the message whose JSON is {\"<name>\": ...} (TF.EX's <name>Message classes): parses root into a fresh object of it\n");
        sb.Append(" * and writes it back as TF.EX would; false for a name TF.EX has no class for. Host tests only (heap). */\n");
        sb.Append("#ifdef TFEXGEN_TEST\nbool RoundTrip(const char* name, const Doc& d, const Node* root, Writer& w);\n#endif\n\n");
        sb.Append("} // namespace tfex::gen::ws\n");
        return sb.ToString();
    }

    string Source(List<INamedTypeSymbol> order)
    {
        var sb = new StringBuilder();
        sb.Append(Banner(ws, classes.Values.Select(c => c.File)));
        sb.Append("#include \"tfexgen_Models.hpp\"\n#include <cstring>\n\nnamespace tfex::gen::ws {\n\n");
        foreach (var t in order)
        {
            var ci = classes[t];
            sb.Append($"void JRead(const Doc& d, const Node* n, {t.Name}& v) {{\n");
            var ser = ci.Members.Where(m => !m.Local).ToList();
            if (ser.Count == 0) sb.Append("    (void)d; (void)n; (void)v;\n");
            else
            {
                sb.Append("    if (!n || n->type != json::Type::Object) return;\n");
                foreach (var m in ser) sb.Append($"    JRead(d, d.Get(n, \"{m.Key}\"), v.{m.Name});\n");
            }
            sb.Append("}\n");
            sb.Append($"void JWrite(Writer& w, const char* key, const {t.Name}& v) {{\n");
            if (ser.Count == 0)
                sb.Append("    (void)v;\n    w.BeginArray(key);   /* MessagePack writes a class with no members as an empty array */\n    w.EndArray();\n");
            else
            {
                sb.Append("    w.BeginObject(key);\n");
                foreach (var m in ser) sb.Append($"    JWrite(w, \"{m.Key}\", v.{m.Name});\n");
                sb.Append("    w.EndObject();\n");
            }
            sb.Append("}\n\n");
        }
        sb.Append("#ifdef TFEXGEN_TEST\nbool RoundTrip(const char* name, const Doc& d, const Node* root, Writer& w) {\n");
        foreach (var t in order.Where(t => t.Name.EndsWith("Message") && t.Name.Length > 7))
        {
            sb.Append($"    if (!std::strcmp(name, \"{t.Name[..^7]}\")) {{ auto* m = new {t.Name}(); JRead(d, root, *m); JWrite(w, nullptr, *m); delete m; return true; }}\n");
        }
        sb.Append("    return false;\n}\n#endif\n\n} // namespace tfex::gen::ws\n");
        return sb.ToString();
    }
}
