// One translation unit: a TF.EX type (or some of its members) -> tfexgen_<name>.{hpp,cpp}. See CodeGen.cs.
using System.Globalization;
using System.Text;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using Microsoft.CodeAnalysis.Operations;
using Tomlyn.Model;

namespace TfexTranslator;

public sealed partial class UnitEmitter
{
    // TFEX_PROF_UNITS=<unit,unit|*>: a profiling scope at each method's entry (gen::Prof in tfexgen_code_rt.hpp: inclusive / self time and call counts
    // per method, dumped by the rollback seam; a throwaway regeneration for the console's save / load cost, never committed)
    static bool ProfUnit(string unit) { var v = (Environment.GetEnvironmentVariable("TFEX_PROF_UNITS") ?? ""); return v == "*" || v.Split(',').Contains(unit); }

    readonly CodeGen gen;
    readonly Workspace ws;
    readonly Config cfg;
    readonly TomlTable unit;
    readonly MetadataOrder order;
    readonly string name, file;
    INamedTypeSymbol type;
    SemanticModel model;
    /* the semantic model of a node's own file (a partial type, as FortRise's, has members in several files) */
    SemanticModel M(SyntaxNode n) => n.SyntaxTree == model.SyntaxTree ? model : ws.Compilation.ContainsSyntaxTree(n.SyntaxTree) ? ws.Model(n.SyntaxTree) : gen.ModelOf(n.SyntaxTree);
    readonly List<ISymbol> members = new();
    readonly StringBuilder lits = new();
    readonly Dictionary<string, string> litNames = new();
    readonly HashSet<string> includes = new();
    /* P/Invoke externs this unit calls ([DllImport("ggrs_ffi")] GGRSFFI.netplay_*): their extern "C" prototypes, before the namespace */
    readonly SortedSet<string> externDecls = new(StringComparer.Ordinal);
    IMethodSymbol current;
    int tmp;
    /* per method, for the coverage report: what blocked it, and the game members it binds */
    public sealed record MethodResult(IMethodSymbol Method, int Lines, List<string> Blockers, List<string> Keys, List<IMethodSymbol> Callees);
    public readonly List<MethodResult> Methods = new();
    public readonly List<ISymbol> DataMembers = new();   /* fields and auto-properties emitted (tfexgen_members.txt) */
    readonly List<string> methodBlockers = new(), methodKeys = new();
    readonly List<IMethodSymbol> methodCallees = new();
    public INamedTypeSymbol Type => type;
    /* for HookGen: this emitter's type mapping and binding */
    public string CppOf(ITypeSymbol t) => Cpp(t);
    public string BodyOf(IMethodSymbol m) => B.Need("m", SwitchName(m.ContainingType), m.MethodKind == MethodKind.Constructor ? ".ctor" : m.Name, IlSig(m));
    public string FieldOf(IFieldSymbol f) => B.Need("f", SwitchName(f.ContainingType), f.Name);
    public string TypeKeyOf(ITypeSymbol t) => B.Need("t", SwitchName(t));
    readonly List<IPropertySymbol> autoProps = new();
    /* (CodeGen sets it: StructsGen.CanonProperty, a FortRise property seen through TF.EX's FortRise.dll -> FortRise's source one) */
    public static Func<IPropertySymbol, IPropertySymbol> CanonProperty;
    static bool IsAutoProperty(IPropertySymbol p) =>
        p.DeclaringSyntaxReferences.Length == 0 ? CanonProperty?.Invoke(p) is IPropertySymbol cp && cp.DeclaringSyntaxReferences.Length > 0 && IsAutoProperty(cp) :
        (p.DeclaringSyntaxReferences[0].GetSyntax() is PropertyDeclarationSyntax pd && pd.ExpressionBody == null &&
         pd.AccessorList != null && pd.AccessorList.Accessors.All(a => a.Body == null && a.ExpressionBody == null)) ||
        p.DeclaringSyntaxReferences[0].GetSyntax() is ParameterSyntax;   // a positional record's property (record R(string Tag)): an auto-property
    /* a blocker's construct class: the text before details (": ...", " (", type names) */
    static string Construct(string what)
    {
        var w = what;
        foreach (var cut in new[] { ": ", " (" }) { var i = w.IndexOf(cut); if (i > 0) w = w[..i]; }
        foreach (var pre in new[] { "call to a TF.EX type no unit translates", "field of another TF.EX type", "property ", "field ", "new ", "LINQ ", "user-defined operator", "FNA value-type method", "string conversion of", "unsupported type", "glue", "method group", "DynamicData", "delegate from", "generic method over a TF.EX type" })
            if (w.StartsWith(pre)) return pre.Trim();
        return w;
    }

    Report R => gen.Report;
    Binder B => gen.Binder;

    public UnitEmitter(CodeGen gen, Workspace ws, Config cfg, TomlTable unit, MetadataOrder order)
    {
        this.gen = gen; this.ws = ws; this.cfg = cfg; this.unit = unit; this.order = order;
        name = (string)unit["name"];
        file = (string)unit["file"];
        gen.Binder.OnNeed = k => methodKeys.Add(k);
    }

    string Where(IOperation op) => $"{name}: {current?.Name}: {op.Syntax.ToString().Split('\n')[0].Trim()}";
    string Where(SyntaxNode n) => $"{name}: {current?.Name}: {n.ToString().Split('\n')[0].Trim()}";

    /* ---------------------------------------------------------------- unit ---------------------------------------------------------------- */
    public (string h, string c) Emit()
    {
        var tree = ws.Tree(file) ?? throw new Exception($"unit {name}: no file {file}");
        model = ws.Model(tree);
        var typeName = (string)unit["type"];
        var decl = tree.GetRoot().DescendantNodes().OfType<TypeDeclarationSyntax>().FirstOrDefault(t => t.Identifier.Text == typeName) ?? throw new Exception($"unit {name}: no type {typeName} in {file}");
        type = M(decl).GetDeclaredSymbol(decl);
        if (unit.ContainsKey("typeargs") && gen.InstTypes.TryGetValue(name, out var inst))   // a generic class's instantiation (CodeGen.InstSuffix)
            for (int k = 0; k < type.TypeParameters.Length && k < inst.TypeArguments.Length; k++) subst[type.TypeParameters[k]] = inst.TypeArguments[k];
        // hosted = true: the type's object is hand-written (tfex/MatchmakingService.cpp ...): no class of ours; its fields are read and
        // written through overrides.toml [service] rules ("<Namespace.Type>.<field>" = { cpp = ..., set = ... }); self is passed through
        isClass = IsClassType(type) && !(unit.TryGetValue("hosted", out var hosted) && hosted is true);
        SelectMembers();

        var h = new StringBuilder();
        var c = new StringBuilder();
        var ns = $"tfex::gen::{name}";
        var banner = $"/* GENERATED by tools/tfex/translator from TF.EX (vendor/tfex) {Runtime.Label}: do not edit; re-run tools/tfex/translate.sh.\n" +
                     $" * vendor/tfex/src/{file}: {type.Name}" + (unit.ContainsKey("members") ? " (" + string.Join(", ", Config.Strings(unit, "members")) + ")" : "") + " */\n";
        var body = new StringBuilder();
        var decls = new StringBuilder();
        var prelude = new StringBuilder();
        methodBlockers.Clear();
        if (isClass) ClassPrelude(h, prelude);
        // constants and static fields of a static class
        // a static field / property whose initializer doesn't translate blocks the methods that use it (a unit with `members` emits only
        // those); a class's (all its fields make the object) blocks every method, below
        var memberBlockers = new Dictionary<ISymbol, List<string>>(SymbolEqualityComparer.Default);
        foreach (var f in members.OfType<IFieldSymbol>()) { int n0 = methodBlockers.Count; EmitField(f, h, prelude); if (methodBlockers.Count > n0) memberBlockers[f] = methodBlockers.Skip(n0).ToList(); }
        foreach (var p in members.OfType<IPropertySymbol>()) { int n0 = methodBlockers.Count; EmitAutoProperty(p, h, prelude); if (methodBlockers.Count > n0) memberBlockers[p] = methodBlockers.Skip(n0).ToList(); }
        // iterators first (templates, used by the methods below), then the rest
        methodKeys.Clear();
        var unitBlockers = isClass ? methodBlockers.ToList() : new List<string>();   // a class's prelude / fields: the whole class
        foreach (var m in members.OfType<IMethodSymbol>()) NoteOverride(m);
        var slotKeys = methodKeys.ToList();   // the game vtable slots the class overrides: the class needs them all
        // TF.State's generic methods: a copy per instantiation its callers asked for (CodeGen.MethodInsts), type parameters substituted
        // a definition's copies count for it (tfexgen_members.txt, the coverage's callers): translated when every copy is
        var instBlockers = new Dictionary<IMethodSymbol, List<string>>(SymbolEqualityComparer.Default);
        if (gen.MethodInsts.TryGetValue(name, out var insts))
            foreach (var (def, targs) in insts.ToList())
            {
                current = def;
                for (int k = 0; k < def.TypeParameters.Length && k < targs.Length; k++) subst[def.TypeParameters[k]] = targs[k];
                instSuffix = MethodInstSuffix(targs);
                try
                {
                    R.Add("mechanical: a TF.State generic method -> a copy per instantiation", $"{name}: {def.Name}{instSuffix}");
                    h.Append(Signature(def, header: true) + ";\n");
                    body.Append($"/*@M {type.Name}.{def.Name}{instSuffix}*/\n").Append(Signature(def, header: false)).Append(" {\n");
                    methodBlockers.Clear(); methodKeys.Clear(); methodCallees.Clear();
                    if ((Environment.GetEnvironmentVariable("TFEX_TRACE_UNITS") ?? "").Split(',').Contains((string)unit["name"]))   // (the copies too)
                        body.Append($"    gen::Trace(\"{unit["name"]}.{def.Name}{instSuffix}\");\n");
                    if (ProfUnit((string)unit["name"])) body.Append($"    TFEXGEN_PROF(\"{unit["name"]}.{def.Name}{instSuffix}\");\n");
                    string ib;
                    try { ib = MethodBody(def); }
                    catch (Exception e) { ib = "    TFEXGEN_UNSUPPORTED;\n"; methodBlockers.Add("translator error: " + e.GetType().Name); R.Add("translator error", $"{name}: {def.Name}{instSuffix}: {e.Message}"); }
                    body.Append(ib).Append("}\n\n");
                    if (!instBlockers.TryGetValue(def, out var ibl)) instBlockers[def] = ibl = new List<string>();
                    ibl.AddRange(methodBlockers.Where(b => !ibl.Contains(b)));
                }
                finally { instSuffix = null; foreach (var tp in def.TypeParameters) subst.Remove(tp); }
            }
        foreach (var (def, bl) in instBlockers)
            if (def.DeclaringSyntaxReferences.FirstOrDefault()?.GetSyntax().GetLocation().GetLineSpan() is FileLinePositionSpan dspan)
                Methods.Add(new MethodResult(def, dspan.EndLinePosition.Line - dspan.StartLinePosition.Line + 1, bl, new List<string>(), new List<IMethodSymbol>()));
        foreach (var m in members.OfType<IMethodSymbol>().OrderBy(m => m.IsIterator ? 0 : 1).ThenBy(m => m.MethodKind == MethodKind.PropertyGet ? 0 : 1))
        {
            if (gen.CopiesGeneric(m)) continue;   // TF.State's, the glue's, the code mods' and FortRise's generic methods: only their copies (gen.MethodInsts)
            current = m;
            string sig;
            try { sig = Signature(m, header: false); }
            catch (KeyNotFoundException e)
            {   // a type in the signature the translator has no C++ for (FortRise's generic Result<T, U>): reported, the method left out
                R.Add("unsupported: a type in the signature", $"{type.Name}.{m.Name}: {e.Message}");
                continue;
            }
            bool pub = IsPublicEntry(m);
            if (pub && !m.IsIterator) h.Append(Signature(m, header: true) + ";\n");
            else if (m.MethodKind == MethodKind.Constructor && isClass) h.Append(Signature(m, header: false) + ";\n");
            else if (!m.IsIterator && !(m.MethodKind == MethodKind.Constructor && isClass)) decls.Append("static " + sig + ";\n");
            body.Append($"/*@M {type.Name}.{m.Name}*/\n");   // marker: compile errors map back to the method (test/compile-gen.sh)
            body.Append(m.IsIterator || (m.MethodKind == MethodKind.Constructor && isClass) ? "" : (pub ? "" : "static ")).Append(sig).Append(" {\n");
            methodBlockers.Clear(); methodKeys.Clear(); methodCallees.Clear();
            // TFEX_TRACE_UNITS=<unit,unit>: a log line at each method's entry (debugging a translated unit in the emulator; never committed)
            if ((Environment.GetEnvironmentVariable("TFEX_TRACE_UNITS") ?? "").Split(',').Contains((string)unit["name"]) && !m.IsIterator)
                body.Append($"    gen::Trace(\"{unit["name"]}.{m.Name}\");\n");
            if (ProfUnit((string)unit["name"]) && !m.IsIterator && m.MethodKind != MethodKind.Constructor) body.Append($"    TFEXGEN_PROF(\"{unit["name"]}.{m.Name}\");\n");
            string mb;
            try { mb = MethodBody(m); }
            catch (Exception e) { if (Environment.GetEnvironmentVariable("TFEX_ERRTRACE") == "1") Console.Error.WriteLine($"translator error {m.Name}: {e}"); mb = "    TFEXGEN_UNSUPPORTED;\n"; methodBlockers.Add("translator error: " + e.GetType().Name); R.Add("translator error", $"{name}: {m.Name}: {e.Message}"); if (Environment.GetEnvironmentVariable("TFEX_TRACE") == "1") Console.Error.WriteLine($"{name}.{m.Name}: {e}"); }
            body.Append(mb);
            body.Append("}\n\n");
            if (IsCoroutineIterator(m))   // its coroutine twin, for `new Coroutine(Seq())` (gen::CoGen)
            {
                var ps = m.Parameters.Select(p => $"{Cpp(p.Type)} {Id(p.Name)}").ToList();
                if (!m.IsStatic) ps.Insert(0, "void* self");
                var coSig = $"gen::CoGen {Id(m.Name)}_co({string.Join(", ", ps)})";
                decls.Append("static " + coSig + ";\n");
                coMode = true;
                string cb;
                try { cb = MethodBody(m); } finally { coMode = false; }
                if (!cb.Contains("co_yield") && !cb.Contains("co_return")) cb += "    co_return;\n";
                body.Append($"/*@M {type.Name}.{m.Name}*/\nstatic {coSig} {{\n{cb}}}\n\n");
            }
            var span = m.DeclaringSyntaxReferences[0].GetSyntax().GetLocation().GetLineSpan();
            Methods.Add(new MethodResult(m, span.EndLinePosition.Line - span.StartLinePosition.Line + 1, methodBlockers.ToList(), methodKeys.ToList(), methodCallees.ToList()));
            if (Environment.GetEnvironmentVariable("TFEX_BLOCKERS") is string tbn && tbn == m.Name && methodBlockers.Count > 0)   // (why a member isn't counted as translated)
                Console.Error.WriteLine($"blockers {type.Name}.{m.Name}: {string.Join("; ", methodBlockers)}");
            current = null;
        }
        if (isClass) ClassConstructors(h, body);
        if (isClass)
        {
            // the members making the class (vtable slots it overrides, its base): every method needs them, the class can't exist without
            methodKeys.Clear(); methodBlockers.Clear();
            ClassEnsure(h, body);
            var classKeys = methodKeys.Concat(slotKeys).ToList();
            for (int i = 0; i < Methods.Count; i++) Methods[i] = Methods[i] with { Keys = Methods[i].Keys.Concat(classKeys).ToList() };
            unitBlockers.AddRange(methodBlockers);   // the class's statics (ClassEnsure)
        }
        if (!isClass && memberBlockers.Count > 0)
            for (int i = 0; i < Methods.Count; i++)
            {
                var syn = Methods[i].Method.DeclaringSyntaxReferences.FirstOrDefault()?.GetSyntax();
                if (syn == null || syn.SyntaxTree != model.SyntaxTree) continue;
                foreach (var id in syn.DescendantNodes().OfType<IdentifierNameSyntax>())
                    if (M(id).GetSymbolInfo(id).Symbol is ISymbol us && memberBlockers.TryGetValue(us.OriginalDefinition, out var mb))
                        Methods[i].Blockers.AddRange(mb.Where(b => !Methods[i].Blockers.Contains(b)));
            }
        if (unitBlockers.Count > 0)   // the unit can't be built: every method is blocked
            for (int i = 0; i < Methods.Count; i++) Methods[i].Blockers.AddRange(unitBlockers.Distinct().Where(b => !Methods[i].Blockers.Contains(b)));
        if (!isClass && lazyMakers.Count > 0)
        {
            h.Append("void MakeStatics();   /* its statics made at first use, all at once (before a rollback window) */\n");
            body.Append("void MakeStatics() {\n" + string.Concat(lazyMakers.Distinct().Select(x => $"    (void){x};\n")) + "}\n\n");
        }
        var hh = new StringBuilder();
        var hinc = (h.ToString().Contains("ws::") ? "#include \"tfexgen_Models.hpp\"\n" : "") + (h.ToString().Contains("tfex::gen::st::") ? "#include \"tfexgen_structs.hpp\"\n" : "") + string.Concat(headerIncludes.Select(i => $"#include \"{i}\"\n"));
        hh.Append(banner).Append("#pragma once\n#include \"tfexgen_code_rt.hpp\"\n").Append(hinc).Append("\nnamespace ").Append(ns).Append(" {\n").Append(h).Append("} // namespace ").Append(ns).Append('\n');
        c.Append(banner).Append($"#include \"tfexgen_{name}.hpp\"\n#include \"tfexgen_binding.hpp\"\n");
        foreach (var i in includes.Where(i => i != $"tfexgen_{name}.hpp")) c.Append($"#include \"{i}\"\n");
        if (externDecls.Count > 0) c.Append("\n/* P/Invoke externs (TF.EX's [DllImport]): the native library's C functions, linked into the module */\n").Append(string.Concat(externDecls.Select(d => d + "\n")));
        c.Append($"\nnamespace {ns} {{\nusing namespace tfex::gen;\n\n");
        if (lits.Length > 0) c.Append("/* string literals */\n").Append(lits).Append('\n');
        // the methods' declarations before the statics: a static field's initializer may call one (static X = Make(...))
        if (decls.Length > 0 && !isClass) c.Append(decls).Append('\n');
        // a static unit: its delegates' declarations (g_dhost, the lambdas) before the statics too: a static's initializer may make a
        // delegate (TF.State's PickupExtensions.ChangeColor = AccessTools.MethodDelegate(...))
        if (!isClass && lambdaDecls.Length > 0) c.Append(lambdaDecls).Append('\n');
        c.Append(prelude);
        if ((decls.Length > 0 && isClass) || (isClass && lambdaDecls.Length > 0)) c.Append(isClass ? decls.ToString() : "").Append(isClass ? lambdaDecls.ToString() : "").Append('\n');
        c.Append(lambdaBodies).Append(body).Append($"}} // namespace {ns}\n");
        return (hh.ToString(), c.ToString());
    }

    /* a TF.EX class that becomes a game class of ours: its base is a game class, another such class, or System.Object */
    bool IsClassType(INamedTypeSymbol t) =>
        t.TypeKind == TypeKind.Class && !t.IsStatic && t.BaseType != null && !gen.Models.IsModel(t) &&
        (InBinary(t.BaseType) || CorlibBase(t.BaseType) || (OriginOf(t.BaseType) == Origin.Tfex && IsClassType(t.BaseType)) ||
         (t.BaseType.SpecialType == SpecialType.System_Object && HasInstanceMembers(t)));
    /* a corlib class a TF.EX class derives from (TF.State's DeterministicRandom : System.Random): the Switch's corlib is compiled into the
     * main module (BRUTE), so its type, descriptor, size and vtable are bound as a game class's are, and the class becomes a real subclass
     * of it. Not System.Object (its own case), not generic, not abstract (SafeHandle's interop subclasses stay out). */
    bool CorlibBase(INamedTypeSymbol b) =>
        b != null && OriginOf(b) == Origin.Bcl && b.TypeKind == TypeKind.Class && b.SpecialType == SpecialType.None && !b.IsGenericType && !b.IsAbstract && !b.IsSealed;
    /* a class whose members are all static (Harmony's patch classes) is a static class in all but name */
    static bool HasInstanceMembers(INamedTypeSymbol t) =>
        t.GetMembers().Any(m => !m.IsStatic && !m.IsImplicitlyDeclared && m is IFieldSymbol or IPropertySymbol or IMethodSymbol { MethodKind: MethodKind.Ordinary or MethodKind.Constructor });

    bool IsPublicEntry(IMethodSymbol m) => (isClass && m.MethodKind != MethodKind.Constructor) || HookGen.KindOf(m) != null ||
        (m.DeclaredAccessibility is Accessibility.Public or Accessibility.Internal && m.MethodKind is MethodKind.Ordinary or MethodKind.PropertyGet or MethodKind.PropertySet or MethodKind.UserDefinedOperator && (m.IsStatic || !isClass))
        || Config.Strings(unit, "members").Contains(m.Name)
        || (m.MethodKind == MethodKind.Constructor && !m.IsStatic && !isClass && IsClassType(type) && m.DeclaredAccessibility == Accessibility.Public && type.BaseType?.ToDisplayString() == "FortRise.Mod")   // a mod's Mod subclass (hosted): its generated boot constructs it
        || (((string)unit["name"]).StartsWith("Cov_") && m.MethodKind is MethodKind.Ordinary or MethodKind.PropertyGet or MethodKind.PropertySet or MethodKind.UserDefinedOperator);   // coverage: whole types, called from other units

    void SelectMembers()
    {
        var wanted = Config.Strings(unit, "members").ToList();
        var all = type.GetMembers().Where(m => !m.IsImplicitlyDeclared && m.DeclaringSyntaxReferences.Length > 0 &&
                                               m is IMethodSymbol { MethodKind: MethodKind.Ordinary or MethodKind.Constructor or MethodKind.UserDefinedOperator, IsExtern: false } or IFieldSymbol).ToList();
        all.RemoveAll(m => m is IFieldSymbol { AssociatedSymbol: not null });   // auto-property backing fields
        // C# 14 extension blocks (FortRise.Content's EntryExtensions: `extension(IModSprites sprite) { ... }`): their members, as static
        // functions of this class with the receiver first (Roslyn's implementation methods have no syntax of their own)
        foreach (var et in type.GetTypeMembers().Where(t => t.IsExtension))
            all.AddRange(et.GetMembers().OfType<IMethodSymbol>().Where(m => m.MethodKind == MethodKind.Ordinary && m.DeclaringSyntaxReferences.Length > 0));
        // properties: accessors with bodies are methods (get_X/set_X); an auto-property is storage plus trivial accessors
        foreach (var p in type.GetMembers().OfType<IPropertySymbol>().Where(p => p.DeclaringSyntaxReferences.Length > 0 && !p.IsIndexer))
        {
            if (IsAutoProperty(p)) { autoProps.Add(p); continue; }
            if (p.GetMethod != null) all.Add(p.GetMethod);
            if (p.SetMethod != null) all.Add(p.SetMethod);
        }
        var excluded = Config.Strings(unit, "exclude").ToHashSet();   // members the ledger keeps off the Switch (and HookGen with them)
        all.RemoveAll(m => excluded.Contains(m.Name));
        all.RemoveAll(m => gen.AbsentRule(m) != null);   // translate.toml [fortrise] absent: its calls are no-ops / defaults, its body isn't ours
        bool filtered = unit.ContainsKey("members");
        var queue = new Queue<ISymbol>(!filtered ? all : all.Where(m => wanted.Contains(m.Name) || (m is IMethodSymbol { MethodKind: MethodKind.Constructor } && wanted.Contains(".ctor"))));
        var seen = new HashSet<ISymbol>(SymbolEqualityComparer.Default);
        var usedAutoProps = new HashSet<IPropertySymbol>(SymbolEqualityComparer.Default);
        while (queue.Count > 0)
        {
            var s = queue.Dequeue();
            if (!seen.Add(s)) continue;
            members.Add(s);
            if (s is not IMethodSymbol m) continue;
            foreach (var r in m.DeclaringSyntaxReferences)
            {
                var n = r.GetSyntax();
                var rm = r.SyntaxTree == model.SyntaxTree ? model : ws.Model(r.SyntaxTree);   // (a partial type's member in another file: that file's model)
                foreach (var id in n.DescendantNodes().Where(x => x is IdentifierNameSyntax || x is GenericNameSyntax || x is ObjectCreationExpressionSyntax || x is ImplicitObjectCreationExpressionSyntax))   // (Generic: M<T>(), a generic method of ours)
                {
                    var t = rm.GetSymbolInfo(id).Symbol;
                    if (t is IMethodSymbol { ReducedFrom: { } rf }) t = rf;   // x.Ext(): the extension method itself
                    if (t is IMethodSymbol { IsGenericMethod: true } gm) t = gm.OriginalDefinition;   // M<T>(): the definition
                    if (t != null && SymbolEqualityComparer.Default.Equals(t.ContainingType, type) && all.Contains(t, SymbolEqualityComparer.Default)) queue.Enqueue(t);
                    if (t is IPropertySymbol rp && SymbolEqualityComparer.Default.Equals(rp.ContainingType, type))   // a property: its accessors are the members
                        foreach (var acc in new[] { rp.GetMethod, rp.SetMethod })
                            if (acc != null && all.Contains(acc, SymbolEqualityComparer.Default)) queue.Enqueue(acc);
                    if (t is IPropertySymbol ap && autoProps.Contains(ap, SymbolEqualityComparer.Default)) usedAutoProps.Add(ap);   // its storage and accessors come too
                }
            }
        }
        foreach (var p in autoProps.Where(p => !filtered || wanted.Contains(p.Name) || usedAutoProps.Contains(p))) if (!members.Contains(p)) members.Add(p);
        // declaration order
        members.Sort((a, b) => a.Locations[0].SourceSpan.Start.CompareTo(b.Locations[0].SourceSpan.Start));
    }

    /* ---------------------------------------------------------------- types ---------------------------------------------------------------- */
    static readonly HashSet<string> CppKeywords = new() { "new", "delete", "default", "register", "union", "template", "operator", "auto", "signed",
        "unsigned", "typename", "friend", "inline", "mutable", "export", "and", "or", "not", "xor", "bool", "int", "float", "double", "char", "long",
        "short", "this", "class", "struct", "enum", "const", "static", "extern", "volatile", "goto", "asm", "near", "far", "small", "s", "self" };
    /* a method's C++ name: its own, and "_<k>" (its place among the same-named methods) when an earlier overload has the same C++
     * parameter list (references are all void*: FortRise's SpriteDataLoader.LoadSpriteData(..., IResourceInfo) / (..., Loader?)); an instance
     * method's list starts with its self (void*): v0.29.0's ConfirmDialog has IsPressed(RawMenuButton) and static IsPressed(PlayerInput,
     * RawMenuButton), both (void*, int32_t) in C++ */
    static string MName(IMethodSymbol m)
    {
        var n = Id(m.Name);
        if (m.MethodKind != MethodKind.Ordinary || m.ContainingType == null) return n;
        var same = m.ContainingType.OriginalDefinition.GetMembers(m.Name).OfType<IMethodSymbol>().Where(x => x.MethodKind == MethodKind.Ordinary).ToList();
        if (same.Count < 2) return n;
        static string Sig(IMethodSymbol x) => string.Join(",", (x.IsStatic ? Enumerable.Empty<string>() : new[] { "void*" }).Concat(x.Parameters.Select(p =>
            p.RefKind != RefKind.None ? "ref " + p.Type.ToDisplayString() : p.Type.IsReferenceType ? "void*" :
            p.Type is INamedTypeSymbol { TypeKind: TypeKind.Enum, EnumUnderlyingType: var eu } ? eu.ToDisplayString() : p.Type.ToDisplayString())));   // (an enum: its C++ integer, LoaderAI's NameOf(Keys) / NameOf(Buttons))
        var k = same.FindIndex(x => SymbolEqualityComparer.Default.Equals(x, m.OriginalDefinition));
        if (k <= 0) return n;
        var sig = Sig(same[k]);
        return same.Take(k).Any(x => Sig(x) == sig) ? $"{n}_{k}" : n;
    }

    /* a constructor's suffix for Ctor / New: "_<k>" (its place among the type's constructors) when an earlier one has the same C++
     * parameter list (FortRise's SFXEntry(string, IResourceInfo, bool) / (string, Func<SFX>, bool)), else "" */
    static string CtorSuffix(IMethodSymbol c)
    {
        if (c == null || c.MethodKind != MethodKind.Constructor || c.ContainingType == null) return "";
        var all = c.ContainingType.OriginalDefinition.InstanceConstructors.Where(x => !x.IsImplicitlyDeclared || x.Parameters.Length == 0).ToList();
        static string Sig(IMethodSymbol x) => string.Join(",", x.Parameters.Select(p =>
            p.RefKind != RefKind.None ? "ref " + p.Type.ToDisplayString() : p.Type.IsReferenceType ? "void*" : p.Type.ToDisplayString()));
        var k = all.FindIndex(x => SymbolEqualityComparer.Default.Equals(x, c.OriginalDefinition));
        if (k <= 0) return "";
        return all.Take(k).Any(x => Sig(x) == Sig(all[k])) ? $"_{k}" : "";
    }

    static string Id(string n)
    {
        n = new string(n.Select(ch => char.IsLetterOrDigit(ch) || ch == '_' ? ch : '_').ToArray());   // compiler names: <Clone>$ ...
        return CppKeywords.Contains(n) ? n + "_" : n;
    }

    public enum Origin { Tfex, Game, Fna, Bcl }
    /* members reached in the Switch binary: the game's, and FNA's classes (FNA's value types are runtime PODs) */
    bool InBinary(ITypeSymbol t) { var o = OriginOf(t); return (o == Origin.Game && (t.IsReferenceType || t.TypeKind == TypeKind.Enum)) || (o == Origin.Fna && t.IsReferenceType); }
    Origin OriginOf(ITypeSymbol t)
    {
        var a = t.ContainingAssembly?.Name;
        if (a == null) return Origin.Bcl;
        if (SymbolEqualityComparer.Default.Equals(t.ContainingAssembly, ws.Compilation.Assembly)) return Origin.Tfex;
        // another of the translator's compilations (Workspace: all "TfexAll"): a code mod's class seen from TF.EX's code (TF.State's
        // DynamicData read of a mod round logic's roundEndCounter) is ours too, made by its unit
        if (a == "TfexAll") return Origin.Tfex;
        // a FortRise interface a mod sees through the patched game assembly (FortRise is MonoMod'd into TowerFall.exe) whose
        // implementation FortRise's source gives a unit (ISubtextureEntry -> SubtextureEntry): FortRise's code, ours, not the game's
        if (a == "TowerFall" && t is INamedTypeSymbol { TypeKind: TypeKind.Interface } fi && gen.FrImplsOf(fi).Count > 0) return Origin.Tfex;
        if (a == "TowerFall") return Origin.Game;
        if (a == "FNA") return Origin.Fna;
        return Origin.Bcl;
    }

    /* game structs TF.EX holds by value: their size on the Switch (bytes), verified against the binary's field records */
    /* TF.EX's generic AttributeManager<T> (ledger: a generic C# helper): the runtime's gen::AttrMgr<T> */
    /* the PC's keyboard / mouse state (FNA): the Switch has neither; their reads are "nothing pressed" (TFEX-DIFF S-input) */
    static bool IsPcInputState(ITypeSymbol t) => t?.ToDisplayString() is "Microsoft.Xna.Framework.Input.KeyboardState" or "Microsoft.Xna.Framework.Input.MouseState";
    static bool IsPcInputOwner(ITypeSymbol t) => t?.ToDisplayString() is "Microsoft.Xna.Framework.Input.Keyboard" or "Microsoft.Xna.Framework.Input.Mouse"
        or "Microsoft.Xna.Framework.Input.KeyboardState" or "Microsoft.Xna.Framework.Input.MouseState";
    static bool IsAttrMgr(ITypeSymbol t) => t is INamedTypeSymbol { Name: "AttributeManager", TypeArguments.Length: 1 } a && a.ContainingNamespace?.ToDisplayString() == "TF.EX.Domain.Utils";
    internal static readonly Dictionary<string, int> GameValueTypes = new()
    {
        ["TowerFall.InputState"] = 28,   // 9 bools (0..8), MoveX 12, MoveY 16, AimAxis 20 (binding.json; xplay.cpp InputStateRaw)
        // ArcherData's nested structs (FortRise's ModArchers fills them for a mod's archer): sizes from ArcherData's field records
        // (binding.json: Sprites +136, Hat +184, Portraits +216, Statue +248, Gems +264, Breathing +280; their own fields' offsets)
        ["TowerFall.ArcherData.SpriteInfo"] = 48,      // Body 0, HeadNormal 8, HeadNoHat 16, HeadCrown 24, HeadBack 32, Bow 40
        ["TowerFall.ArcherData.HatInfo"] = 32,         // Material 0, Normal 8, Blue 16, Red 24
        ["TowerFall.ArcherData.PortraitInfo"] = 32,    // NotJoined 0, Joined 8, Win 16, Lose 24
        ["TowerFall.ArcherData.StatueInfo"] = 16,      // Image, Glow
        ["TowerFall.ArcherData.GemInfo"] = 16,         // Menu 0, Gameplay 8
        ["TowerFall.ArcherData.BreathingInfo"] = 20,   // Interval 0, Offset 4, DuckingOffset 12
        // TF.State's state path (tools/re rtypes: the Switch's type records)
        ["TowerFall.ArrowCushion.ArrowData"] = 24,      // Arrow 0, Offset 8, Rotation 16
        ["TowerFall.KillStats"] = 216,                  // 27 ulong counters
        ["TowerFall.MatchStats"] = 680,                 // Won 0 ... FramesAlive 244, Kills 248, Deaths 464 (KillStats)
    };
    static readonly Dictionary<string, string> FnaValueTypes = new()
    {
        ["Microsoft.Xna.Framework.Vector2"] = "gen::Vector2", ["Microsoft.Xna.Framework.Color"] = "gen::Color",
        ["Microsoft.Xna.Framework.Rectangle"] = "gen::Rectangle", ["Microsoft.Xna.Framework.Point"] = "gen::Point",
        ["Microsoft.Xna.Framework.Matrix"] = "gen::Matrix",
        ["TowerFall.PlayerInventory"] = "gen::PlayerInventory",   // the Switch's 16 bytes: Arrows, then 4 bools (Player..ctor x4/x5, Session.GetPlayerInventory x0/x1)
    };

    /* a generic local function's copy for one instantiation (Register<string>, Register<int>): its type parameters -> the arguments,
     * wherever a type is looked at (Cpp, the struct and collection checks, new, conversions, typeof) */
    readonly Dictionary<ITypeParameterSymbol, ITypeSymbol> subst = new(SymbolEqualityComparer.Default);
    /* ref locals that stand for a dictionary slot (GetValueRefOrAddDefault): their read and their write */
    readonly Dictionary<ILocalSymbol, (string get, Func<string, string> set)> refSlots = new(SymbolEqualityComparer.Default);
    readonly Dictionary<ILocalSymbol, string> refFound = new(SymbolEqualityComparer.Default);   /* a GetValueRefOrNullRef ref local: its "key found" flag */
    ITypeSymbol Sub(ITypeSymbol t)
    {
        if (t != null && subst.Count == 0) return gen.Structs.Canon(t);
        if (t == null) return t;
        switch (t)
        {
            case ITypeParameterSymbol tp: return subst.TryGetValue(tp, out var r) ? r : t;
            case IArrayTypeSymbol a: { var e = Sub(a.ElementType); return SymbolEqualityComparer.Default.Equals(e, a.ElementType) ? t : ws.Compilation.CreateArrayTypeSymbol(e, a.Rank); }
            case INamedTypeSymbol n when n.IsGenericType && n.TypeArguments.Length > 0:
            {
                var args = n.TypeArguments.Select(Sub).ToArray();
                return args.SequenceEqual(n.TypeArguments, SymbolEqualityComparer.Default) ? t : n.OriginalDefinition.Construct(args);
            }
            default: return t;
        }
    }
    bool IsPodS(ITypeSymbol t) => gen.Structs.IsPod(Sub(t));
    /* a type where C++ can't take `auto` (a static field, a tuple's element): a collection of lobby models is then a pointer to the
     * models' own list of them (List<ws::SeatStats, 16>*: MatchStatsReport._stats, Take's (List<SeatStats>, int?)), null as C#'s null */
    string StorableCpp(ITypeSymbol t)
    {
        var c = Cpp(t);
        if (c == "auto" && IsModelCollection(t) && t is INamedTypeSymbol { TypeArguments.Length: 1 } mt && gen.Models.IsModel(mt.TypeArguments[0]))
            return $"tfex::gen::List<tfex::gen::ws::{mt.TypeArguments[0].Name}, {gen.Models.ListCapacityOf(mt.TypeArguments[0].Name)}>*";
        return c;
    }
    /* a rectangular array of numbers (FortRise's UIModMenu.LevenshteinDistance: int[,]): gen::Grid, a local value owning its cells (no
     * managed references in it, so nothing for the GC to scan) */
    public static bool IsGridElem(ITypeSymbol e) => e.SpecialType is SpecialType.System_Int32 or SpecialType.System_Single or SpecialType.System_Double or SpecialType.System_Boolean;
    string Cpp(ITypeSymbol t)
    {
        t = Sub(t);
        switch (t.SpecialType)
        {
            case SpecialType.System_Void: return "void";
            case SpecialType.System_Boolean: return "bool";
            case SpecialType.System_Char: return "char16_t";
            case SpecialType.System_SByte: return "int8_t";
            case SpecialType.System_Byte: return "uint8_t";
            case SpecialType.System_Int16: return "int16_t";
            case SpecialType.System_UInt16: return "uint16_t";
            case SpecialType.System_Int32: return "int32_t";
            case SpecialType.System_UInt32: return "uint32_t";
            case SpecialType.System_Int64: return "int64_t";
            case SpecialType.System_UInt64: return "uint64_t";
            case SpecialType.System_Single: return "float";
            case SpecialType.System_Double: return "double";
            case SpecialType.System_IntPtr: return "intptr_t";     // nint / IntPtr: a native pointer-sized value (the FFI's handles)
            case SpecialType.System_UIntPtr: return "uintptr_t";
            case SpecialType.System_String: return "void*";
            case SpecialType.System_Object: return "void*";
        }
        if (t is IArrayTypeSymbol { Rank: 2 } a2 && IsGridElem(a2.ElementType)) return $"gen::Grid<{Cpp(a2.ElementType)}>";   // int[,]: a local grid
        if (t is ITypeParameterSymbol tp0)
        {
            if (tp0.IsReferenceType || tp0.ConstraintTypes.Any(c => c.IsReferenceType)) return "void*";
            R.Add("unsupported type", "type parameter " + tp0.Name + " (value type)");
            return "/*T*/ int32_t";
        }
        if (t.ToDisplayString() is "System.DateTime" or "System.TimeSpan") return "int64_t";
        // ReadOnlySpan<char> over a string (FortRise's patch_Sounds: SoundsLoaded's keys by span): the managed string itself
        if (t.ToDisplayString() == "System.ReadOnlySpan<char>") return "void*";
        // Dictionary<string, V>.AlternateLookup<ReadOnlySpan<char>> (the same: keys by span): the dictionary itself
        if (AltLookupOf(t) is ITypeSymbol ald) return Cpp(ald);
        if (t.ToDisplayString() is "System.Version" or "System.Version?") return "int64_t";   // packed so that int64 order is Version's (gen::VersionTryParse)
        // CancellationToken: TF.EX's async work runs synchronously on the Switch (S-async), so nothing is ever cancelled mid-way; a token is
        // a plain value (IsCancellationRequested reads false, CancellationTokenSource.Cancel is the game's own)
        if (t.ToDisplayString() == "System.Threading.CancellationToken") return "int32_t";
        // ArraySegment<T> over a whole array (TF.EX's socket buffers: new ArraySegment<byte>(buffer)): the array itself (S-net)
        if (t is INamedTypeSymbol { IsGenericType: true } seg && seg.OriginalDefinition.ToDisplayString() == "System.ArraySegment<T>") return $"gen::Arr<{Cpp(seg.TypeArguments[0])}>";
        if (t.ToDisplayString() is "System.Threading.Tasks.Task" or "System.Threading.Tasks.Task?") return "void";   // an async method's / lambda's Task: nothing to wait for here (S-async)
        if (t is INamedTypeSymbol tup && tup.IsTupleType && tup.TupleElements.Length is >= 2 and <= 7)
            return $"gen::Tup{tup.TupleElements.Length}<{string.Join(", ", tup.TupleElements.Select(e => StorableCpp(e.Type)))}>";
        if (t is INamedTypeSymbol kvt && kvt.OriginalDefinition.ToDisplayString() == "System.Collections.Generic.KeyValuePair<TKey, TValue>")
            return $"gen::KV<{Cpp(kvt.TypeArguments[0])}, {Cpp(kvt.TypeArguments[1])}>";
        if (gen.Models.IsModel(t)) { includes.Add("tfexgen_Models.hpp"); return $"tfex::gen::ws::{t.Name}*"; }
        if (IsPodS(t)) { includes.Add("tfexgen_structs.hpp"); return gen.Structs.CppName(t); }
        if (IsStrongBox(t)) return $"gen::Box<{Cpp(((INamedTypeSymbol)t).TypeArguments[0])}>*";
        if (IsGrouping(t)) return $"gen::Group<{Cpp(((INamedTypeSymbol)t).TypeArguments[0])}, {Cpp(((INamedTypeSymbol)t).TypeArguments[1])}>*";
        if (IsValColl(t)) return ValCpp(t) + "*";
        if (IsModelCollection(t)) { includes.Add("tfexgen_Models.hpp"); return "auto"; }
        if (t is INamedTypeSymbol { OriginalDefinition.SpecialType: SpecialType.System_Nullable_T } nul) return $"gen::Opt<{Cpp(nul.TypeArguments[0])}>";
        if (t is IArrayTypeSymbol at) return $"gen::Arr<{Cpp(at.ElementType)}>";
        if (t.TypeKind == TypeKind.Enum) return Cpp(((INamedTypeSymbol)t).EnumUnderlyingType);
        if (FnaValueTypes.TryGetValue(t.ToDisplayString(), out var fv)) return fv;
        if (GameValueTypes.TryGetValue(t.ToDisplayString(), out var gvn)) return $"gen::GameVal<{gvn}>";
        if (IsPcInputState(t)) return "int32_t";   // S-input: no PC keyboard / mouse on the Switch (nothing pressed: UnitEmitter.Expr PcInput)
        if (IsAttrMgr(t)) return $"gen::AttrMgr<{Cpp(((INamedTypeSymbol)t).TypeArguments[0])}>*";
        if (t.IsReferenceType) return "void*";
        R.Add("unsupported type", t.ToDisplayString() + " (" + name + ")");
        methodBlockers.Add("unsupported type");
        return "/*" + t.ToDisplayString() + "*/ int32_t";
    }

    /* an expression that is a lobby model's collection (a pointer to the struct's list): by its type (IsModelCollection), a model's
     * property, an element of a model's list of lists (series.Sides[side]: List<int>), an element picked from one, or a local holding one */
    readonly HashSet<ILocalSymbol> modelLocals = new(SymbolEqualityComparer.Default);
    /* lambda parameters bound to the inner lists of a model's list of lists (Series.Sides: FindIndex(seats => seats.Contains(x))) */
    readonly HashSet<IParameterSymbol> modelLambdaParams = new(SymbolEqualityComparer.Default);
    readonly HashSet<ILocalSymbol> seqLocals = new(SymbolEqualityComparer.Default);
    /* a switch's exit label -> the C++ label after its if-chain, for a break nested in a section (DarkPortalsVariantSequencePatch.Step) */
    internal readonly Dictionary<ILabelSymbol, string> switchExits = new(SymbolEqualityComparer.Default);   // var x = <lazy LINQ chain> (an IEnumerable<T>): the sequence itself
    /* a TF.EX method's collection-interface parameter (IEnumerable<string> variants) that TF.EX's code passes a lobby model list
     * (lobby.GameData.Variants), always the same kind: typed as that list (a pointer, as models' own) and iterated as one; a call passing
     * another collection ([title]) copies it into a temporary list (Expr.cs). Null when no call passes a model list, or two kinds do. */
    static readonly Dictionary<IParameterSymbol, string> modelListParams = new(SymbolEqualityComparer.Default);
    string ModelListParamCpp(IParameterSymbol p)
    {
        if (modelListParams.TryGetValue(p, out var known)) return known;
        modelListParams[p] = null;   // (a recursive call while scanning: not one)
        string found = null; bool ok = p.ContainingSymbol is IMethodSymbol pm && OriginOf(pm.ContainingType) == Origin.Tfex &&
            p.Type is INamedTypeSymbol { IsGenericType: true, ContainingNamespace: var pns } pt && pns?.ToDisplayString() == "System.Collections.Generic" &&
            pt.Name is "IEnumerable" or "ICollection" or "IReadOnlyCollection" or "IReadOnlyList" or "IList" or "Dictionary" or "IDictionary" or "IReadOnlyDictionary";
        int sites = 0;
        if (ok)
        {
            var m = (IMethodSymbol)p.ContainingSymbol;
            // its callers, and the callers of the interface member it implements (SeriesScreen's INetplayManager.AddSpectators(lobby.Spectators))
            var targets = new List<IMethodSymbol> { m.OriginalDefinition };
            foreach (var itf in m.ContainingType.AllInterfaces)
                foreach (var im in itf.GetMembers(m.Name).OfType<IMethodSymbol>())
                    if (SymbolEqualityComparer.Default.Equals(m.ContainingType.FindImplementationForInterfaceMember(im), m)) targets.Add(im.OriginalDefinition);
            foreach (var tree in ws.Compilation.SyntaxTrees)
            {
                if (!ok) break;
                var sm = ws.Model(tree);
                foreach (var ie in tree.GetRoot().DescendantNodes().OfType<InvocationExpressionSyntax>())
                {
                    var nm = ie.Expression switch { MemberAccessExpressionSyntax ma => ma.Name.Identifier.ValueText, IdentifierNameSyntax id => id.Identifier.ValueText, _ => null };
                    if (nm != m.Name) continue;
                    if (sm.GetOperation(ie) is not IInvocationOperation io || !targets.Any(t => SymbolEqualityComparer.Default.Equals(io.TargetMethod.OriginalDefinition, t))) continue;
                    var arg = io.Arguments.FirstOrDefault(a => a.Parameter?.Ordinal == p.Ordinal)?.Value;
                    var v = Strip(arg);
                    while (v is IConversionOperation c) v = c.Operand;
                    string cpp = v is IPropertyReferenceOperation mp && !mp.Property.IsStatic && gen.Models.IsModel(mp.Property.ContainingType) &&
                                 gen.Models.MemberOf(mp.Property.ContainingType, mp.Property.Name) is { } mem && (mem.CppType.StartsWith("List<") || mem.CppType.StartsWith("StrMap<") || mem.CppType.StartsWith("KeyMap<")) ? mem.CppType : null;
                    if (cpp == null) continue;   // another collection ([title], a sequence): copied into a temporary list at its call (Expr.cs)
                    if (found != null && found != cpp) { ok = false; break; }
                    found = cpp; sites++;
                }
            }
        }
        var r = ok && sites > 0 ? "tfex::gen::" + found + "*" : null;
        // no caller passes a model's own list (UpdateLobbySettings(.., mods): a ToList() copy): a concrete model list all the same, so the
        // function is callable from other units (an `auto` parameter isn't); the callers copy their sequence into a temporary one
        if (r == null && ok && p.Type is INamedTypeSymbol { TypeArguments.Length: 1 } mp1 && gen.Models.IsModel(mp1.TypeArguments[0]))
            r = $"tfex::gen::List<tfex::gen::ws::{mp1.TypeArguments[0].Name}, 16>*";
        modelListParams[p] = r;
        return r;
    }

    bool IsModelList(IOperation op)
    {
        op = Strip(op);
        if (op?.Type == null) return false;
        // x?.Linq(...): the placeholder for x is what x is (TF.EX's ArcherDataExtensions: player.ArcherMods?.Contains(...), a model's list)
        if (op is IConditionalAccessInstanceOperation)
            for (var up = op.Parent; up != null; up = up.Parent)
                if (up is IConditionalAccessOperation ca && ca.Operation.Syntax == op.Syntax) return IsModelList(ca.Operation);
        if (op is IParameterReferenceOperation mlp && modelLambdaParams.Contains(mlp.Parameter)) return true;
        // a parameter: what its callers pass decides (ModelListParamCpp), not the ICollection<string> guess below
        if (op is IParameterReferenceOperation pr0 && IsStringCollectionGuess(pr0.Parameter.Type)) return ModelListParamCpp(pr0.Parameter) is string ml0 && !ml0.Contains("StrMap<");
        if (op is IInvocationOperation { TargetMethod.Name: "ToList" or "ToArray" or "OrderBy" or "OrderByDescending" or "Where" or "Select" } lq && IsEnumerable(lq.TargetMethod))
            return false;   // a LINQ result over a model's list: a snapshot / sequence, not the struct's list
        // an ICollection<string> property of a TF.EX class (NetplayConfig.Players: PropCpp, a game List<string>), not a model's
        if (op is IPropertyReferenceOperation sp0 && IsStringCollectionGuess(sp0.Property.Type) && !gen.Models.IsModel(sp0.Property.ContainingType)) return false;
        if (IsModelCollection(op.Type)) return true;
        if (op.Type is not INamedTypeSymbol { IsGenericType: true } nt || nt.ContainingNamespace?.ToDisplayString() != "System.Collections.Generic") return false;
        if (op is IPropertyReferenceOperation p)
            return p.Property.IsIndexer ? p.Instance != null && IsModelList(p.Instance) : !p.Property.IsStatic && gen.Models.IsModel(p.Property.ContainingType) && gen.Models.MemberOf(p.Property.ContainingType, p.Property.Name) != null;
        if (op is ILocalReferenceOperation l) return modelLocals.Contains(l.Local);
        if (op is IParameterReferenceOperation pr) return ModelListParamCpp(pr.Parameter) is string ml1 && !ml1.Contains("StrMap<");
        if (op is IInvocationOperation inv && IsEnumerable(inv.TargetMethod) && inv.TargetMethod.Name is "FirstOrDefault" or "First" or "LastOrDefault" or "Last" or "ElementAt" && inv.Arguments.Length >= 1)
            return IsModelList(inv.Arguments.OrderBy(a => a.Parameter.Ordinal).First().Value);
        return false;
    }

    /* collections the game has no code for, as C++ containers in scanned GC memory (gen::ValMap / ValSet / VList, code_rt): a dictionary
     * over a TF.EX value type (IReadOnlyDictionary<int, NetworkStats>), a Dictionary / HashSet instantiation the Switch never compiled
     * (Dictionary<Variant, bool>, HashSet<int>: its type doesn't bind), a List of TF.EX structs or tuples (List<Input>) */
    enum VKind { None, Map, Set, List }
    /* Dictionary<string, V>.AlternateLookup<ReadOnlySpan<char>>: the dictionary type (a span key is its string here) */
    static ITypeSymbol AltLookupOf(ITypeSymbol t) =>
        t is INamedTypeSymbol { Name: "AlternateLookup", ContainingType: INamedTypeSymbol d } al && d.OriginalDefinition.ToDisplayString() == "System.Collections.Generic.Dictionary<TKey, TValue>" &&
        d.TypeArguments[0].SpecialType == SpecialType.System_String && al.TypeArguments.FirstOrDefault()?.ToDisplayString() == "System.ReadOnlySpan<char>" ? d : null;
    VKind ValKind(ITypeSymbol t)
    {
        t = Sub(t);
        if (AltLookupOf(t) is ITypeSymbol ald0) return ValKind(ald0);
        // ConcurrentDictionary (TF.EX's SkinStreamService: chunks arrive on its socket thread on the PC): the game has none; one game
        // thread here, so a value map (TryRemove = Remove(key, out value))
        // (a lobby model as the value, GameContext.ArcherSelections v0.28.0: its pointer, as Dictionary's)
        if (t is INamedTypeSymbol cn && cn.IsGenericType && cn.OriginalDefinition.ToDisplayString() == "System.Collections.Concurrent.ConcurrentDictionary<TKey, TValue>" &&
            cn.TypeArguments.Select((a, i) => a.SpecialType != SpecialType.None || a.IsReferenceType && (i == 1 || !gen.Models.IsModel(a)) && a is not ITypeParameterSymbol).All(ok => ok))
            return VKind.Map;
        // ConcurrentQueue<T> (TF.EX's MatchmakingService.gameThreadActions: its socket thread queues work for the game thread on the PC):
        // the game has none; a value list behind a lock (gen::CQueue: TF.EX's listen thread enqueues, the game thread drains)
        if (t is INamedTypeSymbol cq && cq.IsGenericType && cq.OriginalDefinition.ToDisplayString() == "System.Collections.Concurrent.ConcurrentQueue<T>" &&
            cq.TypeArguments[0] is { IsReferenceType: true } cqe && !gen.Models.IsModel(cqe) && cqe is not ITypeParameterSymbol)
            return VKind.List;
        // ConditionalWeakTable<K, V> (TF.State's owned state per entity: Icicle's fall counter, a MoonGlassBlock's explode state): the
        // Switch build has none; a value map keyed by the object (identity, as the PC). Weak on the PC (an entry goes with its object):
        // here its unit's MakeStatics clears it at each match's start (EnsureStateClasses), when the last match's objects are gone
        if (IsWeakTable(t)) return VKind.Map;
        if (t is not INamedTypeSymbol n || !n.IsGenericType || n.ContainingNamespace?.ToDisplayString() != "System.Collections.Generic") return VKind.None;
        var def = n.OriginalDefinition.ToDisplayString();
        bool Representable(ITypeSymbol a) => a.SpecialType != SpecialType.None || a.TypeKind == TypeKind.Enum || a.IsReferenceType && !gen.Models.IsModel(a) && a is not ITypeParameterSymbol ||
                                             IsPodS(a) || StructsGen.IsFnaValue(a) || a is INamedTypeSymbol { IsTupleType: true } tt && tt.TupleElements.All(e => Representable(e.Type));   // (Teams+'s Recolor cache: (Texture2D, Rectangle, Rectangle, Tint) keys)
        if (def is "System.Collections.Generic.IReadOnlyDictionary<TKey, TValue>" or "System.Collections.Generic.IDictionary<TKey, TValue>" or "System.Collections.Generic.Dictionary<TKey, TValue>")
        {
            // a lobby model as the value (NetplayManager.GetArchersInfo: GetPlayers().ToDictionary(seat, player)): its pointer, when the
            // game has no Dictionary<key, object> to hold it (Dictionary<int, object> isn't in the Switch's build)
            bool modelValue = gen.Models.IsModel(n.TypeArguments[1]) && Representable(n.TypeArguments[0]);
            if (!modelValue && !n.TypeArguments.All(Representable)) return VKind.None;
            if (IsPodS(n.TypeArguments[1])) return VKind.Map;
            var dict = ws.Compilation.GetTypeByMetadataName("System.Collections.Generic.Dictionary`2")!.Construct(n.TypeArguments[0], n.TypeArguments[1]);
            return GameLacks(dict) ? VKind.Map : VKind.None;
        }
        if (def is "System.Collections.Generic.HashSet<T>" or "System.Collections.Generic.ISet<T>" or "System.Collections.Generic.IReadOnlySet<T>")
        {
            if (!Representable(n.TypeArguments[0])) return VKind.None;
            var set = ws.Compilation.GetTypeByMetadataName("System.Collections.Generic.HashSet`1")!.Construct(n.TypeArguments[0]);
            return GameLacks(set) ? VKind.Set : VKind.None;
        }
        if (def is "System.Collections.Generic.List<T>" or "System.Collections.Generic.IList<T>" or "System.Collections.Generic.IReadOnlyList<T>" or "System.Collections.Generic.ICollection<T>" or "System.Collections.Generic.Queue<T>" &&
            (IsPodS(n.TypeArguments[0]) || n.TypeArguments[0] is INamedTypeSymbol { IsTupleType: true }) && Representable(n.TypeArguments[0]))
            return VKind.List;   // (a Queue of them too: Enqueue / Dequeue at the list's ends; ScenarioSweeper's Queue<Run>)
        // IEnumerable<a TF.EX struct> (GameContext.UpdateCurrentInputs(IEnumerable<Input>)): ours too; a sequence argument is copied in
        if (def is "System.Collections.Generic.IEnumerable<T>" or "System.Collections.Generic.IReadOnlyCollection<T>" && IsPodS(n.TypeArguments[0]))
            return VKind.List;
        // a list of an enum the game has no List of (FortRise's GAME MODE list, List<Modes>: the Switch's build never needed one): ours
        if (def is "System.Collections.Generic.List<T>" or "System.Collections.Generic.IList<T>" or "System.Collections.Generic.IReadOnlyList<T>" &&
            n.TypeArguments[0].TypeKind == TypeKind.Enum && GameLacks(ws.Compilation.GetTypeByMetadataName("System.Collections.Generic.List`1")!.Construct(n.TypeArguments[0])))
            return VKind.List;
        // TF.State's code: a list of a primitive the game has no List of (List<double>, ICollection<double>): ours
        if ((file.StartsWith("state/") || name == "GlueStateMsgPack") && def is "System.Collections.Generic.List<T>" or "System.Collections.Generic.IList<T>" or "System.Collections.Generic.IReadOnlyList<T>" or "System.Collections.Generic.ICollection<T>" &&
            n.TypeArguments[0].SpecialType is SpecialType.System_Double or SpecialType.System_Int64 or SpecialType.System_UInt64 or SpecialType.System_Int16 or SpecialType.System_UInt16 &&
            GameLacks(ws.Compilation.GetTypeByMetadataName("System.Collections.Generic.List`1")!.Construct(n.TypeArguments[0])))
            return VKind.List;
        // TF.EX's scenario sweep (ScenarioCatalog, ScenarioSweeper, ScenarioMapWriter, InputScripter, TFCommands' launch): a sequence of tuples
        // passed as a parameter (ScenarioSweeper.Start's runs, IEnumerable<(...)>): materialized, a value list
        if (name is "ScenarioCatalog" or "ScenarioSweeper" or "ScenarioMapWriter" or "InputScripter" or "ScenarioLaunch" &&
            def is "System.Collections.Generic.IEnumerable<T>" or "System.Collections.Generic.IReadOnlyCollection<T>" &&
            n.TypeArguments[0] is INamedTypeSymbol { IsTupleType: true } && Representable(n.TypeArguments[0]))
            return VKind.List;
        // a list of game objects the game has no List of (TF.EX's InputService: List<PlayerInput>, #283's spare / joining devices): ours (a
        // value list in scanned memory: it keeps its objects alive as the managed list would); only while TF.EX's code keeps it to itself
        if (def is "System.Collections.Generic.List<T>" or "System.Collections.Generic.IList<T>" or "System.Collections.Generic.IReadOnlyList<T>" or "System.Collections.Generic.ICollection<T>" &&
            n.TypeArguments[0].IsReferenceType && n.TypeArguments[0].SpecialType == SpecialType.None && !gen.Models.IsModel(n.TypeArguments[0]) &&
            n.TypeArguments[0] is not ITypeParameterSymbol && OriginOf(n.TypeArguments[0]) is not (Origin.Tfex or Origin.Bcl) &&
            GameLacks(ws.Compilation.GetTypeByMetadataName("System.Collections.Generic.List`1")!.Construct(n.TypeArguments[0])))
            return VKind.List;
        // a list of a TF.EX enum (List<NetplayRequest>): the game has no List<Enum>; ours, when the game lacks the instance
        if (def is "System.Collections.Generic.List<T>" or "System.Collections.Generic.IList<T>" or "System.Collections.Generic.IReadOnlyList<T>" or "System.Collections.Generic.ICollection<T>" &&
            n.TypeArguments[0].TypeKind == TypeKind.Enum && OriginOf(n.TypeArguments[0]) == Origin.Tfex &&
            GameLacks(ws.Compilation.GetTypeByMetadataName("System.Collections.Generic.List`1")!.Construct(n.TypeArguments[0])))
            return VKind.List;
        // any other List / Queue the game has no instantiation of (a code mod's: AIJimmy's Queue<MovementAction>, the Switch's Queue<object>;
        // LoaderAI's List<InputState>, a game struct by value): ours, a value list. The game can't hold one (it has no such type), so only
        // translated code ever sees it
        if (gen.IsCodeModFile(file) && def is "System.Collections.Generic.List<T>" or "System.Collections.Generic.IList<T>" or "System.Collections.Generic.IReadOnlyList<T>" or
                   "System.Collections.Generic.ICollection<T>" or "System.Collections.Generic.Queue<T>" &&
            n.TypeArguments[0] is not ITypeParameterSymbol and not IArrayTypeSymbol && !gen.Models.IsModel(n.TypeArguments[0]) &&
            n.TypeArguments[0].ContainingNamespace?.ToDisplayString() is not ("System.Collections.Generic" or "System.Collections.Concurrent") &&   // (not a collection of collections: their own paths)
            (Representable(n.TypeArguments[0]) || n.TypeArguments[0].TypeKind == TypeKind.Struct && OriginOf(n.TypeArguments[0]) == Origin.Game) &&
            GameLacks(ws.Compilation.GetTypeByMetadataName(def == "System.Collections.Generic.Queue<T>" ? "System.Collections.Generic.Queue`1" : "System.Collections.Generic.List`1")!.Construct(n.TypeArguments[0])))
            return VKind.List;
        return VKind.None;
    }
    /* the Switch has no type for this instantiation (the binding: pass 1 asks, pass 2 knows) */
    bool GameLacks(INamedTypeSymbol t)
    {
        int n = methodKeys.Count;
        var k = B.Need("t", SwitchName(t));
        bool lacks = !Bound(k);
        if (lacks && methodKeys.Count > n) methodKeys.RemoveRange(n, methodKeys.Count - n);   // a probe: not a member this method uses
        return lacks;
    }
    bool IsWeakTable(ITypeSymbol t) => Sub(t) is INamedTypeSymbol { IsGenericType: true } w &&
        w.OriginalDefinition.ToDisplayString() == "System.Runtime.CompilerServices.ConditionalWeakTable<TKey, TValue>" &&
        w.TypeArguments.All(a => a.IsReferenceType && !gen.Models.IsModel(a) && a is not ITypeParameterSymbol);
    /* System.Runtime.CompilerServices.StrongBox<T> (a ConditionalWeakTable's value box): gen::Box<T> (runtime arena, never freed) */
    /* System.Linq.IGrouping<K, E> (LINQ GroupBy's groups): gen::Group<K, E>* */
    static bool IsGrouping(ITypeSymbol t) => t is INamedTypeSymbol { IsGenericType: true } g && g.OriginalDefinition.ToDisplayString() == "System.Linq.IGrouping<TKey, TElement>";
    static bool IsStrongBox(ITypeSymbol t) => t is INamedTypeSymbol { IsGenericType: true } sb && sb.OriginalDefinition.ToDisplayString() == "System.Runtime.CompilerServices.StrongBox<T>";
    bool IsValMap(ITypeSymbol t) => ValKind(t) == VKind.Map;
    bool IsValColl(ITypeSymbol t) => ValKind(t) != VKind.None;
    /* an IEnumerable<primitive> parameter outside TF.State (InputService.EnsureRemoteControllers(IEnumerable<int> seats), given
     * Enumerable.Range or a LINQ chain over the lobby's players): a value list of ours, the argument copied in at the call. Only the
     * parameter: an expression of that type elsewhere (seats.Where(...), IReadOnlyDictionary.Keys) stays a lazy sequence. (TF.State's are
     * the game's Lists: StateEnumListOf.) */
    bool IsPrimSeqParam(IParameterSymbol p) => (IsPrimEnumerable(p.Type) || IsGameObjEnumerable(p.Type) && p.ContainingSymbol.DeclaredAccessibility == Accessibility.Private) &&
        StateEnumListOf(p) == null &&
        p.ContainingSymbol?.DeclaringSyntaxReferences.FirstOrDefault()?.SyntaxTree.FilePath is string pf && !pf.StartsWith("state/");
    /* ... and IEnumerable<a game class> the same way for a private method (LocalPlayersPanel.DistinctTextures(IEnumerable<Subtexture>),
     * v0.28.0: given joinable.Select(input => input.StartIcon)): a value list of the references, in scanned GC memory. Only private ones: a
     * public one is also given the game's own collections (MatchStatsReport.RecordAwards(awards): the game's list, passed as it is) */
    bool IsGameObjEnumerable(ITypeSymbol t) =>
        t is INamedTypeSymbol { TypeArguments.Length: 1 } n && n.OriginalDefinition.ToDisplayString() == "System.Collections.Generic.IEnumerable<T>" &&
        n.TypeArguments[0] is INamedTypeSymbol { TypeKind: TypeKind.Class } et && OriginOf(et) == Origin.Game;
    string PrimSeqCpp(IParameterSymbol p) => $"gen::VList<{Cpp(((INamedTypeSymbol)p.Type).TypeArguments[0])}>";
    static bool IsPrimEnumerable(ITypeSymbol t) =>
        t is INamedTypeSymbol { TypeArguments.Length: 1 } n &&
        n.OriginalDefinition.ToDisplayString() is "System.Collections.Generic.IEnumerable<T>" or "System.Collections.Generic.IReadOnlyCollection<T>" &&
        n.TypeArguments[0].SpecialType is SpecialType.System_Int32 or SpecialType.System_Int64 or SpecialType.System_Single or SpecialType.System_Double or SpecialType.System_Boolean;
    string ValCpp(ITypeSymbol t)
    {
        var n = (INamedTypeSymbol)t;
        return ValKind(t) switch
        {
            // a map TF.State keeps more than 64 entries in (translate.toml [capacity], by the C# type: SFXService / MatchStatsService keep
            // SFX_SNAPSHOT_HISTORY = 960 frames of snapshots)
            VKind.Map => cfg.Table("capacity").TryGetValue(t.ToDisplayString(), out var capv) && capv is long capn
                ? $"gen::ValMap<{ValKey(n.TypeArguments[0])}, {Cpp(n.TypeArguments[1])}, {capn}>"
                : $"gen::ValMap<{ValKey(n.TypeArguments[0])}, {Cpp(n.TypeArguments[1])}>",
            // a set filled past 64 (translate.toml [capacity], by the C# type: AIJimmy's path search's visited cells)
            VKind.Set => cfg.Table("capacity").TryGetValue(t.ToDisplayString(), out var caps) && caps is long capsn
                ? $"gen::ValSet<{ValKey(n.TypeArguments[0])}, {capsn}>" : $"gen::ValSet<{ValKey(n.TypeArguments[0])}>",
            VKind.List when n.OriginalDefinition.ToDisplayString() == "System.Collections.Concurrent.ConcurrentQueue<T>" => $"gen::CQueue<{Cpp(n.TypeArguments[0])}>",
            // a list of big value structs TF.State keeps few of (translate.toml [capacity]: List<MatchStats> holds one per player, and the
            // default 128 inline entries of 264 bytes made every GameState allocate 33 KB: the GC's main load at 240 Hz)
            _ => cfg.Table("capacity").TryGetValue(t.ToDisplayString(), out var capl) && capl is long capln
                ? $"gen::VList<{Cpp(n.TypeArguments[0])}, {capln}>"
                : $"gen::VList<{Cpp(n.TypeArguments[0])}>",
        };
    }
    /* a value container's key: a string is equal by content (gen::StrK), anything else by C++ == (a game object by identity, as C#) */
    string ValKey(ITypeSymbol k) => k.SpecialType == SpecialType.System_String ? "gen::StrK" : Cpp(k);
    static IOperation Strip(IOperation op) { while (op is IConversionOperation c && c.IsImplicit) op = c.Operand; return op; }

    /* ICollection<Player>, List<string> ... held by a lobby model: in code, a pointer to the struct's list (C++ `auto`) */
    bool IsModelCollection(ITypeSymbol t) =>
        t is INamedTypeSymbol n && n.IsGenericType && n.TypeArguments.Length == 1 && n.ContainingNamespace?.ToDisplayString() == "System.Collections.Generic" &&
        (gen.Models.IsModel(n.TypeArguments[0]) || (n.TypeArguments[0] is INamedTypeSymbol e && e.IsGenericType && gen.Models.IsModel(e.TypeArguments.FirstOrDefault())) ||
         // an interface over strings (ICollection<string> variants): what TF.EX passes there is a model's string list (GameData.Variants)
         IsStringCollectionGuess(t));
    static bool IsStringCollectionGuess(ITypeSymbol t) =>
        t is INamedTypeSymbol { Name: "ICollection", TypeArguments.Length: 1 } n && n.TypeArguments[0].SpecialType == SpecialType.System_String;

    /* The Switch's (rtypes) name of a type: namespace.Name, nested with +, generic instances Name`n[Arg,...]; TF.EX classes in a generic
     * argument become their nearest game base (List<Notification> is List<Entity> on the Switch: docs/ws-tfex-subclass.md row 5). */
    int switchNameDepth;   /* a self-referencing generic (T : IThing<T>): no endless recursion */
    string SwitchName(ITypeSymbol t)
    {
        if (switchNameDepth > 12) return "System.Object";
        switchNameDepth++;
        try { return SwitchName0(subst.Count > 0 ? Sub(t) : t); } finally { switchNameDepth--; }   // (a generic copy: Sprite<T> is Sprite<int>)
    }
    string SwitchName0(ITypeSymbol t)
    {
        if (t.SpecialType == SpecialType.System_String) return "System.String";
        // an array of a struct of ours (ChangelogPanel's Segment[]): no game type; an object[] holds it (BRUTE's NewArray takes the element
        // size apart, and Boehm scans the block conservatively, so the structs' references stay alive)
        if (t is IArrayTypeSymbol pa && (IsPodS(pa.ElementType) || pa.ElementType is IArrayTypeSymbol)) return "System.Object[]";   // (and arrays of arrays: byte[][], the same gen::Arr elements)
        // TF.State's code: an array of a game class the game has no array type for (Lantern[], from ToArray()): an object[] holds it (.NET
        // arrays of references are covariant; BRUTE's NewArray only needs the element size)
        if (t is IArrayTypeSymbol ra && ra.ElementType.IsReferenceType && ra.ElementType.SpecialType == SpecialType.None && file.StartsWith("state/"))
        {
            var an = SwitchName(ra.ElementType) + "[]";
            int n0 = methodKeys.Count;
            if (!Bound(B.Need("t", an))) { if (methodKeys.Count > n0) methodKeys.RemoveRange(n0, methodKeys.Count - n0); return "System.Object[]"; }
            return an;
        }
        if (t is IArrayTypeSymbol a) return SwitchName(a.ElementType) + "[]";
        if (t is ITypeParameterSymbol tp)   // List<T> where T : Entity: the instantiation over the constraint (shared generic code)
            return SwitchName(tp.ConstraintTypes.FirstOrDefault(c => c.IsReferenceType) ?? ws.Compilation.GetSpecialType(SpecialType.System_Object));
        var nt = (INamedTypeSymbol)t;
        while (OriginOf(nt) == Origin.Tfex && nt.BaseType != null) nt = nt.BaseType;
        if (OriginOf(nt) == Origin.Tfex && nt.TypeKind == TypeKind.Interface) return "System.Object";   // an interface of ours: objects of any class (List<IResourceInfo>)
        // FortRise's interfaces as TF.EX sees them (its FortRise reference: List<IArcherEntry> from IModArchers.GetAllArchers): the same, as
        // the translated FortRise registry made them (System.Object)
        if (nt.TypeKind == TypeKind.Interface && nt.ContainingNamespace?.ToDisplayString() is string ns && (ns == "FortRise" || ns.StartsWith("FortRise."))) return "System.Object";
        var baseName = nt.ContainingType != null ? SwitchName(nt.ContainingType).Split('[')[0] + "+" + nt.MetadataName
                     : (nt.ContainingNamespace.IsGlobalNamespace ? "" : nt.ContainingNamespace.ToDisplayString() + ".") + nt.MetadataName;
        if (nt.IsGenericType && !nt.IsUnboundGenericType && nt.TypeArguments.Length > 0 && !SymbolEqualityComparer.Default.Equals(nt, nt.OriginalDefinition))
            baseName += "[" + string.Join(", ", nt.TypeArguments.Select(SwitchName)) + "]";   // rtypes: "Dictionary`2[System.Int32, Monocle.Layer]"
        // translate.toml [instantiation]: a generic instantiation BRUTE never compiled -> one it did, for objects our code creates
        // (FortRise's Dictionary<string, Monocle.Texture>: the <string, object> one; reference values, nothing casts them)
        if (cfg.Root.TryGetValue("instantiation", out var inst) && inst is Tomlyn.Model.TomlTable it && it.TryGetValue(baseName, out var to) && to is Tomlyn.Model.TomlTable tt)
        { gen.Rules.Use("instantiation", baseName); return (string)tt["as"]; }
        return baseName;
    }

    /* PC IL parameter types (tools/re sigs.py form) */
    string IlName(ITypeSymbol t)
    {
        var s = t.SpecialType switch
        {
            SpecialType.System_Single => "float32", SpecialType.System_Double => "float64", SpecialType.System_Int32 => "int32",
            SpecialType.System_UInt32 => "uint32", SpecialType.System_Int64 => "int64", SpecialType.System_Int16 => "int16",
            SpecialType.System_Byte => "uint8", SpecialType.System_SByte => "int8", SpecialType.System_Char => "char",
            SpecialType.System_Boolean => "bool", SpecialType.System_String => "string", SpecialType.System_Object => "object", _ => null,
        };
        if (s != null) return s;
        if (t is IArrayTypeSymbol a) return IlName(a.ElementType) + "[]";
        if (t is ITypeParameterSymbol tp) return tp.Name;
        var nt = (INamedTypeSymbol)t;
        var n = nt.ContainingType != null ? IlName(nt.ContainingType) + "+" + nt.MetadataName
              : (nt.ContainingNamespace.IsGlobalNamespace ? "" : nt.ContainingNamespace.ToDisplayString() + ".") + nt.MetadataName;
        if (nt.IsGenericType && !SymbolEqualityComparer.Default.Equals(nt, nt.OriginalDefinition))
            n += "<" + string.Join(",", nt.TypeArguments.Select(IlName)) + ">";
        return n;
    }
    string IlSig(IMethodSymbol m) => string.Join(",", m.OriginalDefinition.Parameters.Select(p => IlName(p.Type) + (p.RefKind != RefKind.None ? "&" : "")));

    /* a method returning a model's collection (Lobby.SessionPlayers => MatchPlayers.Count > 0 ? MatchPlayers : Players): its C++ type when
     * every returned value is a model member of one list type (`auto` can't cross units) */
    string ReturnedModelList(IMethodSymbol m)
    {
        if (m.DeclaringSyntaxReferences.Length == 0) return null;
        var syn = m.DeclaringSyntaxReferences[0].GetSyntax();
        if (syn.SyntaxTree != model.SyntaxTree) return null;
        if (syn is PropertyDeclarationSyntax pds && pds.ExpressionBody != null) syn = pds.ExpressionBody;
        else if (syn is ArrowExpressionClauseSyntax) { }
        var op = M(syn).GetOperation(syn);
        if (op == null) return null;
        var types = new HashSet<string>();
        void Add(IOperation v)
        {
            v = Strip(v);
            while (v is IConversionOperation c) v = c.Operand;
            if (v is IConditionalOperation co && co.WhenFalse != null) { Add(co.WhenTrue); Add(co.WhenFalse); return; }
            if (v is ISwitchExpressionOperation swe) { foreach (var arm in swe.Arms) Add(arm.Value); return; }   // (PlayerProfilePanel.FilteredMonths)
            // [.. seq] into a model collection: the temporary model list the expression makes (UnitEmitter.Expr CollectionExpr)
            if (v is ICollectionExpressionOperation { Elements.Length: 1 } ce1 && ce1.Elements[0] is ISpreadOperation &&
                ce1.Type is INamedTypeSymbol { TypeArguments.Length: 1 } cet && gen.Models.IsModel(cet.TypeArguments[0]))
            { types.Add($"List<tfex::gen::ws::{cet.TypeArguments[0].Name}, 8>"); return; }
            var mem = v is IPropertyReferenceOperation pr && gen.Models.IsModel(pr.Property.ContainingType) ? gen.Models.MemberOf(pr.Property.ContainingType, pr.Property.Name) : null;
            types.Add(mem != null && mem.CppType.StartsWith("List<") ? mem.CppType : "?");
        }
        foreach (var r in op.DescendantsAndSelf().OfType<IReturnOperation>().Where(r => r.ReturnedValue != null && !InLambda(r))) Add(r.ReturnedValue);
        if (types.Count != 1 || types.Contains("?")) return null;
        includes.Add("tfexgen_Models.hpp");
        return "tfex::gen::" + types.First() + "*";   // List<ws::T, N> is tfex::gen::List
    }

    bool coMode;   /* translating an iterator's coroutine twin: yield -> co_yield */
    static bool IsCoroutineIterator(IMethodSymbol m) => m.IsIterator && m.ReturnType.ToDisplayString() == "System.Collections.IEnumerator";

    /* ---------------------------------------------------------------- declarations ---------------------------------------------------------------- */
    string Signature(IMethodSymbol m, bool header)
    {
        var ps = m.Parameters.Select(p =>
        {
            // an ICollection<string> parameter its callers don't pass a model list: an ordinary collection (the game's), not the guess's `auto`
            var t = (ModelListParamCpp(p) ?? (IsStringCollectionGuess(p.Type) ? "void*" : IsPrimSeqParam(p) ? PrimSeqCpp(p) + "*" : Cpp(p.Type))) + (p.RefKind is RefKind.Ref or RefKind.Out ? "&" : "");   // `in`: by value (read-only; FortRise's ArcherLoader passes `default` and with-expressions)
            // a string default in the header: its own function-local literal (the unit's literal table is in the .cpp)
            var d = header && p.HasExplicitDefaultValue && m.TypeParameters.Length == 0 ? " = " +   /* (a generic method's T_type parameters come last: no defaults then) */ (p.ExplicitDefaultValue is string ds
                ? $"[]() {{ static gen::LitN<{ds.Length + 1}> l_{{u{CppU16(ds)}}}; return l_.P(); }}()" : Literal(p.ExplicitDefaultValue, p.Type)) : "";
            if (header && p.IsParams && d == "") d = " = {}";   // params T[]: none passed (C++ needs a default after defaulted parameters)
            return $"{t} {Id(p.Name)}{d}";
        }).ToList();
        if (m.ContainingType is { IsExtension: true, ExtensionParameter: IParameterSymbol ep } && !m.IsStatic) ps.Insert(0, $"{Cpp(ep.Type)} {Id(ep.Name)}");   // an extension member: its receiver
        else if (!m.IsStatic && m.MethodKind != MethodKind.Constructor) ps.Insert(0, gen.Models.IsModel(type) ? $"tfex::gen::ws::{type.Name}* self" : IsPodS(type) ? $"{gen.Structs.CppName(type)} self" : "void* self");   // a struct: by value (a mutating struct method is reported)
        if (instSuffix == null) foreach (var tp in m.TypeParameters) ps.Add($"void* {tp.Name}_type");   // generic over a class: T's runtime type (`is T`, `as T`, OfType<T>)
        if (m.IsIterator)
        {
            ps.Add("F&& yield_");
            return $"template <class F> static bool {MName(m)}({string.Join(", ", ps)})";
        }
        if (m.MethodKind == MethodKind.Constructor) return $"void Ctor{CtorSuffix(m)}({string.Join(", ", new[] { "void* self" }.Concat(ps))})";
        var ret = ReturnsSnap(m) ? $"gen::Snap<{SnapElem(m)}>" : IsModelCollection(m.ReturnType) && ReturnedModelList(m) is string ml ? ml : Cpp(m.ReturnType);
        return $"{ret} {MName(m)}{instSuffix}({string.Join(", ", ps)})";
    }
    /* a TF.State generic method's copy for one instantiation (CodeGen.MethodInsts): the name's suffix while it is emitted */
    string instSuffix;
    public static string MethodInstSuffix(IEnumerable<ITypeSymbol> args) => "__" + string.Join("_", args.Select(a => a.MetadataName.Replace("`", "")));

    void EmitField(IFieldSymbol f, StringBuilder h, StringBuilder c)
    {
        current = null;
        if (gen.AbsentRule(f) is { } ar && ar.ContainsKey("absent")) return;   // translate.toml [fortrise] absent: no storage, its uses read the default
        DataMembers.Add(f);
        var v = f.DeclaringSyntaxReferences[0].GetSyntax() as VariableDeclaratorSyntax;
        if (f.IsConst && f.Type.SpecialType == SpecialType.System_String) return;   // inlined at every use (Literal): nothing to emit
        if (f.IsConst)
        {
            var line = $"constexpr {Cpp(f.Type)} {Id(f.Name)} = {Literal(f.ConstantValue, f.Type)};\n";
            if (f.DeclaredAccessibility is Accessibility.Public or Accessibility.Internal) h.Append(line); else c.Append(line);
            return;
        }
        if (HostedInstanceField(f))
        {   // a hosted unit's instance field (a mod's FortRise.Mod subclass: FortRise makes one object per mod; the Switch keeps none): this
            // unit's static storage, written by the constructor's initializers (UnitEmitter.Class), read and written by every unit
            var ht = StorableCpp(f.Type);
            bool pubH = f.DeclaredAccessibility is Accessibility.Public or Accessibility.Internal;
            if (pubH) h.Append($"{ht}& {Id(f.Name)}();\n");
            c.Append(HoldsRef(f.Type)
                ? $"{(pubH ? "" : "static ")}{ht}& {Id(f.Name)}() {{   /* {f.ToDisplayString()}: a hosted object's field, a GC root */\n    static {ht}* p_ = nullptr;\n    if (!p_) {{ gen::RootStatic(p_); }}\n    return *p_;\n}}\n"
                : $"{(pubH ? "" : "static ")}{ht}& {Id(f.Name)}() {{   /* {f.ToDisplayString()}: a hosted object's field */\n    static {ht} v_{{}};\n    return v_;\n}}\n");
            R.Add("mechanical: a hosted unit's instance field -> its static storage (one object per mod)", $"{f.ContainingType.Name}.{f.Name}");
            return;
        }
        if (!f.IsStatic || isClass) return;   // instance fields and a class's statics: ClassPrelude
        var init = v?.Initializer?.Value;
        var iop = init == null ? null : InitOp(init);
        // static readonly T[] X = { literals }: a lazily built array of literal objects (never handed to game code)
        var arrInit = iop as IArrayInitializerOperation ?? (iop as IArrayCreationOperation)?.Initializer;
        if (f.Type is IArrayTypeSymbol at && arrInit != null)
        {
            var el = Cpp(at.ElementType);
            var items = arrInit.ElementValues.Select(Expr).ToList();
            bool pubA = f.DeclaredAccessibility is Accessibility.Public or Accessibility.Internal;   // (other units read it: SkinLimits.AllowedExtraDataKeys)
            if (pubA) h.Append($"gen::Arr<{el}> {Id(f.Name)}();\n");
            c.Append($"{(pubA ? "" : "static ")}gen::Arr<{el}> {Id(f.Name)}() {{   /* {f.ToDisplayString()} */\n");
            if (HoldsRef(at.ElementType))   // its elements in scanned memory (gen::RootStatic): a non-literal element is a managed object
                c.Append($"    static gen::RootArr<{el}, {items.Count}>* root_ = nullptr;\n    {el}* items = gen::RootStatic(root_).v;\n    static bool made = false;\n");
            else
                c.Append($"    static {el} items[{items.Count}];\n    static bool made = false;\n");
            c.Append($"    if (!made) {{\n");
            for (int i = 0; i < items.Count; i++) c.Append($"        items[{i}] = {items[i]};\n");
            c.Append($"        made = true;\n    }}\n    return {{items, {items.Count}}};\n}}\n");
            fieldFns.Add(f);
            return;
        }
        var t = StorableCpp(f.Type);
        // a Harmony FieldRef made by reflection (AccessTools.(Static)FieldRefAccess): every invoke reads the field itself (Expr.cs), so the
        // reflection never runs on the Switch; the field stays, null
        if (f.Type is INamedTypeSymbol { Name: "FieldRef", ContainingType: var frc } && frc?.ToDisplayString() == "HarmonyLib.AccessTools" &&
            Strip(iop) is IInvocationOperation fri0 && RefField(fri0) != null)
        {
            R.Add("mechanical: a Harmony FieldRef field -> unused (its invokes read the game field)", $"{f.ContainingType.Name}.{f.Name}");
            return;
        }
        // a delegate made by reflection over a static property setter (LevelPatch.SetEngineTimeMult): every invoke is the setter's call
        // (Expr.cs SetterInvoke), so the reflection never runs on the Switch; the field isn't emitted
        if (ResolvedReflectionField(f) && ReflectedSetterField(f) == null)
        {   // a FieldInfo / MethodInfo field holding a reflection lookup resolved at translate time: its uses are direct code
            R.Add("mechanical: a reflection field resolved at translate time -> no storage (its uses are direct code)", $"{f.ContainingType.Name}.{f.Name}");
            return;
        }
        if (ReflectedSetterField(f) is IPropertySymbol rsp)
        {
            R.Add("mechanical: a reflected property-setter delegate field -> unused (its invokes call the setter)", $"{f.ContainingType.Name}.{f.Name}: {rsp.ContainingType.Name}.{rsp.Name}");
            return;
        }
        var initText = iop != null ? Expr(iop) : null;
        if (LazyStatic(f))
        {   // not a constant: made at first use (C#'s static constructor runs at the first access; a C++ static initializer would run at
            // module load, before the game exists: Auto_Constants.NETPLAY_SAFE_MAP's List crashed the boot)
            bool pubL = f.DeclaredAccessibility is Accessibility.Public or Accessibility.Internal;
            if (pubL) h.Append($"{t}& {Id(f.Name)}();\n");
            lazyMakers.Add($"{Id(f.Name)}()");
            if (IsWeakTable(f.Type)) lazyMakers.Add($"{Id(f.Name)}()->Clear()");   /* a ConditionalWeakTable: the last match's objects out */
            if (HoldsRef(f.Type))   // the storage in scanned memory (gen::RootStatic), made with the first access
                c.Append($"{(pubL ? "" : "static ")}{t}& {Id(f.Name)}() {{   /* {f.ToDisplayString()}: a GC root, made at first use */\n" +
                         $"    static {t}* p_ = nullptr;\n    if (!p_) {{ gen::RootStatic(p_){(initText != null ? " = " + initText : "")}; }}\n    return *p_;\n}}\n");
            else
                c.Append($"{(pubL ? "" : "static ")}{t}& {Id(f.Name)}() {{   /* {f.ToDisplayString()}: made at first use */\n" +
                         $"    static {t} v_{{}}; static bool made_ = false;\n    if (!made_) {{ made_ = true; v_ = {initText}; }}\n    return v_;\n}}\n");
            return;
        }
        if (initText != null && initText.Contains("[&]")) initText = $"[]() {{ return {initText}; }}()";   // C++: no capture-default at namespace scope
        var line2 = $"{t} {Id(f.Name)}" + (initText != null ? " = " + initText : "{}") + ";   /* " + f.ToDisplayString() + " */\n";
        if (f.DeclaredAccessibility is Accessibility.Public or Accessibility.Internal) { h.Append("extern " + t + " " + Id(f.Name) + ";\n"); c.Append(line2); }
        else c.Append("static " + line2);
    }
    /* the unit's statics made at first use (their accessors): MakeStatics() makes them all at once. TF.State's units are made before a
     * match (EnsureStateClasses): one made inside a rollback window is undone by the restore under the module's pointer (the glue's
     * entity cache crashed GetState after tower 1's first rollback, 2026-10-21) */
    readonly List<string> lazyMakers = new();
    readonly HashSet<IFieldSymbol> fieldFns = new(SymbolEqualityComparer.Default);
    /* a static field of a static (non-class) unit whose initializer isn't a constant: an accessor that makes it at first use (every
     * unit decides the same way from the symbol, so callers in other units add the `()` too) */
    /* a static array field with a literal initializer of a static unit: its accessor function (EmitField), from any unit */
    bool ArrayFieldFn(IFieldSymbol f) =>
        f.IsStatic && !f.IsConst && f.Type is IArrayTypeSymbol && !IsClassType(f.ContainingType) &&
        LiteralArrayInit((f.DeclaringSyntaxReferences.FirstOrDefault()?.GetSyntax() as VariableDeclaratorSyntax)?.Initializer?.Value);
    /* `{ a, b }`, `new T[] { a, b }`, `new[] { a, b }`: a literal array (not `new T[n]`, an empty array of a size: made at first use) */
    static bool LiteralArrayInit(ExpressionSyntax e) =>
        e is InitializerExpressionSyntax or ImplicitArrayCreationExpressionSyntax || e is ArrayCreationExpressionSyntax { Initializer: not null };
    /* an instance field of a hosted unit's type (no object of ours: FortRise's Mod subclass of a code mod) without a [service] rule */
    bool HostedInstanceField(IFieldSymbol f)
    {
        if (f.IsStatic || f.IsConst || f.AssociatedSymbol != null || !IsClassType(f.ContainingType)) return false;
        return HostedModType(f.ContainingType) && gen.ServiceRule(f) == null;
    }
    /* a hosted unit's FortRise.Mod subclass (a code mod's module: FortRise makes one object per mod, the Switch keeps none). Only a
     * FortRise.Mod subclass: a MonoMod patch class's or a hand-made service's object exists */
    bool HostedModType(INamedTypeSymbol t)
    {
        if (!IsClassType(t)) return false;
        bool modClass = false;
        for (var b = t.BaseType; b != null; b = b.BaseType) if (b.ToDisplayString() == "FortRise.Mod") modClass = true;
        if (!modClass) return false;
        return SymbolEqualityComparer.Default.Equals(t, type) ? !isClass :
               OtherUnit(t) is TomlTable hu && hu.TryGetValue("hosted", out var hv) && hv is true;
    }
    bool LazyStatic(IFieldSymbol f)
    {
        // a hosted unit's type (a class whose object is hand-written, no g_s): its static fields are this file's statics, made at first use
        // as a static unit's are (InputService's #283 lists: their initializers allocated at module load, before the game's GC, and crashed the boot)
        bool hosted = IsClassType(f.ContainingType) && (SymbolEqualityComparer.Default.Equals(f.ContainingType, type) ? !isClass :
                      OtherUnit(f.ContainingType) is TomlTable hu && hu.TryGetValue("hosted", out var hv) && hv is true);
        if (!f.IsStatic || f.IsConst || (IsClassType(f.ContainingType) && !hosted)) return false;
        var vd0 = f.DeclaringSyntaxReferences.FirstOrDefault()?.GetSyntax() as VariableDeclaratorSyntax;
        if (f.Type is IArrayTypeSymbol)   // a literal array initializer has its own accessor (EmitField); any other array (`new int[8]`:
            return !LiteralArrayInit(vd0?.Initializer?.Value);   // Teams+'s Teams3, a global made at module load crashed the boot) is rooted, made at first use
        if (HoldsRef(f.Type)) return true;   // its storage is a GC root (gen::RootStatic)
        if (f.DeclaringSyntaxReferences.FirstOrDefault()?.GetSyntax() is not VariableDeclaratorSyntax { Initializer: not null } vd) return false;
        var op = ws.Model(vd.SyntaxTree).GetOperation(vd.Initializer.Value);
        while (op is IConversionOperation c) op = c.Operand;
        return op != null && !op.ConstantValue.HasValue && op is not IDefaultValueOperation && op is not ILiteralOperation;
    }

    /* a value the GC must see: a managed reference (string, game object, delegate, TF.EX class instance), a managed array, or a nullable /
     * plain-data struct holding one; not a lobby model (C++ data) or a value container (already uncollectable) */
    bool HoldsRef(ITypeSymbol t)
    {
        if (t is IArrayTypeSymbol) return true;
        if (gen.Models.IsModel(t) || IsModelCollection(t) || IsValColl(t)) return false;
        if (t is INamedTypeSymbol n && IsNullable(n)) return HoldsRef(n.TypeArguments[0]);
        if (t.IsReferenceType) return true;
        if (t is INamedTypeSymbol sn && IsPodS(sn)) return StructsGen.Fields(sn).Any(sf => HoldsRef(sf.Type));
        return false;
    }

    /* a static auto-property: its storage and get_X/set_X (instance ones of a class: a field of Self, ClassPrelude) */
    void EmitAutoProperty(IPropertySymbol p, StringBuilder h, StringBuilder c)
    {
        DataMembers.Add(p);
        // a hosted FortRise.Mod subclass's instance auto-property (AIJimmy's LoaderAIModApi { get; private set; }): static storage, as
        // its instance fields (HostedInstanceField: one object per mod), its accessors taking the receiver they don't need
        bool hostedInst = !p.IsStatic && !isClass && HostedModType(p.ContainingType) && gen.ServiceRule(p) == null;
        if ((!p.IsStatic && !hostedInst) || isClass) return;   // a class's static auto-property: ClassPrelude (g_s)
        var t = Cpp(p.Type);
        var init = (p.DeclaringSyntaxReferences[0].GetSyntax() as PropertyDeclarationSyntax)?.Initializer?.Value;
        current = null;
        var iop0 = init != null ? M(init).GetOperation(init) : null;
        var initText = iop0 != null ? Expr(iop0) : Literal(null, p.Type);
        while (iop0 is IConversionOperation ic0) iop0 = ic0.Operand;
        if (HoldsRef(p.Type))
        {   // a GC root (gen::RootStatic, see LazyStatic): the storage behind get_X / set_X
            lazyMakers.Add($"{Id(p.Name)}_s()");
            c.Append($"static {t}& {Id(p.Name)}_s() {{   /* {p.ToDisplayString()}: a GC root, made at first use */\n    static {t}* p_ = nullptr;\n" +
                     $"    if (!p_) {{ gen::RootStatic(p_) = {initText}; }}\n    return *p_;\n}}\n#define {Id(p.Name)}_ {Id(p.Name)}_s()\n");
        }
        else if (iop0 != null && !iop0.ConstantValue.HasValue && iop0 is not IDefaultValueOperation && iop0 is not ILiteralOperation)
        {   // made at first use (see LazyStatic): the storage behind get_X / set_X
            lazyMakers.Add($"{Id(p.Name)}_s()");
            c.Append($"static {t}& {Id(p.Name)}_s() {{   /* {p.ToDisplayString()}: made at first use */\n    static {t} v_{{}}; static bool made_ = false;\n" +
                     $"    if (!made_) {{ made_ = true; v_ = {initText}; }}\n    return v_;\n}}\n#define {Id(p.Name)}_ {Id(p.Name)}_s()\n");
        }
        else
        {
            if (initText.Contains("[&]")) initText = $"[]() {{ return {initText}; }}()";   // C++: no capture-default at namespace scope
            c.Append($"static {t} {Id(p.Name)}_ = {initText};   /* {p.ToDisplayString()} */\n");
        }
        bool pub = p.DeclaredAccessibility is Accessibility.Public or Accessibility.Internal;
        var selfP = hostedInst ? "void*, " : "";
        if (hostedInst) R.Add("mechanical: a hosted unit's instance auto-property -> its static storage (one object per mod)", $"{p.ContainingType.Name}.{p.Name}");
        if (pub) h.Append($"{t} get_{p.Name}({(hostedInst ? "void*" : "")});\nvoid set_{p.Name}({selfP}{t} v);\n");
        c.Append($"{(pub ? "" : "static ")}{t} get_{p.Name}({(hostedInst ? "void*" : "")}) {{ return {Id(p.Name)}_; }}\n{(pub ? "" : "static ")}void set_{p.Name}({selfP}{t} v) {{ {Id(p.Name)}_ = v; }}\n");
    }

    /* ---------------------------------------------------------------- bodies ---------------------------------------------------------------- */
    string MethodBody(IMethodSymbol m)
    {
        var syntax = m.DeclaringSyntaxReferences[0].GetSyntax();
        var sb = new StringBuilder();
        if (Replacement(m) is string rep) return rep;
        // [MonoModLinkTo("T", "M")] (FortRise's base_Update / base_OnConfirm in a patch class): MonoMod makes the method a call of the
        // game's T.M, non-virtual; the C# body (`base.Update()`) would bind the patched class's own method, which FortRise replaces:
        // patch_VersusModeButton.Update calling itself through its hook. The linked method, up T's base types to where it's declared
        if (m.GetAttributes().FirstOrDefault(a => a.AttributeClass?.Name is "MonoModLinkTo" or "MonoModLinkToAttribute") is AttributeData lt &&
            lt.ConstructorArguments.Length >= 2 && lt.ConstructorArguments[0].Value is string ltType && lt.ConstructorArguments[1].Value is string ltName)
        {
            IMethodSymbol ltm = null;
            for (var lty = ws.Compilation.GetTypeByMetadataName(ltType); lty != null && ltm == null; lty = lty.BaseType)
                ltm = lty.GetMembers(ltName).OfType<IMethodSymbol>().FirstOrDefault(x => x.IsStatic == m.IsStatic && x.Parameters.Length == m.Parameters.Length);
            if (ltm != null)
            {
                R.Add("mechanical: [MonoModLinkTo] -> the linked game method, called directly", $"{m.ContainingType.Name}.{m.Name} -> {ltm.ContainingType.Name}.{ltm.Name}");
                var ltc = Call(ltm, m.IsStatic ? null : "self", m.Parameters.Select(p => Id(p.Name)).ToList(), null, nonVirtual: true);
                return m.ReturnsVoid ? $"    {ltc};\n" : $"    return {ltc};\n";
            }
        }
        // [HarmonyReversePatch] (TFGamePatch.TFGame_Update_orig): Harmony copies the target's original body into this stub; here, the
        // original under the target's generated hook (tfexgen_hooks: Orig_<Type>_<Method>), which exists when a hook unit patches it
        if (HookGen.KindOf(m) == "ReversePatch" && new HookGen(ws).TargetOf(m, out _) is IMethodSymbol rpt)
        {
            static string Ident(string x) { var ib = new StringBuilder(); foreach (var ch in x) ib.Append(char.IsLetterOrDigit(ch) ? ch : '_'); return ib.ToString(); }
            includes.Add("tfexgen_hooks.hpp");
            R.Add("mechanical: a Harmony reverse patch -> the original under the generated hook", $"{m.ContainingType.Name}.{m.Name} -> {rpt.ContainingType.Name}.{rpt.Name}");
            var rargs = (rpt.IsStatic ? Enumerable.Empty<string>() : new[] { Id(m.Parameters[0].Name) }).Concat(m.Parameters.Skip(rpt.IsStatic ? 0 : 1).Select(x => Id(x.Name)));
            var call = $"tfex::gen::hooks::Orig_{Ident(rpt.ContainingType.Name)}_{rpt.Name}({string.Join(", ", rargs)})";
            return m.ReturnsVoid ? $"    {call};\n" : $"    return {call};\n";
        }
        // a member of ours with a [service] `cpp` rule (MainMenuPatch.ReadClipboard): its body is the rule too, so a delegate made from it
        // (JoinCodeEntry's pasteProvider) runs the Switch's code as a call does
        if (gen.ServiceRule(m) is TomlTable sbr && sbr.ContainsKey("cpp") && Service(m, m.Parameters.Select(p => Id(p.Name)).ToList(), null) is string sbody)
        {   // (an instance member: $self is its self; virtual calls through the game's vtable reach this body: JoinCodeEntry.OnConfirm)
            R.Add("service map: the member's own body is its rule (delegates to it run the Switch's code)", m.ContainingType.Name + "." + m.Name);
            return m.ReturnsVoid ? $"    {sbody};\n" : $"    return {sbody};\n";
        }
        FindBoxedLocals(syntax);
        if (gen.AfterRule(m) is TomlTable ar)   // overrides.toml [after]: the Switch's code on every way out of TF.EX's body
        {
            if (ar.TryGetValue("include", out var ai)) foreach (var h in ((string)ai).Split(',')) includes.Add(h.Trim());
            R.Add($"service map: the Switch's code after TF.EX's body ({(ar.TryGetValue("key", out var ak) ? ak : "?")})", m.ContainingType.Name + "." + m.Name);
            sb.Append($"    auto after_ = gen::MakeDefer([&]() {{ {(string)ar["cpp"]}; }});\n");
        }
        // a public static entry of a class unit: the class (runtime type, statics: C#'s class constructor) must exist
        if (isClass && m.IsStatic && IsPublicEntry(m))
            sb.Append($"    if (!EnsureClass()) return{(m.IsIterator ? " true" : m.ReturnsVoid ? "" : " " + Literal(null, m.ReturnType))};\n");   // an iterator (visitor form, bool): empty
        if (m.MethodKind == MethodKind.Constructor)
        {
            var co = M(syntax).GetOperation(syntax) as IConstructorBodyOperation;
            // a record's primary constructor (record R(string Tag, ...)): initializers, then each positional property from its parameter
            if (co == null && syntax is TypeDeclarationSyntax { ParameterList: not null } && isClass)
            {
                CtorPrologue(sb, null);
                foreach (var prm in m.Parameters)
                    if (type.GetMembers(prm.Name).OfType<IPropertySymbol>().FirstOrDefault() is IPropertySymbol pp && IsAutoProperty(pp))
                        sb.Append($"    S(self)->{Id(pp.Name)}_ = {Id(prm.Name)};\n");
                R.Add("mechanical: record primary constructor -> positional properties set", Where(syntax));
                return sb.ToString();
            }
            if (co == null) { R.Add("no body", Where(syntax)); methodBlockers.Add("no body"); return "    TFEXGEN_UNSUPPORTED;\n"; }
            CtorPrologue(sb, co.Initializer);
            if (co.BlockBody != null) foreach (var s in co.BlockBody.Operations) Stmt(s, sb, 1);
            else if (co.ExpressionBody != null) foreach (var s in co.ExpressionBody.Operations) Stmt(s, sb, 1);
            return sb.ToString();
        }
        if (syntax is PropertyDeclarationSyntax pds && pds.ExpressionBody != null) syntax = pds.ExpressionBody;   // int X => e;
        var any = M(syntax).GetOperation(syntax);
        if (any is IBlockOperation blk) { foreach (var s in blk.Operations) Stmt(s, sb, 1); if (m.IsIterator && !coMode) sb.Append("    return true;\n"); return sb.ToString(); }
        var op = any as IMethodBodyOperation;
        // an abstract method (LoaderAI's Agent.Move): called only through its vtable slot, where a subclass's override sits; its own
        // function is the slot's filler, never reached (C# can't call it non-virtually)
        if (op == null && m.IsAbstract) { R.Add("mechanical: an abstract method -> an empty slot filler (its overrides run)", $"{m.ContainingType.Name}.{m.Name}"); return m.ReturnsVoid ? "" : "    return {};\n"; }
        if (op == null) { R.Add("no body", Where(syntax)); methodBlockers.Add("no body"); return "    TFEXGEN_UNSUPPORTED;\n"; }
        if (op.BlockBody != null) BlockStmts(op.BlockBody, sb, 1);
        else if (op.ExpressionBody != null) foreach (var s in op.ExpressionBody.Operations) Stmt(s, sb, 1);
        if (m.IsIterator && !coMode) sb.Append("    return true;\n");
        return sb.ToString();
    }

    /* foreach over a TF.EX iterator: the loop body becomes the iterator's callback; break/continue/return inside it map to the
     * callback's result. Stack of those contexts. */
    readonly Stack<string> callbackDepth = new();
    int loopDepthInCallback;

    void Stmt(IOperation op, StringBuilder sb, int ind)
    {
        // inside a try that catches a throwing stand-in: after a simple statement that called one, the jump to the catch on a fault
        bool compound = op is IBlockOperation or ILoopOperation or IConditionalOperation or ISwitchOperation or ITryOperation or IUsingOperation or ILockOperation;
        var saveThrows = stmtThrows; var saveYields = stmtYields;
        stmtThrows = false; stmtYields = false;
        StmtCore(op, sb, ind);
        // S-async: a stand-in with nothing yet (the socket's receive on the game thread): this pass of the loop returns, its pump calls it again
        if (stmtYields && !compound && op is not IReturnOperation and not IBranchOperation)
        {
            R.Add("mechanical: an await of a stand-in with nothing yet -> the pass returns (gen::WouldBlock; its pump calls the loop again next frame)", Where(op));
            sb.Append(new string(' ', ind * 4)).Append("if (gen::WouldBlock()) return;\n");
        }
        if (stmtThrows && !compound && catchLabels.Count > 0 && op is not IReturnOperation and not IBranchOperation)
            sb.Append(new string(' ', ind * 4)).Append($"if (gen::Faulted()) goto {catchLabels[^1]};\n");
        stmtThrows |= saveThrows; stmtYields |= saveYields;
    }
    void StmtCore(IOperation op, StringBuilder sb, int ind)
    {
        var pad = new string(' ', ind * 4);
        // (a deconstruction's statement hoists its out vars / patterns; Stmt then recurses into the deconstruction: not again)
        if (op is not IBlockOperation and not ISwitchOperation && op is not IDeconstructionAssignmentOperation { Parent: IExpressionStatementOperation })
            HoistPatternLocals(op, sb, pad);
        switch (op)
        {
            case ISwitchOperation sw:
                Switch(sw, sb, ind);
                break;
            case IBlockOperation b:
                sb.Append(pad).Append("{\n");
                BlockStmts(b, sb, ind + 1);
                sb.Append(pad).Append("}\n");
                break;
            case IVariableDeclarationGroupOperation g:
                foreach (var d in g.Declarations)
                    foreach (var v in d.Declarators)
                    {
                        var init = v.Initializer?.Value ?? d.Initializer?.Value;
                        var local = v.Symbol;
                        if (local.IsConst) { sb.Append(pad).Append($"constexpr {Cpp(local.Type)} {Id(local.Name)} = {Literal(local.ConstantValue, local.Type)};\n"); continue; }
                        // `ref var x = ref CollectionsMarshal.GetValueRefOrAddDefault(dict, key, out bool exists)` (FortRise): the slot of key
                        // in dict, added with the default when missing; x's reads and writes are dict[key]
                        if (local.RefKind == RefKind.Ref && init != null && Strip(init) is IInvocationOperation { TargetMethod: { Name: "GetValueRefOrAddDefault", ContainingType.Name: "CollectionsMarshal" } } gvi &&
                            Sub(gvi.Arguments.OrderBy(a => a.Parameter.Ordinal).First().Value.Type) is INamedTypeSymbol dictT)
                        {
                            var ga = gvi.Arguments.OrderBy(a => a.Parameter.Ordinal).ToList();
                            var dv = $"rd_{tmp++}"; var kv = $"rk_{tmp++}";
                            var contains = dictT.GetMembers("ContainsKey").OfType<IMethodSymbol>().First();
                            var setItem = dictT.GetMembers("set_Item").OfType<IMethodSymbol>().First();
                            var getItem = dictT.GetMembers("get_Item").OfType<IMethodSymbol>().First();
                            R.Add("mechanical: a ref local over CollectionsMarshal.GetValueRefOrAddDefault -> the dictionary's slot", Where(gvi));
                            sb.Append(pad).Append($"auto {dv} = {Expr(ga[0].Value)}; auto {kv} = {Expr(ga[1].Value)};\n");
                            bool valMap = IsValColl(dictT);   // a value map (the runtime's ValMap): its own members
                            sb.Append(pad).Append($"{LValue(ga[2].Value)} = {(valMap ? $"{dv}->ContainsKey({kv})" : Call(contains, dv, new List<string> { kv }, gvi))};\n");
                            var dflt = Literal(null, dictT.TypeArguments[1]);
                            sb.Append(pad).Append($"if (!{LValue(ga[2].Value)}) {(valMap ? $"{dv}->Set({kv}, {dflt})" : Call(setItem, dv, new List<string> { kv, dflt }, gvi))};\n");
                            refSlots[local] = valMap ? ($"{dv}->Get({kv})", v2 => $"{dv}->Set({kv}, {v2})")
                                : (Call(getItem, dv, new List<string> { kv }, gvi), v2 => Call(setItem, dv, new List<string> { kv, v2 }, gvi));
                            continue;
                        }
                        // `ref var x = ref CollectionsMarshal.GetValueRefOrNullRef(dict, key)`: dict[key] when present, else a null ref
                        // (Unsafe.IsNullRef(ref x) is the missing flag); x's reads and writes are dict[key]
                        if (local.RefKind == RefKind.Ref && init != null && Strip(init) is IInvocationOperation { TargetMethod: { Name: "GetValueRefOrNullRef", ContainingType.Name: "CollectionsMarshal" } } gnr &&
                            Sub(gnr.Arguments.OrderBy(a => a.Parameter.Ordinal).First().Value.Type) is INamedTypeSymbol dictN)
                        {
                            var ga = gnr.Arguments.OrderBy(a => a.Parameter.Ordinal).ToList();
                            var dv = $"rd_{tmp++}"; var kv = $"rk_{tmp++}"; var fv = $"rf_{tmp++}";
                            var contains = dictN.GetMembers("ContainsKey").OfType<IMethodSymbol>().First();
                            var setItem = dictN.GetMembers("set_Item").OfType<IMethodSymbol>().First();
                            var getItem = dictN.GetMembers("get_Item").OfType<IMethodSymbol>().First();
                            R.Add("mechanical: a ref local over CollectionsMarshal.GetValueRefOrNullRef -> the dictionary's slot and a found flag", Where(gnr));
                            sb.Append(pad).Append($"auto {dv} = {Expr(ga[0].Value)}; auto {kv} = {Expr(ga[1].Value)};\n");
                            bool valMapN = IsValColl(dictN);
                            sb.Append(pad).Append($"bool {fv} = {(valMapN ? $"{dv}->ContainsKey({kv})" : Call(contains, dv, new List<string> { kv }, gnr))};\n");
                            refFound[local] = fv;
                            refSlots[local] = valMapN ? ($"({fv} ? {dv}->Get({kv}) : {Literal(null, dictN.TypeArguments[1])})", v2 => $"{dv}->Set({kv}, {v2})")
                                : ($"({fv} ? {Call(getItem, dv, new List<string> { kv }, gnr)} : {Literal(null, dictN.TypeArguments[1])})", v2 => Call(setItem, dv, new List<string> { kv, v2 }, gnr));
                            continue;
                        }
                        if (boxedLocals.Contains(local))
                        {
                            var bt = Cpp(local.Type);
                            R.Add("mechanical: a local lambdas capture and the code changes -> a one-element managed array (shared display class)", $"{current?.Name}: {local.Name}");
                            sb.Append(pad).Append($"auto {Id(local.Name)}_arr = gen::NewArr<{bt}>({BoxArrayType(local)}, 1);   /* shared with its lambdas */\n");
                            sb.Append(pad).Append($"{bt}& {Id(local.Name)} = {Id(local.Name)}_arr.data[0];\n");
                            if (init != null) sb.Append(pad).Append($"{Id(local.Name)} = {Expr(init)};\n");
                            continue;
                        }
                        if (init != null && !IsModelCollection(local.Type) && IsModelList(init)) { modelLocals.Add(local); sb.Append(pad).Append($"auto {Id(local.Name)} = {Expr(init)};   /* a model's list */\n"); continue; }
                        if (init != null && CalledSnap(init, ReturnsSnap) is IMethodSymbol sm) { snapLocals[local] = SnapElem(sm); sb.Append(pad).Append($"gen::Snap<{SnapElem(sm)}> {Id(local.Name)} = {Expr(init)};\n"); continue; }
                        if (init != null && ChainSnapElem(init) is string chel) { snapLocals[local] = chel; sb.Append(pad).Append($"gen::Snap<{chel}> {Id(local.Name)} = {Expr(init)};\n"); continue; }
                        if (init != null && Strip(init) is ICoalesceOperation { WhenNull: var cwn } cco && Strip(cwn) is ICollectionExpressionOperation { Elements.Length: 0 } &&
                            Strip(cco.Value) is IConditionalAccessOperation cca && LinqMaterialized(cca.WhenNotNull) is string celem)
                        {   // x?.Seq.ToList() ?? []: a snapshot, empty when x is null
                            snapLocals[local] = celem;
                            var cv = $"ca_{tmp++}";
                            condAccessStack.Push(cv);
                            var cin = Expr(cca.WhenNotNull);
                            condAccessStack.Pop();
                            sb.Append(pad).Append($"gen::Snap<{celem}> {Id(local.Name)}{{}};\n");
                            sb.Append(pad).Append($"{{ auto {cv} = {Expr(cca.Operation)}; if ({cv}) {Id(local.Name)} = {cin}; }}\n");
                            continue;
                        }
                        if (init != null && LinqMaterialized(init) is string elem) { snapLocals[local] = elem; sb.Append(pad).Append($"gen::Snap<{elem}> {Id(local.Name)} = {Expr(init)};\n"); continue; }
                        // an IEnumerable<T> local made by a lazy LINQ chain (var known = All.Select(...)): materialized where it's declared,
                        // a snapshot (the same elements when the source doesn't change before it's read, as in TF.EX's uses)
                        if (init != null && local.Type is INamedTypeSymbol { OriginalDefinition.SpecialType: SpecialType.System_Collections_Generic_IEnumerable_T } lit &&
                            Strip(init) is IInvocationOperation lin && IsEnumerable(lin.TargetMethod) && !IsValColl(local.Type))
                        {
                            var le = Cpp(lit.TypeArguments[0]);
                            snapLocals[local] = le;
                            R.Add("mechanical: an IEnumerable<T> local from a lazy LINQ chain -> a snapshot where it's declared", $"{current?.Name}: {local.Name}");
                            sb.Append(pad).Append($"gen::Snap<{le}> {Id(local.Name)} = gen::ToSnap<{le}>({SeqOf(init)});\n");
                            continue;
                        }
                        if (init != null && Strip(init) is IInvocationOperation { TargetMethod: { Name: "Create", ContainingType: var tct } } tcr && tct.ToDisplayString() == "HarmonyLib.Traverse" && tcr.Arguments.Length == 1)
                        { dynLocals[local] = Strip(tcr.Arguments[0].Value).Type; sb.Append(pad).Append($"void* {Id(local.Name)} = {Expr(Strip(tcr.Arguments[0].Value))};   /* Traverse.Create */\n"); continue; }
                        if (init != null && ReflectedField(init) is IFieldSymbol rfs)
                        { reflFieldLocals[local] = rfs; sb.Append(pad).Append($"/* {local.Name} = typeof({rfs.ContainingType.Name}).GetField(\"{rfs.Name}\"): read as the field */\n"); continue; }
                        if (init != null && IsDynamicDataFor(init, out var target)) { dynLocals[local] = Strip(init) is IObjectCreationOperation { Type: INamedTypeSymbol { TypeArguments.Length: 1 } dt1 } ? dt1.TypeArguments[0] : target.Type; sb.Append(pad).Append($"void* {Id(local.Name)} = {Expr(target)};   /* DynamicData.For */\n"); continue; }
                        // DynamicData d = null; ... d = DynamicData.For(x) (inside a try, LoaderAI's Keys_postfix): every assignment of it a
                        // DynamicData.For over one type: tracked as the initialized form, its assignments the wrapped object (Assign)
                        if ((init == null || Strip(init) is ILiteralOperation { ConstantValue.Value: null }) && IsDynamicData(local.Type) && DynAssignedType(local) is ITypeSymbol dat)
                        { dynLocals[local] = dat; sb.Append(pad).Append($"void* {Id(local.Name)} = nullptr;   /* DynamicData, assigned below */\n"); continue; }
                        if (init != null && local.Type is INamedTypeSymbol { Name: "IEnumerable", TypeArguments.Length: 1 } && Strip(init) is IInvocationOperation sq && IsEnumerable(sq.TargetMethod))
                        {   // C#'s IEnumerable local over a lazy chain: the C++ sequence (re-run per use, as C# re-enumerates)
                            seqLocals.Add(local);
                            R.Add("mechanical: an IEnumerable local of a lazy LINQ chain -> the sequence", $"{current?.Name}: {local.Name}");
                            sb.Append(pad).Append($"auto {Id(local.Name)} = {Expr(init)};\n");
                            continue;
                        }
                        sb.Append(pad).Append($"{Cpp(local.Type)} {Id(local.Name)}" + (init != null ? " = " + Expr(init) : "{}") + ";\n");
                    }
                break;
            case IExpressionStatementOperation { Operation: IDeconstructionAssignmentOperation dex }:
                Stmt(dex, sb, ind);
                break;
            case IExpressionStatementOperation e:
                sb.Append(pad).Append(Expr(e.Operation)).Append(";\n");
                break;
            case IReturnOperation r when r.Kind == OperationKind.YieldReturn && coMode:
            {   // Coroutine.Update: an int waits that many frames, anything else (null, a float) continues next frame
                var yv = Strip(r.ReturnedValue);
                while (yv is IConversionOperation yc) yv = yc.Operand;
                sb.Append(pad).Append(yv.Type?.SpecialType == SpecialType.System_Int32 ? $"co_yield (int32_t)({Expr(yv)});\n" : "co_yield 0;\n");
                break;
            }
            case IReturnOperation r when r.Kind == OperationKind.YieldBreak && coMode:
                sb.Append(pad).Append("co_return;\n");
                break;
            case IReturnOperation r when r.Kind == OperationKind.YieldReturn:
                sb.Append(pad).Append($"if (!yield_({YieldArgs(r.ReturnedValue)})) return false;\n");
                break;
            case IReturnOperation r when r.Kind == OperationKind.YieldBreak:
                sb.Append(pad).Append("return true;\n");
                break;
            case IReturnOperation r:
                // `return;` in a void method's foreach over a sequence (its visitor): leave the visit, then the method (the flag below)
                if (callbackDepth.Count > 0 && r.ReturnedValue == null && callbackDepth.Peek().StartsWith("seq:"))
                { sb.Append(pad).Append($"{{ {callbackDepth.Peek()[4..].Split('|')[0]} = true; return false; }}\n"); break; }
                if (callbackDepth.Count == 1 && r.ReturnedValue != null && callbackDepth.Peek().StartsWith("seq:") && callbackDepth.Peek().Contains('|'))
                {   // `return v;` inside a foreach over a sequence (TreasureChestPatch.FindPreview): the value kept, the visit stopped, returned after it
                    var fl = callbackDepth.Peek()[4..].Split('|');
                    R.Add("mechanical: return of a value inside a foreach over a sequence -> kept, the visit stopped, returned after it", Where(op));
                    sb.Append(pad).Append($"{{ {fl[1]} = {Expr(r.ReturnedValue)}; {fl[0]} = true; return false; }}\n"); break;
                }
                if (callbackDepth.Count > 0) { R.Add("unsupported: return inside a foreach over an iterator", Where(op)); sb.Append(pad).Append("TFEXGEN_UNSUPPORTED;\n"); break; }
                if (r.ReturnedValue != null && ReturnsSnap(current) && !InLambda(r))
                {
                    R.Add("rule: IEnumerable<T> returned by TF.EX code -> a snapshot (gen::Snap)", Where(op));
                    var rv = Strip(r.ReturnedValue);   // .ToList()/.ToArray() returned as IEnumerable: the snapshot is the materialization
                    if (rv is IInvocationOperation ti && IsEnumerable(ti.TargetMethod) && ti.TargetMethod.Name is "ToList" or "ToArray")
                        rv = ti.Arguments.OrderBy(a => a.Parameter.Ordinal).First().Value;
                    sb.Append(pad).Append($"return gen::ToSnap<{SnapElem(current)}>({SeqOf(rv)});\n");
                    break;
                }
                sb.Append(pad).Append(r.ReturnedValue == null ? "return;\n" : $"return {Expr(r.ReturnedValue)};\n");
                break;
            case IConditionalOperation c when subst.Count > 0 && Strip(c.Condition) is IBinaryOperation { OperatorKind: BinaryOperatorKind.Equals or BinaryOperatorKind.NotEquals } tc &&
                                              Strip(tc.LeftOperand) is ITypeOfOperation && Strip(tc.RightOperand) is ITypeOfOperation:
            {   // a generic copy's `if (typeof(T) == typeof(int))`: only the branch of this instantiation (the other wouldn't compile)
                var live = Expr(c.Condition) == "true" ? c.WhenTrue : c.WhenFalse;
                if (live != null) StmtBlock(live, sb, ind);
                break;
            }
            case IConditionalOperation c:
                sb.Append(pad).Append($"if ({Expr(c.Condition)})\n");
                StmtBlock(c.WhenTrue, sb, ind);
                if (c.WhenFalse != null)
                {
                    if (c.WhenFalse is IConditionalOperation) { sb.Append(pad).Append("else\n"); StmtBlock(c.WhenFalse, sb, ind); }
                    else { sb.Append(pad).Append("else\n"); StmtBlock(c.WhenFalse, sb, ind); }
                }
                break;
            case IForLoopOperation f:
            {
                var before = string.Join(", ", f.Before.SelectMany(LoopInit));
                var after = string.Join(", ", f.AtLoopBottom.Select(a => Expr(a is IExpressionStatementOperation es ? es.Operation : a)));
                sb.Append(pad).Append($"for ({before}; {(f.Condition != null ? Expr(f.Condition) : "")}; {after})\n");
                loopDepthInCallback++;
                StmtBlock(f.Body, sb, ind);
                loopDepthInCallback--;
                break;
            }
            case IWhileLoopOperation w:
                if (w.ConditionIsTop) sb.Append(pad).Append($"while ({(w.Condition != null ? Expr(w.Condition) : "true")})\n");
                else sb.Append(pad).Append("do\n");
                loopDepthInCallback++;
                StmtBlock(w.Body, sb, ind);
                loopDepthInCallback--;
                if (!w.ConditionIsTop) sb.Append(pad).Append($"while ({Expr(w.Condition)});\n");
                break;
            // foreach (var n in new[] { "a", "b" }) whose body uses n as a member name (TF.State's RestoreEnemyTags: dynScene.Get(pending) over
            // "tagsToAdd", "tagsToRemove"): unrolled, each pass with n a known constant (a do-while(false) per pass: `continue` ends the pass)
            case IForEachLoopOperation fe when Strip(fe.Collection) is IArrayCreationOperation { Initializer: { } fai } &&
                                                fai.ElementValues.Length is > 0 and <= 8 && fai.ElementValues.All(e => e.ConstantValue is { HasValue: true, Value: string }) &&
                                                fe.LoopControlVariable is IVariableDeclaratorOperation { Symbol: ILocalSymbol fel } &&
                                                !fe.Body.Descendants().OfType<IBranchOperation>().Any(b => b.BranchKind == BranchKind.Break && SymbolEqualityComparer.Default.Equals(b.Target, fe.ExitLabel)):
            {
                R.Add("mechanical: foreach over a constant string array -> unrolled, the element a known constant (DynamicData member names)", Where(fe));
                foreach (var e in fai.ElementValues)
                {
                    constLocals[fel] = (string)e.ConstantValue.Value;
                    sb.Append(pad).Append("do {\n");
                    sb.Append(pad).Append($"    void* {Id(fel.Name)} = {Expr(e)}; (void){Id(fel.Name)};\n");
                    Stmt(fe.Body, sb, ind + 1);
                    sb.Append(pad).Append("} while (false);\n");
                }
                constLocals.Remove(fel);
                break;
            }
            case IForEachLoopOperation fe:
                ForEach(fe, sb, ind);
                break;
            case IBranchOperation sbr when sbr.BranchKind == BranchKind.Break && sbr.Target != null && switchExits.TryGetValue(sbr.Target, out var swEnd):
                sb.Append(pad).Append($"goto {swEnd};   /* break (the switch) */\n");   // a break nested in a switch section: past the switch
                break;
            case IBranchOperation gt when gt.BranchKind == BranchKind.GoTo && gt.Target != null:
                R.Add("mechanical: goto -> goto", Where(op));
                sb.Append(pad).Append($"goto L_{Id(gt.Target.Name)};\n");
                break;
            case ILabeledOperation lo:
                sb.Append(pad).Append($"L_{Id(lo.Label.Name)}:;\n");
                if (lo.Operation != null) Stmt(lo.Operation, sb, ind);
                break;
            case IBranchOperation br when br.BranchKind is BranchKind.Break or BranchKind.Continue:
                if (callbackDepth.Count > 0 && loopDepthInCallback == 0)
                    sb.Append(pad).Append(br.BranchKind == BranchKind.Break ? "return false;\n" : "return true;\n");
                else sb.Append(pad).Append(br.BranchKind == BranchKind.Break ? "break;\n" : "continue;\n");
                break;
            case IEmptyOperation:
            case ILocalFunctionOperation:   // placed by BlockStmts
                break;
            case IDeconstructionAssignmentOperation da when da.Target is IDeclarationExpressionOperation { Expression: ITupleOperation tt } || da.Target is ITupleOperation:
            {
                var tt2 = da.Target is IDeclarationExpressionOperation de2 ? (ITupleOperation)de2.Expression : (ITupleOperation)da.Target;
                var tv = $"tup_{tmp++}";
                sb.Append(pad).Append($"auto {tv} = {Expr(da.Value)};\n");
                for (int i = 0; i < tt2.Elements.Length; i++)
                {
                    var el = tt2.Elements[i];
                    if (el is IDiscardOperation) continue;
                    if (el is IDeclarationExpressionOperation { Expression: ILocalReferenceOperation dl1 }) sb.Append(pad).Append($"{Cpp(dl1.Local.Type)} {Id(dl1.Local.Name)} = {tv}.Item{i + 1};\n");
                    else if (da.Target is IDeclarationExpressionOperation && el is ILocalReferenceOperation dl2) sb.Append(pad).Append($"{Cpp(dl2.Local.Type)} {Id(dl2.Local.Name)} = {tv}.Item{i + 1};\n");
                    else sb.Append(pad).Append($"{LValue(el)} = {tv}.Item{i + 1};\n");
                }
                break;
            }
            case ITryOperation tr when tr.Catches.Length > 0 && tr.Finally == null && ThrowsInside(tr.Body):
            {   // a try around a throwing stand-in (the socket: ClientWebSocket's connect / receive / send): the stand-in records a fault for
                // its thread (gen::Fault) where .NET throws; each statement that called one is followed by a jump to the catch, which clears
                // the fault and runs (its exception variable: null; TF.EX only logs it). Its first clause stands for all (they catch Exception).
                R.Add("mechanical: try/catch around a throwing stand-in -> the body with a fault check after each call, the catch on a fault (gen::Faulted)", Where(op));
                var lbl = $"catch_{catchN++}";
                sb.Append(pad).Append("{\n").Append(pad).Append("    gen::ClearFault();\n");
                catchLabels.Add(lbl);
                if (tr.Body is IBlockOperation tb) BlockStmts(tb, sb, ind + 1); else Stmt(tr.Body, sb, ind + 1);
                catchLabels.RemoveAt(catchLabels.Count - 1);
                sb.Append(pad).Append("}\n");
                sb.Append(pad).Append("if (false) {\n").Append(pad).Append($"{lbl}:\n").Append(pad).Append("    gen::ClearFault();\n");
                var cc = tr.Catches[0];
                if (cc.Locals.FirstOrDefault() is ILocalSymbol ce) sb.Append(pad).Append($"    void* {Id(ce.Name)} = nullptr; (void){Id(ce.Name)};   /* the exception: none on the Switch */\n");
                if (cc.Handler is IBlockOperation hb) BlockStmts(hb, sb, ind + 1); else Stmt(cc.Handler, sb, ind + 1);
                sb.Append(pad).Append("}\n");
                break;
            }
            case ITryOperation tr:
                // exlaunch builds without exceptions and the game's own are fatal: a catch never runs. The try body runs, then finally.
                R.Add("rule: try/catch -> the body (no exceptions on the Switch; catch never runs)", Where(op));
                if (tr.Finally == null) { Stmt(tr.Body, sb, ind); break; }
                {   // finally: a scope guard (gen::MakeDefer), so it also runs when the body returns (NetplayManager.NativeInit's
                    // `try { return netplay_init(..); } finally { IsInInit = false; }`: emitted after the return, it never ran)
                    R.Add("mechanical: finally -> a scope guard (gen::MakeDefer) around the try body", Where(op));
                    var fsb = new StringBuilder();
                    Stmt(tr.Finally, fsb, ind + 2);
                    var pad1 = pad + "    ";
                    sb.Append(pad).Append("{\n");
                    sb.Append(pad1).Append($"auto finally_{finallyN++} = gen::MakeDefer([&]() {{\n").Append(fsb).Append(pad1).Append("});\n");
                    Stmt(tr.Body, sb, ind + 1);
                    sb.Append(pad).Append("}\n");
                }
                break;
            case IUsingDeclarationOperation ud:
                // `using var x = e;`: x declared, its Dispose() at the end of the enclosing scope (C#'s order), through a scope guard
                Stmt(ud.DeclarationGroup, sb, ind);
                UsingDisposes(ud.DeclarationGroup, op, sb, pad);
                break;
            case IUsingOperation us when us.Resources is IVariableDeclarationGroupOperation ug:
                // `using (var x = e) { ... }`: a block with x and its Dispose() guard, then the body
                sb.Append(pad).Append("{\n");
                Stmt(ug, sb, ind + 1);
                UsingDisposes(ug, op, sb, pad + "    ");
                if (us.Body is IBlockOperation ub) foreach (var s in ub.Operations) Stmt(s, sb, ind + 1); else Stmt(us.Body, sb, ind + 1);
                sb.Append(pad).Append("}\n");
                break;
            case IThrowOperation th:
                R.Add("rule: throw -> gen::Throw (fatal, as the game's own exceptions)", Where(op));
                sb.Append(pad).Append($"gen::Throw(\"{name}: {current?.Name}\");\n");
                break;

            default:
                R.Add("unsupported statement: " + op.Kind, Where(op));
                methodBlockers.Add("statement " + op.Kind);
                sb.Append(pad).Append($"TFEXGEN_UNSUPPORTED;   /* {op.Kind}: {op.Syntax.ToString().Split('\n')[0]} */\n");
                break;
        }
    }

    /* using: each declared local's Dispose() at the end of the scope (gen::MakeDefer): a [service] rule, its unit's, or the game's */
    void UsingDisposes(IVariableDeclarationGroupOperation g, IOperation op, StringBuilder sb, string pad)
    {
        foreach (var d in g.Declarations)
            foreach (var v in d.Declarators)
            {
                var lt = v.Symbol.Type as INamedTypeSymbol;
                var dm = lt?.GetMembers("Dispose").OfType<IMethodSymbol>().FirstOrDefault(x => x.Parameters.Length == 0 && !x.IsStatic);
                var x = Id(v.Symbol.Name);
                string call = null;
                // a [service] rule keyed on this type's Dispose, inherited or not (SafeBytes<T> : SafeHandle): the rule, $self = the local
                var typeRule = lt == null ? default : gen.Services.FirstOrDefault(sv => sv.pattern == lt.ToDisplayString().TrimEnd('?') + ".Dispose");   // (`Stream?` in a nullable context: the same type)
                if (typeRule.rule is TomlTable tr && tr.TryGetValue("cpp", out var tc) && tc is string tcs)
                {
                    gen.Rules.Use("service", typeRule.pattern);
                    if (tr.TryGetValue("include", out var ti) && ti is string tis) includes.Add(tis);
                    R.Add("mechanical: using -> its Dispose at the end of the scope (gen::MakeDefer)", Where(op));
                    sb.Append(pad).Append($"auto dispose_{x} = gen::MakeDefer([&]() {{ {tcs.Replace("$self", x)}; }});\n");
                    continue;
                }
                if (IsDynamicData(lt)) continue;   /* DynData<T>'s Dispose only drops its own member cache: nothing to do */
                if (dm == null) { call = null; }
                else if (gen.AbsentRule(dm) != null) continue;
                else if (gen.ServiceRule(dm) != null) call = Service(dm, i => x, op, null);
                else if (SymbolEqualityComparer.Default.Equals(dm.ContainingType, type)) { methodCallees.Add(dm.OriginalDefinition); call = $"{MName(dm)}({x})"; }
                else if (OriginOf(dm.ContainingType) == Origin.Tfex && (OtherUnit(dm.ContainingType) is TomlTable du && !du.ContainsKey("auto") && !du.ContainsKey("members") ? du : AutoU(dm.ContainingType, dm)) is TomlTable du2)
                { includes.Add($"tfexgen_{du2["name"]}.hpp"); call = $"tfex::gen::{du2["name"]}::{MName(dm)}({x})"; }
                else if (InBinary(dm.ContainingType)) call = Call(dm, x, new List<string>(), op);
                if (call == null && lt?.ToDisplayString() is "System.IO.BinaryReader" or "System.IO.BinaryWriter" or "System.IO.MemoryStream")
                {   // over memory: Dispose only marks it closed (no file, nothing else held), and the game's AOT build has none to call
                    R.Add("mechanical: using over a memory reader / writer -> nothing to dispose", Where(op));
                    continue;
                }
                if (call == null) { sb.Append(pad).Append(Unsupported(op, "using var of " + lt?.ToDisplayString() + " (no Dispose to call)") + ";\n"); continue; }
                R.Add("mechanical: using -> its Dispose at the end of the scope (gen::MakeDefer)", Where(op));
                sb.Append(pad).Append($"auto dispose_{x} = gen::MakeDefer([&]() {{ {call}; }});\n");
            }
    }

    static bool InLambda(IOperation op)
    {
        for (var p = op.Parent; p != null; p = p.Parent)
            if (p is IAnonymousFunctionOperation or ILocalFunctionOperation) return true;
        return false;
    }

    void StmtBlock(IOperation op, StringBuilder sb, int ind)
    {
        if (op is IBlockOperation) Stmt(op, sb, ind);
        else { sb.Append(new string(' ', ind * 4)).Append("{\n"); Stmt(op, sb, ind + 1); sb.Append(new string(' ', ind * 4)).Append("}\n"); }
    }

    /* a block's statements; its C# local functions become C++ lambdas just before the first statement that uses them (C# lets a call
     * come before the declaration; C++ needs the lambda first, and after the locals it captures: FortRise's SpriteDataLoader declares
     * Register<T> at the end of a block, after the locals it uses and the calls) */
    void BlockStmts(IBlockOperation b, StringBuilder sb, int ind)
    {
        var pending = b.Operations.OfType<ILocalFunctionOperation>().ToList();
        bool Uses(IOperation s, ILocalFunctionOperation lf) => s.DescendantsAndSelf().Any(d =>
            (d is IInvocationOperation i && SymbolEqualityComparer.Default.Equals(i.TargetMethod.OriginalDefinition, lf.Symbol.OriginalDefinition)) ||
            (d is IMethodReferenceOperation mr && SymbolEqualityComparer.Default.Equals(mr.Method.OriginalDefinition, lf.Symbol.OriginalDefinition)));
        void Place(ILocalFunctionOperation lf)
        {
            if (!pending.Remove(lf)) return;
            foreach (var other in pending.ToList()) if (lf.Body != null && Uses(lf.Body, other)) Place(other);   // the ones it calls first
            LocalFunction(lf, sb, ind);
        }
        // a label a goto earlier in this block jumps forward to (FortRise's UIModMenu.InitMods: goto CAN_HAVE): the statements between
        // the goto's and the label go in a scope of their own, as C++ won't jump past a local's initialization into its scope
        var ops = b.Operations;
        var openAt = new Dictionary<int, int>();   // first statement of a scope -> the label's statement
        for (int k = 0; k < ops.Length; k++)
        {
            if (ops[k] is not ILabeledOperation lab) continue;
            int from = -1;
            for (int g = 0; g < k && from < 0; g++)
                if (ops[g].DescendantsAndSelf().Any(d => d is IBranchOperation { BranchKind: BranchKind.GoTo } br && SymbolEqualityComparer.Default.Equals(br.Target, lab.Label)))
                    from = g;
            if (from >= 0 && from + 1 < k) openAt[from + 1] = k;
        }
        int closeAt = -1;
        for (int k = 0; k < ops.Length; k++)
        {
            var s = ops[k];
            if (k == closeAt) { ind--; sb.Append(new string(' ', ind * 4)).Append("}\n"); closeAt = -1; }
            if (closeAt < 0 && openAt.TryGetValue(k, out var lk)) { sb.Append(new string(' ', ind * 4)).Append("{   /* jumped over by a goto */\n"); ind++; closeAt = lk; }
            foreach (var lf in pending.ToList()) if (s == lf || Uses(s, lf)) Place(lf);
            Stmt(s, sb, ind);
        }
    }

    /* the body calls a stand-in that can fail ([service] rule throws = true), outside any lambda of its own */
    bool ThrowsInside(IOperation body) => body.Descendants().Any(d =>
        (d is IInvocationOperation di && gen.ServiceRule(di.TargetMethod) is TomlTable r1 && r1.TryGetValue("throws", out var t1) && t1 is true) ||
        (d is IPropertyReferenceOperation dp && gen.ServiceRule(dp.Property) is TomlTable r2 && r2.TryGetValue("throws", out var t2) && t2 is true) ||
        (d is IObjectCreationOperation dc && dc.Constructor != null && gen.ServiceRule(dc.Constructor) is TomlTable r3 && r3.TryGetValue("throws", out var t3) && t3 is true));
    void LocalFunction(ILocalFunctionOperation lf, StringBuilder sb, int ind)
    {
        var pad = new string(' ', ind * 4);
        if (lf.Symbol.IsGenericMethod)
        {
            // a generic local function (FortRise's SpriteDataLoader Register<T>): a copy per instantiation the method calls, its type
            // parameters substituted (typeof(T) tests fold, (T)(object)x casts vanish)
            var scope = (IOperation)lf.Parent ?? lf;
            var insts = scope.Descendants().OfType<IInvocationOperation>()
                .Where(i => SymbolEqualityComparer.Default.Equals(i.TargetMethod.OriginalDefinition, lf.Symbol.OriginalDefinition))
                .Select(i => i.TargetMethod.TypeArguments.Select(Sub).ToArray())
                .GroupBy(a => string.Join(",", a.Select(x => x.ToDisplayString()))).Select(g => g.First()).ToList();
            foreach (var args in insts)
            {
                for (int k = 0; k < args.Length; k++) subst[lf.Symbol.TypeParameters[k]] = args[k];
                try
                {
                    R.Add("mechanical: a generic local function -> a copy per instantiation", Where(lf) + " <" + string.Join(", ", args.Select(a => a.ToDisplayString())) + ">");
                    var gps = string.Join(", ", lf.Symbol.Parameters.Select(p => $"{Cpp(p.Type)} {Id(p.Name)}"));
                    var ginner = new StringBuilder();
                    if (lf.Body != null) BlockStmts(lf.Body, ginner, ind + 1);
                    sb.Append($"{pad}auto {LocalInstName(lf.Symbol, args)} = [&]({gps}) -> {Cpp(lf.Symbol.ReturnType)} {{\n{ginner}{pad}}};\n");
                }
                finally { foreach (var tp in lf.Symbol.TypeParameters) subst.Remove(tp); }
            }
            return;
        }
        var ps = string.Join(", ", lf.Symbol.Parameters.Select(p => $"{Cpp(p.Type)} {Id(p.Name)}"));
        var inner = new StringBuilder();
        if (lf.Body != null) BlockStmts(lf.Body, inner, ind + 1);
        sb.Append($"{pad}auto {Id(lf.Symbol.Name)} = [&]({ps}) -> {Cpp(lf.Symbol.ReturnType)} {{\n{inner}{pad}}};\n");
    }
    static string LocalInstName(IMethodSymbol lf, IEnumerable<ITypeSymbol> args) =>
        lf.Name + "__" + string.Join("_", args.Select(a => System.Text.RegularExpressions.Regex.Replace(a.ToDisplayString(), "[^A-Za-z0-9]", "_")));

    IEnumerable<string> LoopInit(IOperation op)
    {
        if (op is IVariableDeclarationGroupOperation g)
        {
            foreach (var d in g.Declarations)
                foreach (var v in d.Declarators)
                    yield return $"{Cpp(v.Symbol.Type)} {Id(v.Symbol.Name)} = {Expr(v.Initializer?.Value ?? d.Initializer?.Value)}";
        }
        else yield return Expr(op is IExpressionStatementOperation e ? e.Operation : op);
    }

    string YieldArgs(IOperation v)
    {
        while (v is IConversionOperation conv && conv.Operand is ITupleOperation) v = conv.Operand;
        return v is ITupleOperation t ? string.Join(", ", t.Elements.Select(Expr)) : Expr(v);
    }

    static bool IsA(INamedTypeSymbol t, string name)
    {
        for (var c = t; c != null; c = c.BaseType) if (c.ToDisplayString() == name) return true;
        return false;
    }

    IOperation forEachCollection;   /* (a foreach over `x ?? Enumerable.Empty<T>()`: x, inside its null test) */
    /* the types of a Type[] field or local whose initializer lists typeof(T)s only ([typeof(A), typeof(B)] or new[] { ... }) */
    List<INamedTypeSymbol> TypeList(IOperation coll)
    {
        var init = Strip(coll) switch
        {
            IFieldReferenceOperation fr => FieldInit(fr.Field),
            _ => null,
        };
        if (init == null || ArgElements(init) is not List<IOperation> els || els.Count == 0) return null;
        var ts = els.Select(e => Strip(e) is ITypeOfOperation { TypeOperand: INamedTypeSymbol t } ? t : null).ToList();
        return ts.All(t => t != null) ? ts : null;
    }
    void ForEach(IForEachLoopOperation fe, StringBuilder sb, int ind)
    {
        var pad = new string(' ', ind * 4);
        var coll = forEachCollection ?? fe.Collection;
        forEachCollection = null;
        while (coll is IConversionOperation c && c.IsImplicit) coll = c.Operand;
        // foreach (var x in list ?? Enumerable.Empty<T>()) (TF.EX's IsValidBundle): over list when it isn't null
        if (coll is ICoalesceOperation fco && Strip(fco.WhenNull) is IInvocationOperation { TargetMethod: { Name: "Empty", ContainingType.Name: "Enumerable" } })
        {
            var inner = fco.Value;
            while (inner is IConversionOperation ic && ic.IsImplicit) inner = ic.Operand;
            R.Add("mechanical: foreach over x ?? Enumerable.Empty<T>() -> over x when it isn't null", Where(fe));
            sb.Append(pad).Append($"if (({Expr(inner)}) != nullptr) {{\n");
            forEachCollection = inner;
            ForEach(fe, sb, ind + 1);
            sb.Append(pad).Append("}\n");
            return;
        }
        // foreach (char c in text): a plain loop over the string's UTF-16 units (break / continue as written)
        if (fe.Locals.Length == 1 && coll.Type?.SpecialType == SpecialType.System_String)
        {
            var strC = fe.Locals[0]; var strV = $"s_{Id(strC.Name)}"; var strI = $"i_{Id(strC.Name)}";
            R.Add("mechanical: foreach over a string -> a loop over its characters", Where(fe));
            sb.Append(pad).Append($"{{ void* {strV} = {Expr(coll)}; int32_t n{strV} = gen::Len({strV});\n");
            sb.Append(pad).Append($"for (int32_t {strI} = 0; {strI} < n{strV}; {strI}++) {{\n").Append(pad).Append($"    char16_t {Id(strC.Name)} = gen::CharAt({strV}, {strI});\n");
            if (fe.Body is IBlockOperation strB) foreach (var st in strB.Operations) Stmt(st, sb, ind + 1); else Stmt(fe.Body, sb, ind + 1);
            sb.Append(pad).Append("}}\n");
            return;
        }
        // foreach (var t in Hookables) t.GetMethod("Load").Invoke(null, [harmony]) over a field initialized with typeof(A), typeof(B), ...
        // (FortRise's mod template: its IHookable / IRegisterable lists): unrolled at translate time, the body once per type with the loop
        // variable a known type, so its reflection is a direct call (ReflectedMethod / KnownType)
        if (fe.Locals.Length == 1 && TypeList(coll) is List<INamedTypeSymbol> tl)
        {
            R.Add("mechanical: foreach over a list of typeof(T) -> unrolled per type (its reflection resolved at translate time)", Where(fe));
            var tv = fe.Locals[0];
            foreach (var t in tl)
            {
                typeLocals[tv] = t;
                sb.Append(pad).Append($"{{   /* {tv.Name} = typeof({t.Name}) */\n");
                if (fe.Body is IBlockOperation tb) foreach (var st in tb.Operations) Stmt(st, sb, ind + 1); else Stmt(fe.Body, sb, ind + 1);
                sb.Append(pad).Append("}\n");
            }
            typeLocals.Remove(tv);
            return;
        }
        if (AbsentMember(coll) != null || (coll is IPropertyReferenceOperation { Instance: not null } ap0 && AbsentMember(ap0.Instance) != null))
        { sb.Append(pad).Append($"/* foreach over {coll.Syntax}: a FortRise collection, empty on the Switch */\n"); return; }
        // loop variables: one local, or a deconstruction (var (x, y))
        var vars = new List<ILocalSymbol>();
        if (fe.LoopControlVariable is IVariableDeclaratorOperation vd) vars.Add(vd.Symbol);
        else if (fe.LoopControlVariable is IDeclarationExpressionOperation de && de.Expression is ITupleOperation tup)
            vars.AddRange(tup.Elements.OfType<ILocalReferenceOperation>().Select(l => l.Local));
        else if (fe.LoopControlVariable is ITupleOperation tup2)   // foreach ((var a, var b) in ...)
            vars.AddRange(tup2.Elements.Select(e => e is IDeclarationExpressionOperation { Expression: ILocalReferenceOperation l } ? l.Local : (e as ILocalReferenceOperation)?.Local).Where(l => l != null));
        else foreach (var l in fe.Locals) vars.Add(l);
        // a TF.EX iterator method: inline as a callback
        if (coll is IInvocationOperation inv && inv.TargetMethod.IsIterator && OriginOf(inv.TargetMethod.ContainingType) == Origin.Tfex)
        {
            var args = string.Join(", ", inv.Arguments.OrderBy(a => a.Parameter.Ordinal).Select(a => Expr(a.Value)));
            var ps = string.Join(", ", vars.Select(v => $"{Cpp(v.Type)} {Id(v.Name)}"));
            sb.Append(pad).Append($"{Id(inv.TargetMethod.Name)}({args}{(args.Length > 0 ? ", " : "")}[&]({ps}) -> bool {{\n");
            callbackDepth.Push("iter");
            var saved = loopDepthInCallback; loopDepthInCallback = 0;
            if (fe.Body is IBlockOperation b) foreach (var s in b.Operations) Stmt(s, sb, ind + 1); else Stmt(fe.Body, sb, ind + 1);
            loopDepthInCallback = saved;
            callbackDepth.Pop();
            sb.Append(pad).Append("    return true;\n").Append(pad).Append("});\n");
            return;
        }
        // a LINQ snapshot (a local holding .ToList()/.ToArray(), or the call itself)
        bool stateSeqProp = Strip(coll) is IPropertyReferenceOperation { Property: var ssp } && OriginOf(ssp.ContainingType) == Origin.Tfex &&
            ssp.ContainingType.DeclaringSyntaxReferences.FirstOrDefault()?.SyntaxTree.FilePath.StartsWith("state/") == true;   // (a List: SeqOf)
        // a [service] rule marked `snap = true` returns a gen::Snap of the IEnumerable's elements (overrides.toml: IGameContext.GetPlayers)
        bool serviceSnap = Strip(coll) is IInvocationOperation sinv && gen.ServiceRule(sinv.TargetMethod) is TomlTable srule && srule.TryGetValue("snap", out var sv) && sv is true;
        if (!stateSeqProp && (serviceSnap || (coll is ILocalReferenceOperation sl && snapLocals.ContainsKey(sl.Local)) || LinqMaterialized(coll) != null || CalledSnap(coll, ReturnsSnap) != null))
        {
            var a = $"snap_{tmp++}";
            sb.Append(pad).Append($"{{ auto {a} = {Expr(coll)}; for (int32_t i_{a} = 0; i_{a} < {a}.count; i_{a}++) {{\n");
            if (vars.Count == 1) sb.Append(pad).Append($"    {Cpp(vars[0].Type)} {Id(vars[0].Name)} = {a}.items[i_{a}];\n");
            else for (int k = 0; k < vars.Count; k++)   // a deconstruction: the tuple's ItemN
                sb.Append(pad).Append($"    {Cpp(vars[k].Type)} {Id(vars[k].Name)} = {a}.items[i_{a}].Item{k + 1};\n");
            loopDepthInCallback++;
            StmtBlock(fe.Body, sb, ind + 1);
            loopDepthInCallback--;
            sb.Append(pad).Append("} }\n");
            return;
        }
        // a lobby model's collection (a pointer to the struct's list)
        if (IsModelList(coll))
        {
            var a = $"ml_{tmp++}";
            sb.Append(pad).Append($"{{ auto* {a} = {Expr(coll)}; for (int32_t i_{a} = 0; {a} && i_{a} < {a}->Count(); i_{a}++) {{\n");
            sb.Append(pad).Append($"    auto {Id(vars[0].Name)} = gen::ModelElem({a}->items[i_{a}]);\n");
            loopDepthInCallback++;
            StmtBlock(fe.Body, sb, ind + 1);
            loopDepthInCallback--;
            sb.Append(pad).Append("} }\n");
            return;
        }
        // an array of ours
        if (coll.Type is IArrayTypeSymbol)
        {
            var a = $"arr_{tmp++}";
            sb.Append(pad).Append($"{{ auto {a} = {Expr(coll)}; for (int32_t i_{a} = 0; i_{a} < {a}.len; i_{a}++) {{\n");
            sb.Append(pad).Append($"    {Cpp(vars[0].Type)} {Id(vars[0].Name)} = {a}[i_{a}];\n");
            loopDepthInCallback++;
            StmtBlock(fe.Body, sb, ind + 1);
            loopDepthInCallback--;
            sb.Append(pad).Append("} }\n");
            return;
        }
        // an XmlNode (its children) or an XmlNodeList (FortRise.Content's loaders): index loop over XmlNodeList.Count / Item(int)
        if (coll.Type is INamedTypeSymbol xt && (IsA(xt, "System.Xml.XmlNode") || IsA(xt, "System.Xml.XmlNodeList")))
        {
            var nlT = M(fe.Syntax).Compilation.GetTypeByMetadataName("System.Xml.XmlNodeList");
            var nodeT = M(fe.Syntax).Compilation.GetTypeByMetadataName("System.Xml.XmlNode");
            var l = $"xl_{tmp++}";
            var src = IsA(xt, "System.Xml.XmlNode") ? BclCall(nodeT, "get_ChildNodes", Expr(coll), new List<string>(), "void*") : Expr(coll);
            var count = BclCall(nlT, "get_Count", l, new List<string>(), "int32_t");
            var item = BclCall(nlT, "Item", l, new List<string> { $"i_{l}" }, "void*", new[] { "int32" });
            R.Add("mechanical: foreach over XML nodes -> index loop over XmlNodeList.Count / Item", Where(fe));
            sb.Append(pad).Append($"{{ void* {l} = {src}; for (int32_t i_{l} = 0; {l} && i_{l} < {count}; i_{l}++) {{\n");
            sb.Append(pad).Append($"    {Cpp(vars[0].Type)} {Id(vars[0].Name)} = {item};\n");
            loopDepthInCallback++;
            StmtBlock(fe.Body, sb, ind + 1);
            loopDepthInCallback--;
            sb.Append(pad).Append("} }\n");
            return;
        }
        // a List<T> that is one of ours (a value list, VKind.List: a TF.State model's List<Vector2f>): its own elements, never the game's List
        // calls (the msgpack glue's foreach over BrambleSpreadState.Made called List.get_Count on a VList: saved empty, the spread re-grew
        // after every rollback; the parity sweep, 2026-10-24)
        // (a deconstruction too, foreach (var (seat, device) in placements) over a value list of tuples: InputService v0.28.0)
        if (vars.Count >= 1 && (ValKind(coll.Type) == VKind.List || Strip(coll) is IParameterReferenceOperation { Parameter: var fp } && IsPrimSeqParam(fp)))
        {
            var l = $"vl_{tmp++}";
            sb.Append(pad).Append($"{{ auto* {l} = {Expr(coll)}; for (int32_t i_{l} = 0; {l} && i_{l} < {l}->Count(); i_{l}++) {{\n");
            if (vars.Count == 1) sb.Append(pad).Append($"    {Cpp(vars[0].Type)} {Id(vars[0].Name)} = {l}->Get(i_{l});\n");
            else for (int k = 0; k < vars.Count; k++)   // the tuple's ItemN
                sb.Append(pad).Append($"    {Cpp(vars[k].Type)} {Id(vars[k].Name)} = {l}->Get(i_{l}).Item{k + 1};\n");
            loopDepthInCallback++;
            StmtBlock(fe.Body, sb, ind + 1);
            loopDepthInCallback--;
            sb.Append(pad).Append("} }\n");
            return;
        }
        // a game List<T>: index loop over get_Count / get_Item (docs/ws-tfex-subclass.md row 20: no version check)
        if (coll.Type is INamedTypeSymbol lt && lt.OriginalDefinition.ToDisplayString() == "System.Collections.Generic.List<T>" && !IsLayerGetList(coll))
        {
            var l = $"list_{tmp++}";
            var count = BclCall(lt, "get_Count", l, new List<string>(), "int32_t");
            var item = BclCall(lt, "get_Item", l, new List<string> { $"i_{l}" }, Cpp(lt.TypeArguments[0]), new[] { "int32" });
            sb.Append(pad).Append($"{{ void* {l} = {Expr(coll)}; for (int32_t i_{l} = 0; i_{l} < {count}; i_{l}++) {{\n");
            sb.Append(pad).Append($"    {Cpp(vars[0].Type)} {Id(vars[0].Name)} = {item};\n");
            loopDepthInCallback++;
            StmtBlock(fe.Body, sb, ind + 1);
            loopDepthInCallback--;
            sb.Append(pad).Append("} }\n");
            return;
        }
        // foreach (E x in Enum.GetValues(typeof(E))), E a game enum: the Switch's values of E, in .NET's order (by value)
        if (Strip(coll) is IInvocationOperation { TargetMethod: { Name: "GetValues", ContainingType.SpecialType: SpecialType.System_Enum } } gv &&
            gv.Arguments.Length == 1 && Strip(gv.Arguments[0].Value) is ITypeOfOperation { TypeOperand: INamedTypeSymbol { TypeKind: TypeKind.Enum } et } &&
            B.EnumValues(SwitchName(et)) is { } swv && vars.Count == 1)
        {
            var a = $"ev_{tmp++}";
            var vals = swv.Values.Distinct().OrderBy(v => (ulong)v).Select(v => v.ToString(CultureInfo.InvariantCulture));
            R.Add("mechanical: foreach over Enum.GetValues(typeof(game enum)) -> the Switch's values", Where(fe));
            sb.Append(pad).Append($"{{ static const int64_t {a}[] = {{ {string.Join(", ", vals)} }}; for (int64_t v_{a} : {a}) {{\n");
            sb.Append(pad).Append($"    {Cpp(vars[0].Type)} {Id(vars[0].Name)} = static_cast<{Cpp(vars[0].Type)}>(v_{a});\n");
            loopDepthInCallback++;
            StmtBlock(fe.Body, sb, ind + 1);
            loopDepthInCallback--;
            sb.Append(pad).Append("} }\n");
            return;
        }
        // a lazy LINQ chain (OrderBy, Where ...), a value map or its Keys/Values: the loop body becomes the sequence's visitor
        bool seqLocal = coll is ILocalReferenceOperation slr && seqLocals.Contains(slr.Local);
        bool gameDict = coll.Type is INamedTypeSymbol { Name: "Dictionary", TypeArguments.Length: 2 } && !IsValColl(coll.Type);   // Scene.Layers ...
        // a game Dictionary's Keys / Values (TF.State: level.Layers.Values): SeqOf selects them from the dictionary's slots
        gameDict = gameDict || (coll is IPropertyReferenceOperation { Instance: not null, Property.Name: "Keys" or "Values" } gdk &&
                                Strip(gdk.Instance).Type is INamedTypeSymbol { OriginalDefinition.MetadataName: "Dictionary`2" } && !IsValMap(Strip(gdk.Instance).Type));
        gameDict = gameDict || (coll.Type is INamedTypeSymbol { OriginalDefinition.MetadataName: "HashSet`1" } && !IsValColl(coll.Type));   // a game HashSet
        bool absentColl = AbsentMember(coll) != null || (coll is IPropertyReferenceOperation { Instance: not null } ab && AbsentMember(ab.Instance) != null);   // a FortRise collection: empty
        // an IEnumerable<T> parameter every caller passes a List<T> (TFCommands.WarnOnUnknownVariants' variants): SeqOf enumerates that List
        bool listParam = coll is IParameterReferenceOperation { Parameter: var lpp } && lpp.Type is INamedTypeSymbol { OriginalDefinition.SpecialType: SpecialType.System_Collections_Generic_IEnumerable_T } &&
                         lpp.ContainingSymbol is IMethodSymbol lpm && ListPassedEverywhere(lpm, lpp.Ordinal) != null;
        if (absentColl || gameDict || seqLocal || stateSeqProp || listParam || (coll is IInvocationOperation li && IsEnumerable(li.TargetMethod)) || IsValColl(coll.Type) || IListOfOp(coll) != null || IsLayerGetList(coll) ||
            (coll is IPropertyReferenceOperation { Instance: not null } kp && IsValMap(Strip(kp.Instance).Type)))
        {
            R.Add("mechanical: foreach over a lazy sequence -> its visitor", Where(fe));
            var retFlag = $"ret_{tmp++}";   // a `return;` inside the visit (void method): the method returns after it
            sb.Append(pad).Append($"bool {retFlag} = false; (void){retFlag};\n");
            string retVal = null;   // a `return v;` inside it (a method with a value, outside any other callback): the value, returned after it
            if (current is IMethodSymbol { ReturnsVoid: false } rm0 && callbackDepth.Count == 0 && !rm0.IsIterator)
            { retVal = $"retv_{tmp++}"; sb.Append(pad).Append($"{(ReturnsSnap(rm0) ? $"gen::Snap<{SnapElem(rm0)}>" : Cpp(rm0.ReturnType))} {retVal}{{}}; (void){retVal};\n"); }
            var seqExpr = seqLocal ? Id(((ILocalReferenceOperation)coll).Local.Name) : SeqOf(coll);
            if (vars.Count == 1) sb.Append(pad).Append($"{seqExpr}([&]({Cpp(vars[0].Type)} {Id(vars[0].Name)}) -> bool {{\n");
            else
            {   // a deconstruction (var (a, b)): the element tuple's ItemN
                sb.Append(pad).Append($"{seqExpr}([&](auto e_) -> bool {{\n");
                // a dictionary's KeyValuePair (foreach ((var seat, var input) in map)): Key, Value
                bool kvp = Strip(coll).Type is INamedTypeSymbol { IsGenericType: true } kt && kt.OriginalDefinition.Name is "Dictionary" or "IDictionary" or "IReadOnlyDictionary";
                for (int k = 0; k < vars.Count; k++) sb.Append(pad).Append($"    {Cpp(vars[k].Type)} {Id(vars[k].Name)} = e_.{(kvp ? (k == 0 ? "Key" : "Value") : $"Item{k + 1}")};\n");
            }
            callbackDepth.Push("seq:" + retFlag + (retVal != null ? "|" + retVal : ""));
            var saved = loopDepthInCallback; loopDepthInCallback = 0;
            if (fe.Body is IBlockOperation b) foreach (var s in b.Operations) Stmt(s, sb, ind + 1); else Stmt(fe.Body, sb, ind + 1);
            loopDepthInCallback = saved;
            callbackDepth.Pop();
            sb.Append(pad).Append("    return true;\n").Append(pad).Append("});\n");
            if (current is IMethodSymbol { ReturnsVoid: true }) sb.Append(pad).Append($"if ({retFlag}) return;\n");
            else if (retVal != null) sb.Append(pad).Append($"if ({retFlag}) return {retVal};\n");
            return;
        }
        R.Add("unsupported foreach collection", Where(fe));
        sb.Append(pad).Append($"TFEXGEN_UNSUPPORTED;   /* foreach over {coll.Type?.ToDisplayString()} */\n");
    }
}
