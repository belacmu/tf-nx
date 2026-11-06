// Harmony patches -> exlaunch hooks (TFEX-BASE.md section 7 "With the translator", stage 4). For every [HarmonyPatch] method of the
// units marked `hooks = true` in translate.toml: the target game method (its bound body), one trampoline per target, the patches on it
// in order (prefixes, the original unless a bool prefix said no, postfixes, finalizers), Harmony's injected parameters mapped:
//   __instance -> this, __result -> the result (by reference), __state -> one per patch class and target, __runOriginal,
//   ___field -> the instance's field (by reference), a name of the target's parameters -> that argument (ref when the patch takes ref).
// Output: tfexgen_hooks.{hpp,cpp}: tfex::gen::hooks::Enable("<Namespace.PatchClass>", or a bare name for any namespace) installs that class's targets (hw.txt "genpatch <Class>",
// tfexgen_glue.cpp); a target several enabled classes patch gets one hook that runs all of them.
using System.Text;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using Microsoft.CodeAnalysis.Operations;
using Tomlyn.Model;

namespace TfexTranslator;

public sealed class HookGen
{
    static string Ident(string x) { var sb = new StringBuilder(); foreach (var ch in x) sb.Append(char.IsLetterOrDigit(ch) ? ch : '_'); return sb.ToString(); }
    public sealed record Patch(IMethodSymbol Method, string Kind, string Unit, int Priority);
    public sealed record Target(IMethodSymbol Method, List<Patch> Patches);
    readonly Dictionary<string, Target> targets = new();
    public readonly List<string> Problems = new();
    /* units whose patch classes EnableAll leaves out (translate.toml unit `enable_all = false`: a mod's, enabled by its boot) */
    public readonly HashSet<string> OnDemand = new();
    /* translate.toml [hooks] load_time: game methods that run before code mods are constructed on the Switch (FortRise constructs mods in
     * Main, before TFGame.Load; the Switch after GameData.Load, gap 3): a mod's postfix on one of them is called once at its boot
     * (ReplayLoadTime), as it would have run on the PC. Static methods without parameters only */
    public readonly HashSet<string> LoadTime = new();
    readonly Workspace ws;

    public HookGen(Workspace ws, Tomlyn.Model.TomlTable fortrise = null) { this.ws = ws; this.fortrise = fortrise; if (ws != null) ScanCalls(ws.Compilation); }
    /* translate.toml [fortrise]: a FortRise wrapper's own work around the vanilla body ({ after = "<C++>" }: FortRise's
     * TreasureSpawner.GetArrowShufflePickups shuffles the vanilla result once more), run right after the original, inside the
     * patches' prefixes / finalizers as on the PC; $result is the original's result, $self the object */
    readonly Tomlyn.Model.TomlTable fortrise;

    static AttributeData Attr(ISymbol s, string name) => s.GetAttributes().FirstOrDefault(a => a.AttributeClass?.Name == name || a.AttributeClass?.Name == name + "Attribute");
    static IEnumerable<AttributeData> Attrs(ISymbol s, string name) => s.GetAttributes().Where(a => a.AttributeClass?.Name == name || a.AttributeClass?.Name == name + "Attribute");

    /* the patch kind of a method: by attribute, by Harmony's naming convention, or by a harmony.Patch(...) call that registers it */
    public static string KindOf(IMethodSymbol m)
    {
        foreach (var k in new[] { "Prefix", "Postfix", "Finalizer", "Transpiler", "ReversePatch" })
            if (Attr(m, "Harmony" + k) != null || m.Name == k) return k;
        return calls.TryGetValue(m, out var reg) ? reg[0].Kind : null;
    }

    /* [HarmonyPatch(typeof(T), "Name", new[] { typeof(A) }, MethodType.Getter)] on the class and/or the method, merged */
    public IMethodSymbol TargetOf(IMethodSymbol m, out string why)
    {
        INamedTypeSymbol type = null; string name = null; ITypeSymbol[] argTypes = null; string special = null;
        foreach (var a in Attrs(m.ContainingType, "HarmonyPatch").Concat(Attrs(m, "HarmonyPatch")))
            foreach (var arg in a.ConstructorArguments)
            {
                if (arg.Kind == TypedConstantKind.Type && arg.Value is INamedTypeSymbol t) type = t;
                else if (arg.Kind == TypedConstantKind.Primitive && arg.Value is string s) name = s;
                else if (arg.Kind == TypedConstantKind.Array) argTypes = arg.Values.Select(v => v.Value as ITypeSymbol).Where(v => v != null).ToArray();
                else if (arg.Kind == TypedConstantKind.Enum) special = arg.Type?.Name == "MethodType" ? arg.Value?.ToString() : special;
            }
        // MethodType: 0 Normal, 1 Getter, 2 Setter, 3 Constructor, 4 StaticConstructor
        if (special == "3" && type != null && argTypes == null) { why = null; return type.InstanceConstructors.FirstOrDefault(); }   // (any: as before)
        return Find(type, name, argTypes, special switch { "1" => "Getter", "2" => "Setter", "3" => "Constructor", _ => null }, declared: true, out why);
    }

    /* a game method by type, name, parameter types (null: any, then it must be the only one) and kind (null: a method, Getter, Setter,
     * Constructor (null parameters: the parameterless one, as AccessTools)); `declared` = only the type's own members (Harmony's Declared*,
     * and [HarmonyPatch]), else the base types too (AccessTools.Method & co.) */
    static IMethodSymbol Find(INamedTypeSymbol type, string name, ITypeSymbol[] argTypes, string special, bool declared, out string why)
    {
        why = null;
        if (type == null) { why = "no target type"; return null; }
        bool Same(IMethodSymbol c, ITypeSymbol[] ts) => c.Parameters.Select(p => p.Type).SequenceEqual(ts, SymbolEqualityComparer.Default);
        if (special == "Constructor")
        {
            var ctor = type.InstanceConstructors.FirstOrDefault(c => Same(c, argTypes ?? Array.Empty<ITypeSymbol>()));
            if (ctor == null) why = $"no {type.Name} constructor with these parameters";
            return ctor;
        }
        if (name == null) { why = "no target method name"; return null; }
        for (var t = type; t != null; t = declared ? null : t.BaseType)
        {
            if (special is "Getter" or "Setter")
            {
                var p = t.GetMembers(name).OfType<IPropertySymbol>().FirstOrDefault();
                if (p != null) return special == "Getter" ? p.GetMethod : p.SetMethod;
                continue;
            }
            var cands = t.GetMembers(name).OfType<IMethodSymbol>().ToList();
            if (argTypes != null) cands = cands.Where(c => Same(c, argTypes)).ToList();
            if (cands.Count == 1) return cands[0];
            if (cands.Count > 1) { why = $"{cands.Count} overloads of {t.Name}.{name} and no argument types"; return null; }
        }
        why = $"{type.Name}.{name} not found";
        return null;
    }

    /* Patches registered in code, the common style of FortRise mods: `harmony.Patch(AccessTools.DeclaredMethod(typeof(T), nameof(T.M)),
     * prefix: new HarmonyMethod(typeof(C), nameof(P)))` (FortRise's IHarmony or HarmonyLib's Harmony), read from the call's constant
     * arguments as [HarmonyPatch] is read from attributes; the call itself does nothing on the Switch (the hooks are HookGen's). A target
     * computed at run time (a local, a loop over methods) is a Problem, as a transpiler is. Scanned once per compilation, by patch method. */
    static readonly Dictionary<IMethodSymbol, List<(string Kind, IMethodSymbol Target)>> calls = new(SymbolEqualityComparer.Default);
    static readonly Dictionary<INamedTypeSymbol, List<string>> callProblems = new(SymbolEqualityComparer.Default);   // by patch class (or the call's class)
    static readonly HashSet<Compilation> scanned = new();

    public static void ScanCalls(Compilation comp)
    {
        if (!scanned.Add(comp)) return;
        foreach (var tree in comp.SyntaxTrees)
        {
            SemanticModel model = null;
            foreach (var inv in tree.GetRoot().DescendantNodes().OfType<InvocationExpressionSyntax>())
            {
                if (inv.Expression is not MemberAccessExpressionSyntax { Name.Identifier.ValueText: "Patch" }) continue;
                model ??= comp.GetSemanticModel(tree, ignoreAccessibility: true);
                if (model.GetOperation(inv) is not IInvocationOperation op ||
                    op.TargetMethod.ContainingType?.Name is not ("IHarmony" or "Harmony")) continue;
                var at = $"{tree.FilePath}:{inv.GetLocation().GetLineSpan().StartLinePosition.Line + 1}";
                var site = model.GetEnclosingSymbol(inv.SpanStart)?.ContainingType;
                IMethodSymbol target = null; string why = "no original";
                var patches = new List<(string Kind, IMethodSymbol M)>();
                foreach (var a in op.Arguments)
                {
                    if (a.ArgumentKind == ArgumentKind.DefaultValue) continue;
                    var v = Unwrap(a.Value);
                    if (v.ConstantValue is { HasValue: true, Value: null }) continue;   // prefix: null
                    if (a.Parameter.Name == "original") { target = ResolveAccess(v, out why); continue; }
                    var kind = a.Parameter.Name switch { "prefix" => "Prefix", "postfix" => "Postfix", "finalizer" => "Finalizer", "transpiler" => "Transpiler", _ => null };
                    if (kind == null) { Note(site, $"{at}: Patch's {a.Parameter.Name} (no hook form)"); continue; }
                    var pm = ResolvePatchMethod(v, out var pwhy);
                    if (pm == null) Note(site, $"{at}: {kind}: {pwhy}");
                    else patches.Add((kind, pm));
                }
                foreach (var (kind, pm) in patches)
                {
                    if (kind == "Transpiler") Note(pm.ContainingType, $"{pm.ContainingType.Name}.{pm.Name}: Transpiler (IL-level: no hook) ({at})");
                    else if (target == null) Note(pm.ContainingType, $"{pm.ContainingType.Name}.{pm.Name}: {why} ({at})");
                    else { if (!calls.TryGetValue(pm, out var l)) calls[pm] = l = new(); l.Add((kind, target)); }
                }
            }
        }
    }
    static void Note(INamedTypeSymbol t, string problem)
    {
        if (t == null) return;
        if (!callProblems.TryGetValue(t, out var l)) callProblems[t] = l = new();
        l.Add(problem);
    }
    static IOperation Unwrap(IOperation v)
    {
        while (v is IConversionOperation c) v = c.Operand;
        return v;
    }
    /* the arguments of an AccessTools / HarmonyMethod call by parameter: a Type, a name, the parameter types */
    static (INamedTypeSymbol type, string name, ITypeSymbol[] args, string why) Args(System.Collections.Immutable.ImmutableArray<IArgumentOperation> arguments)
    {
        INamedTypeSymbol type = null; string name = null; ITypeSymbol[] args = null;
        foreach (var a in arguments)
        {
            if (a.ArgumentKind == ArgumentKind.DefaultValue) continue;
            var v = Unwrap(a.Value);
            var pt = a.Parameter.Type;
            if (pt.Name == "Type" && pt is not IArrayTypeSymbol)
            {
                if (v is ITypeOfOperation to && to.TypeOperand is INamedTypeSymbol nt) type = nt;
                else return (null, null, null, $"{a.Parameter.Name}: not a typeof");
            }
            else if (pt.SpecialType == SpecialType.System_String)
            {
                if (v.ConstantValue is { HasValue: true, Value: string s }) name = s;
                else return (null, null, null, $"{a.Parameter.Name}: not a constant name");
            }
            else if (pt is IArrayTypeSymbol { ElementType.Name: "Type" } && a.Parameter.Name is "parameters" or "argumentTypes")
            {
                if (v.ConstantValue is { HasValue: true, Value: null }) continue;
                var tels = v is IArrayCreationOperation ac ? (ac.Initializer?.ElementValues.Select(Unwrap).ToList() ?? new())
                         : v is ICollectionExpressionOperation ce ? ce.Elements.Select(Unwrap).ToList() : null;   // ([typeof(A), typeof(B)]: C# 12)
                if (tels != null && tels.All(e => e is ITypeOfOperation))
                    args = tels.Select(e => ((ITypeOfOperation)e).TypeOperand).ToArray();
                else if (v is IFieldReferenceOperation { Field.Name: "EmptyTypes" }) args = Array.Empty<ITypeSymbol>();
                else return (null, null, null, $"{a.Parameter.Name}: not a list of typeof");
            }
            else if (a.Parameter.Name == "generics") return (null, null, null, "generic method (no hook form)");
        }
        return (type, name, args, null);
    }
    /* a local's initializer, when it's declared with one and never assigned elsewhere in its method */
    static IOperation LocalInit(ILocalReferenceOperation lr)
    {
        if (lr.Local.DeclaringSyntaxReferences.FirstOrDefault()?.GetSyntax() is not Microsoft.CodeAnalysis.CSharp.Syntax.VariableDeclaratorSyntax { Initializer: { } init } decl) return null;
        var body = decl.Ancestors().FirstOrDefault(a => a is Microsoft.CodeAnalysis.CSharp.Syntax.BaseMethodDeclarationSyntax);
        var model = lr.SemanticModel;
        if (body == null || model == null) return null;
        if (body.DescendantNodes().OfType<Microsoft.CodeAnalysis.CSharp.Syntax.AssignmentExpressionSyntax>().Any(x => SymbolEqualityComparer.Default.Equals(model.GetSymbolInfo(x.Left).Symbol, lr.Local))) return null;
        return Unwrap(model.GetOperation(init.Value));
    }
    /* AccessTools.[Declared]Method / PropertyGetter / PropertySetter / Constructor with constant arguments */
    static IMethodSymbol ResolveAccess(IOperation v, out string why)
    {
        // a local holding the lookup (`MethodInfo aim = AccessTools.DeclaredMethod(...); harmony.Patch(aim, ...)`): its initializer, when the
        // local is never assigned again
        if (Unwrap(v) is ILocalReferenceOperation lr && LocalInit(lr) is IOperation li) v = li;
        if (v is not IInvocationOperation inv || inv.TargetMethod.ContainingType?.Name != "AccessTools")
        { why = "target computed at run time (not an AccessTools call)"; return null; }
        var n = inv.TargetMethod.Name;
        bool declared = n.StartsWith("Declared");
        var special = n.Replace("Declared", "") switch { "Method" => null, "PropertyGetter" => "Getter", "PropertySetter" => "Setter", "Constructor" => "Constructor", _ => "?" };
        if (special == "?") { why = $"AccessTools.{n} (not read)"; return null; }
        var (type, name, args, awhy) = Args(inv.Arguments);
        if (awhy != null) { why = awhy; return null; }
        return Find(type, name, args, special, declared, out why);
    }
    /* new HarmonyMethod(typeof(C), nameof(M) [, types]) / new HarmonyMethod(AccessTools.Method(...)) / new HarmonyMethod(delegate) */
    static IMethodSymbol ResolvePatchMethod(IOperation v, out string why)
    {
        if (v is not IObjectCreationOperation oc || oc.Type?.Name != "HarmonyMethod")
        { why = "not a new HarmonyMethod(...)"; return null; }
        if (oc.Arguments.Length > 0 && Unwrap(oc.Arguments[0].Value) is var first)
        {
            if (first is IInvocationOperation) return ResolveAccess(first, out why);
            if (first is IDelegateCreationOperation { Target: IMethodReferenceOperation mr })
            { why = null; return mr.Method; }
        }
        var (type, name, args, awhy) = Args(oc.Arguments);
        if (awhy != null) { why = awhy; return null; }
        return Find(type, name, args, null, declared: false, out why);
    }

    /* a struct's size with C# sequential layout (references 8, natural alignment): for passing it by registers */
    static int? StructBytes(ITypeSymbol t)
    {
        if (t is not INamedTypeSymbol n || n.TypeKind != TypeKind.Struct) return null;
        int off = 0, maxAlign = 1;
        foreach (var f in n.GetMembers().OfType<IFieldSymbol>().Where(f => !f.IsStatic))
        {
            int size, align;
            if (!f.Type.IsValueType) { size = 8; align = 8; }
            else if (f.Type.SpecialType is SpecialType.System_Boolean or SpecialType.System_Byte or SpecialType.System_SByte) { size = 1; align = 1; }
            else if (f.Type.SpecialType is SpecialType.System_Int16 or SpecialType.System_UInt16 or SpecialType.System_Char) { size = 2; align = 2; }
            else if (f.Type.SpecialType is SpecialType.System_Int64 or SpecialType.System_UInt64 or SpecialType.System_Double) { size = 8; align = 8; }
            else if (f.Type.TypeKind == TypeKind.Struct && f.Type.SpecialType == SpecialType.None && f.Type.TypeKind != TypeKind.Enum) { if (StructBytes(f.Type) is not int sb) return null; size = sb; align = sb >= 8 ? 8 : 4; }
            else { size = 4; align = 4; }
            off = (off + align - 1) / align * align + size;
            maxAlign = Math.Max(maxAlign, align);
        }
        return (off + maxAlign - 1) / maxAlign * maxAlign;
    }

    /* translate.toml unit `targets = ["<Type metadata name>.<Method>", ...]`: a patch class whose targets Harmony finds at run time
     * (static TargetMethods(), e.g. VariantButtonConfirmPatch over AccessTools.DeclaredMethod of several types): the same list as data.
     * As DeclaredMethod, a type only counts when it declares the method itself (one overload). */
    List<IMethodSymbol> NamedTargets(IEnumerable<string> named, string patchClass)
    {
        var outList = new List<IMethodSymbol>();
        foreach (var n in named)
        {
            var dot = n.LastIndexOf('.');
            var type = ws.Compilation.GetTypeByMetadataName(n[..dot]);
            var decl = type?.GetMembers(n[(dot + 1)..]).OfType<IMethodSymbol>().ToList();
            if (decl is { Count: 1 }) outList.Add(decl[0]);
            else Problems.Add($"{patchClass}: target {n}: {(type == null ? "type not found" : decl.Count == 0 ? "not declared there (Harmony's DeclaredMethod skips it)" : "overloaded")}");
        }
        return outList;
    }

    public void Collect(INamedTypeSymbol patchClass, string unit, ICollection<string> exclude = null, IReadOnlyCollection<string> named = null,
                        ICollection<string> only = null)
    {
        if (callProblems.TryGetValue(patchClass, out var cp)) Problems.AddRange(cp);
        foreach (var m in patchClass.GetMembers().OfType<IMethodSymbol>())
        {
            if (exclude != null && exclude.Contains(m.Name)) continue;   // translate.toml unit `exclude`: the ledger keeps it off the Switch
            if (only is { Count: > 0 } && !only.Contains(m.Name)) continue;   // a unit's `members`: only those patches (MainMenuSetState)
            if (calls.TryGetValue(m, out var reg)) { foreach (var (k, t) in reg) Add(t, m, k, unit); continue; }   // registered by harmony.Patch(...)
            var kind = KindOf(m);
            if (kind == null) continue;
            if (kind is "Transpiler" or "ReversePatch") { Problems.Add($"{patchClass.Name}.{m.Name}: {kind} (IL-level: no hook)"); continue; }
            var target = TargetOf(m, out var why);
            if (target == null && named is { Count: > 0 })
            {
                foreach (var nt in NamedTargets(named, patchClass.Name)) Add(nt, m, kind, unit);
                continue;
            }
            if (target == null) { Problems.Add($"{patchClass.Name}.{m.Name}: {why}"); continue; }
            Add(target, m, kind, unit);
        }
    }
    /* FortRise's MonoMod patches (a `monomod = true` unit: class patch_X : X): a method of the patch class named as one of X's (a
     * property's accessor too: get_GoalScore) with the same parameters (patch_Session for Session) replaces X's body, as MonoMod does;
     * TF.EX's Harmony patches on the same method wrap it, as on the PC. Its `orig_X` (extern) is the original: [fortrise] orig rules. */
    public void CollectMonoMod(INamedTypeSymbol patchClass, string unit, ICollection<string> only = null)
    {
        var game = patchClass.BaseType;
        if (game == null) { Problems.Add($"{patchClass.Name}: no patched game class"); return; }
        static string Plain(ITypeSymbol t) => t.ToDisplayString().Replace("patch_", "");
        foreach (var m in patchClass.GetMembers().OfType<IMethodSymbol>())
        {
            if (only is { Count: > 0 } && !only.Contains(m.Name)) continue;
            if (m.IsExtern || m.Name.StartsWith("orig_") || m.MethodKind is MethodKind.Constructor or MethodKind.StaticConstructor) continue;
            if (Attr(m, "MonoModIgnore") != null) continue;
            // FortRise's own [Prefix("<method name or IL signature>")] / [Postfix(...)] (an instance method of the patch class on the game
            // method it names: Player.Added's hair, PlayerHair's constructor defaults)
            var fk = new[] { "Prefix", "Postfix" }.FirstOrDefault(k => Attr(m, k) is AttributeData ka && ka.AttributeClass?.ContainingNamespace?.ToDisplayString() != "HarmonyLib");
            if (fk != null && Attr(m, fk).ConstructorArguments.FirstOrDefault().Value is string named)
            {
                IMethodSymbol ft;
                if (named.Contains(".ctor("))
                {
                    var sig = named[(named.IndexOf('(') + 1)..named.LastIndexOf(')')].Split(',', StringSplitOptions.RemoveEmptyEntries);
                    ft = game.InstanceConstructors.FirstOrDefault(c => c.Parameters.Length == sig.Length &&
                        c.Parameters.Zip(sig).All(z => z.First.Type.ToDisplayString() == z.Second.Trim() || $"{z.First.Type.ContainingNamespace}.{z.First.Type.MetadataName}" == z.Second.Trim()));
                }
                else
                {
                    var cs = game.GetMembers(named).OfType<IMethodSymbol>().ToList();
                    ft = cs.Count == 1 ? cs[0] : null;
                }
                if (ft == null) { Problems.Add($"{patchClass.Name}.{m.Name}: [{fk}(\"{named}\")]: no single {game.Name} method"); continue; }
                // the Switch's constructor makes its object (called with x0 = null): a constructor "prefix" (it only sets FortRise's added
                // fields, which the game's constructor never reads) runs after it, on the object (TFEX-DIFF)
                Add(ft, m, ft.MethodKind == MethodKind.Constructor ? "Postfix" : fk, unit);
                continue;
            }
            if (m.MethodKind != MethodKind.Ordinary && m.MethodKind != MethodKind.PropertyGet && m.MethodKind != MethodKind.PropertySet) continue;
            var target = game.GetMembers(m.Name).OfType<IMethodSymbol>().FirstOrDefault(g => g.IsStatic == m.IsStatic && g.Parameters.Length == m.Parameters.Length &&
                g.Parameters.Zip(m.Parameters).All(z => Plain(z.First.Type) == Plain(z.Second.Type)));
            if (target == null) { Problems.Add($"{patchClass.Name}.{m.Name}: no {game.Name}.{m.Name} with these parameters (MonoMod adds it: no hook)"); continue; }
            Add(target, m, "Replace", unit);
        }
    }

    /* the collected targets and their patches, one line each, then the problems (translator hooks-scan) */
    public IEnumerable<string> Describe() =>
        targets.OrderBy(kv => kv.Key, StringComparer.Ordinal).Select(kv => $"{kv.Key}: " + string.Join(", ", kv.Value.Patches.Select(p => $"{p.Method.ContainingType.Name}.{p.Method.Name} ({p.Kind.ToLowerInvariant()})")))
        .Concat(Problems.Select(p => "problem: " + p));

    void Add(IMethodSymbol target, IMethodSymbol m, string kind, string unit)
    {
        var key = target.ContainingType.ToDisplayString() + "." + target.Name + "(" + string.Join(",", target.Parameters.Select(p => p.Type.ToDisplayString())) + ")";
        if (!targets.TryGetValue(key, out var t)) targets[key] = t = new Target(target, new List<Patch>());
        var prio = Attr(m, "HarmonyPriority")?.ConstructorArguments.FirstOrDefault().Value is int pr ? pr : 400;
        t.Patches.Add(new Patch(m, kind, unit, prio));
    }

    /* one exlaunch trampoline per target */
    public (string h, string c) Emit(Func<ITypeSymbol, string> cpp, Func<IMethodSymbol, string> bodyOf, Func<ITypeSymbol, string> switchName,
                                     Func<IFieldSymbol, string> fieldOf, Report report, Func<ITypeSymbol, string> typeKeyOf = null)
    {
        var h = new StringBuilder("/* GENERATED by tools/tfex/translator (HookGen): TF.EX's Harmony patches as exlaunch hooks. Do not edit. */\n#pragma once\n\n" +
                                  "namespace tfex::gen::hooks {\n/* installs the hooks of TF.EX patch class `cls` (its simple name); false if it has none here. `skip`: patch methods (comma-\n" +
                                  " * separated names, e.g. \"Session_EndRound,Session_CreateResults\") NOT turned on: for targets the hand code already hooks, whose hook\n" +
                                  " * then calls the generated method itself (two trampolines on one address are unsound) */\nbool Enable(const char* cls, const char* skip = nullptr);\n" +
                                  "/* every TF.EX patch class generated here, on (TF.EX loads all its Harmony patches at start-up): the number of classes */\nint EnableAll();\nint ClassCount();\n" +
                                  "/* patch class `cls`'s postfixes on load-time targets ([hooks] load_time), called once: how many */\nint ReplayLoadTime(const char* cls);\n}\n" +
                                  "#include \"tfexgen_code_rt.hpp\"\nnamespace tfex::gen::hooks {\nusing namespace tfex::gen;\n" +
                                  "/* One struct per hooked game method: a call of it (its arguments as fields). `c()` runs TF.EX's patches on it and the original\n" +
                                  " * between them, as Harmony does; `c.Orig()` the original only; `ran_` says whether the original ran, `result_` is the result.\n" +
                                  " * `Wrap(shell)`: the Switch's own code around TF.EX's patches on that method (TFEX-DIFF), on the one trampoline the method has:\n" +
                                  " * shell(c) gets each call and decides what runs. New TF.EX patches on the method run inside c() without a change here. */\n");
        var c = new StringBuilder("/* GENERATED by tools/tfex/translator (HookGen): TF.EX's Harmony patches as exlaunch hooks. Do not edit. */\n" +
                                  "#include \"tfexgen_hooks.hpp\"\n#include \"tfexgen_code_rt.hpp\"\n#include \"tfexgen_binding.hpp\"\n#include \"tfexgen_services.hpp\"\n#include \"../../hwlog.hpp\"\n#include \"lib.hpp\"\n#include <cstring>\n");
        foreach (var u in targets.Values.SelectMany(t => t.Patches).Select(p => p.Unit).Distinct().OrderBy(u => u, StringComparer.Ordinal)) c.Append($"#include \"tfexgen_{u}.hpp\"\n");
        c.Append("\nnamespace tfex::gen::hooks {\nusing namespace tfex::gen;\nnamespace {\n");
        var installs = new StringBuilder();
        var replays = new StringBuilder();
        // each hooked method's original, callable by name: FortRise's `orig_X` (its name for the vanilla body: TreasureSpawner.orig_
        // GetRandomArrowType inside TF.State's GetRandomArrowType prefix) is, on the Switch, the original under the hook
        var origs = new StringBuilder();
        var methods = new StringBuilder();   // the call structs' members (outside the anonymous namespace, after the hooks)
        string BaseName(IMethodSymbol m) => $"{Ident(m.ContainingType.Name)}_{(m.MethodKind == MethodKind.Constructor ? "ctor" : Ident(m.Name))}";
        var baseCount = targets.Values.GroupBy(t => BaseName(t.Method)).ToDictionary(g => g.Key, g => g.Count());
        string CallName(IMethodSymbol m) => baseCount[BaseName(m)] == 1 ? BaseName(m)
            : BaseName(m) + "_" + string.Join("_", m.Parameters.Select(p => Ident(p.Type.Name)));
        var origNames = targets.Values.Where(t => t.Method.MethodKind != MethodKind.Constructor)
            .GroupBy(t => $"Orig_{Ident(t.Method.ContainingType.Name)}_{t.Method.Name}").Where(g => g.Count() == 1).Select(g => g.Key).ToHashSet();
        int n = 0;
        foreach (var (key, t) in targets.OrderBy(kv => kv.Key, StringComparer.Ordinal))
        {
            var tm = t.Method;
            var hn = $"Hook_{n++}";
            bool ctor = tm.MethodKind == MethodKind.Constructor;
            // a constructor returns its object in x0 on the Switch (BRUTE; called with self = null it allocates it), so its hook does too:
            // `obj` is self, then what the original returned (Integration A, 2026-10-17: a void hook left x0 garbage)
            // a game array result is the managed array's pointer in x0: gen::Arr (several words) would come back through memory (x8), and
            // the game's caller read garbage as the array (VersusAwards.GetAwards' List<AwardInfo>[]: VersusMatchResults crashed, 2026-10-28)
            var ret = ctor || tm.ReturnType is IArrayTypeSymbol ? "void*" : tm.ReturnsVoid ? "void" : cpp(tm.ReturnType);
            var ps = new List<string>();
            var args = new List<string>();
            if (!tm.IsStatic) { ps.Add("void* self"); args.Add("self"); }
            var raw = new HashSet<string>();   // by-value game structs with no C++ type: forwarded as their registers, not readable by patches
            var arrays = new HashSet<string>();   // game array arguments: the managed array (void*)
            int rawRegs = 0;
            foreach (var p in tm.Parameters)
            {
                var pc = cpp(p.Type);
                if (p.RefKind == RefKind.None && p.Type.IsValueType && p.Type.TypeKind == TypeKind.Struct && pc.Contains("/*"))
                {   // AAPCS64: up to 16 bytes in one or two x registers, larger by reference (one)
                    int nr = StructBytes(p.Type) is int b && b > 8 && b <= 16 ? 2 : 1;
                    for (int r = 0; r < nr; r++) { ps.Add($"uint64_t {p.Name}_r{r}"); args.Add($"{p.Name}_r{r}"); }
                    raw.Add(p.Name); rawRegs += nr - 1;
                    continue;
                }
                if (p.RefKind == RefKind.None && p.Type is IArrayTypeSymbol)
                {   // a game array argument is one register, the managed array (a C++ gen::Arr by value shifted every later argument: TreasureChest's
                    // Pickups[] moved its timer, and the original made a chest from garbage, 2026-10-21); a patch that reads it gets it wrapped
                    ps.Add($"void* {p.Name}"); args.Add(p.Name); arrays.Add(p.Name);
                    continue;
                }
                ps.Add($"{pc}{(p.RefKind != RefKind.None ? "*" : "")} {p.Name}"); args.Add(p.Name);
            }
            // the Switch's method may take more than the PC's (1.4.1.6's RollcallElement..ctor has a trailing flag in w2 its callers set:
            // Integration A, 2026-10-17): the hook forwards the remaining integer argument registers, up to x7, to the original untouched
            int intRegs = rawRegs + (tm.IsStatic ? 0 : 1) + tm.Parameters.Count(p => p.RefKind != RefKind.None ||
                !(p.Type.SpecialType is SpecialType.System_Single or SpecialType.System_Double || p.Type.ToDisplayString() is "Microsoft.Xna.Framework.Vector2"));
            for (int k = intRegs; k < 8; k++) { ps.Add($"uint64_t x{k}_"); args.Add($"x{k}_"); }
            var sn = CallName(tm);
            var patchList = string.Join(", ", t.Patches.Select(p => $"{p.Method.ContainingType.Name}.{p.Method.Name} ({p.Kind.ToLowerInvariant()})"));
            c.Append($"/* {tm.ToDisplayString()}: {patchList} */\n");
            c.Append($"bool g_on_{hn}[{t.Patches.Count}];\nint g_calls_{hn} = 0;\nvoid (*g_shell_{hn})({sn}&) = nullptr;\n");
            c.Append($"HOOK_DEFINE_TRAMPOLINE({hn}) {{\n    static {ret} Callback({string.Join(", ", ps)}) {{\n");
            c.Append($"        {sn} c{{{string.Join(", ", args)}}};\n        if (g_shell_{hn}) g_shell_{hn}(c); else c();\n");
            if (ret != "void") c.Append("        return c.result_;\n");
            c.Append("    }\n};\n");
            c.Append($"bool g_installed_{hn} = false;\n\n");
            h.Append($"/* {tm.ToDisplayString()}: {patchList} */\nstruct {sn} {{\n");
            foreach (var p in ps) h.Append($"    {p};\n");
            h.Append("    bool ran_ = false;\n");
            // a shell's step between TF.EX's prefixes and the original (the Switch's own setup the original needs: MapScene.StartSession's
            // native session start), when the original runs; the prefix / original / postfix order stays the generated one
            h.Append($"    void (*beforeOrig_)({sn}&) = nullptr;\n");
            if (ret != "void") h.Append($"    {ret} result_{{}};\n");
            h.Append($"    void operator()();\n    void Orig();\n    static void Wrap(void (*shell)({sn}&));\n}};\n");
            var full = c;   // the hook itself is done; the struct's call operator holds the patches and the original
            c = methods;
            c.Append($"void {sn}::operator()() {{\n");
            if (ctor) c.Append("        void* obj = self;\n");
            // a constructor prefix taking __instance (LoaderAI's Level ctor prefix: its agents keep the level): on the PC it gets the object
            // newobj allocated. On the Switch a caller may pass x0 = null, the constructor allocating it (above); then the object is
            // allocated here, as newobj, and the constructor runs on it (BRUTE constructors take `this`)
            var instPre = t.Patches.Select((p, i) => (p, i)).Where(x => x.p.Kind == "Prefix" && x.p.Method.Parameters.Any(q => q.Name == "__instance")).Select(x => $"g_on_{hn}[{x.i}]").ToList();
            if (ctor && typeKeyOf != null && !tm.ContainingType.IsAbstract && instPre.Count > 0)   // (only while such a prefix is on: its mod loaded)
                c.Append($"        if (!self && ({string.Join(" || ", instPre)})) {{ self = gen::Fn<void* (*)(void*)>(game::brute::NewObject)(gen::TypeOf({typeKeyOf(tm.ContainingType)})); obj = self; }}   /* newobj before the prefix's __instance */\n");
            else if (ret != "void") c.Append($"        {ret} __result{{}};\n");
            c.Append("        bool run = true;\n");
            // __state: one per patch class on this target
            foreach (var g in t.Patches.GroupBy(p => p.Method.ContainingType, SymbolEqualityComparer.Default))
            {
                var st = g.SelectMany(p => p.Method.Parameters).FirstOrDefault(p => p.Name == "__state");
                if (st != null) c.Append($"        {cpp(st.Type)} state_{((INamedTypeSymbol)g.Key).Name}{{}};\n");
            }
            string Call(Patch p, int i)
            {
                var a = new List<string>();
                if (!p.Method.IsStatic) a.Add(ctor ? "obj" : "self");   // FortRise's instance [Prefix] / [Postfix] (CollectMonoMod): `this` is the object
                foreach (var pp in p.Method.Parameters)
                {
                    if (pp.Name == "__instance") a.Add(tm.IsStatic ? "nullptr" : ctor ? "obj" : "self");
                    else if (pp.Name == "__result" && !ctor && tm.ReturnType is IArrayTypeSymbol && pp.Type is IArrayTypeSymbol rat)
                        a.Add($"gen::ArrRef<{cpp(rat.ElementType)}>(__result, bind::m_Array_get_Length)");   // the managed array as the patch's gen::Arr, written back
                    else if (pp.Name == "__result") a.Add(ret == "void" || ctor ? "/*no result*/0" : "__result");
                    else if (pp.Name == "__state") a.Add($"state_{p.Method.ContainingType.Name}");
                    else if (pp.Name == "__runOriginal") a.Add("run");
                    else if (pp.Name == "__exception") a.Add("nullptr");   // no exceptions on the Switch: a finalizer sees none
                    else if (pp.Name.StartsWith("___"))
                    {
                        var f = tm.ContainingType.GetMembers(pp.Name[3..]).OfType<IFieldSymbol>().FirstOrDefault();
                        if (f == null) { report.Add("hook: unsupported", $"{p.Method.ContainingType.Name}.{p.Method.Name}: field {pp.Name[3..]}"); a.Add("{}"); }
                        else a.Add($"gen::Fld<{cpp(f.Type)}>(self, {fieldOf(f)})");
                    }
                    else if (arrays.Contains(pp.Name) && pp.Type is IArrayTypeSymbol pat)
                        a.Add($"gen::GameArr<{cpp(pat.ElementType)}>({pp.Name}, bind::m_Array_get_Length)");   // the managed array as the patch's gen::Arr
                    else if (raw.Contains(pp.Name)) { report.Add("hook: unsupported", $"{p.Method.ContainingType.Name}.{p.Method.Name}: parameter {pp.Name} (a game struct with no C++ type)"); a.Add("{}"); }
                    else if (tm.Parameters.FirstOrDefault(q => q.Name == pp.Name) is IParameterSymbol tp)
                        a.Add(tp.RefKind != RefKind.None ? $"*{tp.Name}" : tp.Name);
                    else { report.Add("hook: unsupported", $"{p.Method.ContainingType.Name}.{p.Method.Name}: parameter {pp.Name}"); a.Add("{}"); }
                }
                return $"tfex::gen::{p.Unit}::{p.Method.Name}({string.Join(", ", a)})";
            }
            var ordered = t.Patches.Select((p, i) => (p, i)).ToList();
            foreach (var (p, i) in ordered.Where(x => x.p.Kind == "Prefix").OrderByDescending(x => x.p.Priority))
                c.Append(p.Method.ReturnType.SpecialType == SpecialType.System_Boolean
                    ? $"        if (g_on_{hn}[{i}] && !{Call(p, i)}) run = false;   /* a bool prefix: false skips the original */\n"
                    : $"        if (g_on_{hn}[{i}]) {Call(p, i)};\n");
            // the first calls of a hook in the log (hw.txt genpatch runs: which patches ran, and whether the original did)
            var label = $"{tm.ContainingType.Name}.{(ctor ? "ctor" : tm.Name)}";
            var ints = tm.Parameters.Where(q => q.RefKind == RefKind.None && (q.Type.TypeKind == TypeKind.Enum || q.Type.SpecialType is SpecialType.System_Int32 or SpecialType.System_Boolean)).ToList();
            var shown = ints.Count > 0 ? " (" + string.Join(", ", ints.Select(q => q.Name + " %d")) + ")" : "";
            c.Append($"        if (g_calls_{hn} < 8) hw::Log(\"[tfexgen] hook {label} call %d{shown}: {string.Join(" ", t.Patches.Select(p => p.Method.ContainingType.Name + "." + p.Method.Name + "=%d"))}, original %s\", ++g_calls_{hn}" + string.Concat(ints.Select(q => $", (int){q.Name}")) +
                     string.Concat(t.Patches.Select((p, i) => $", (int)g_on_{hn}[{i}]")) + ", run ? \"runs\" : \"skipped\");\n");
            // a constructor has two call forms on the Switch: x0 = null, and the ctor allocates and returns the new object (MainMenu.set_State
            // -> ScreenTitle..ctor); or x0 = an object the caller allocated, and the return value is NOT it (Pickup's ctor called from a
            // subclass returns 1 on one path: genstate's Pickup crash). So: self when the caller passed one, else what the ctor returned.
            var frKey = tm.ContainingType.ToDisplayString() + "." + tm.Name;
            string frAfter = fortrise != null && fortrise.TryGetValue(frKey, out var frv) && frv is Tomlyn.Model.TomlTable frt && frt.TryGetValue("after", out var fra) ? (string)fra : null;
            if (frAfter != null && !ctor)
            {   // (a void method too: FortRise's Session.StartGame, the game's body then a custom mode's OnStartGame)
                c.Append(ret == "void"
                    ? $"        if (run) {{ {hn}::Orig({string.Join(", ", args)}); {frAfter.Replace("$self", "self")}; }}   /* + FortRise's wrapper ([fortrise] after) */\n"
                    : $"        if (run) {{ __result = {hn}::Orig({string.Join(", ", args)}); {frAfter.Replace("$result", "__result").Replace("$self", "self")}; }}   /* + FortRise's wrapper ([fortrise] after) */\n");
                report.Add("hand rule: FortRise's wrapper around the vanilla body, after the original ([fortrise] after)", frKey);
            }
            else
            {
            c.Append("        if (run && beforeOrig_) beforeOrig_(*this);\n");
            // a MonoMod replacement (FortRise's patch_X method): its body instead of the game's, its orig_X the original
            var repl = ordered.FirstOrDefault(x => x.p.Kind == "Replace");
            if (repl.p != null && !ctor)
            {
                var ra = new List<string>();
                if (!tm.IsStatic) ra.Add("self");
                ra.AddRange(tm.Parameters.Select(q => raw.Contains(q.Name) ? "{}" : q.RefKind != RefKind.None ? $"*{q.Name}" : arrays.Contains(q.Name) && q.Type is IArrayTypeSymbol qa ? $"gen::GameArr<{cpp(qa.ElementType)}>({q.Name}, bind::m_Array_get_Length)" : q.Name));
                var rc = $"tfex::gen::{repl.p.Unit}::{repl.p.Method.Name}({string.Join(", ", ra)})";
                var oc = $"{hn}::Orig({string.Join(", ", args)})";
                c.Append(ret == "void" ? $"        if (run) {{ if (g_on_{hn}[{repl.i}]) {rc}; else {oc}; }}   /* MonoMod: FortRise's body */\n"
                                       : $"        if (run) __result = g_on_{hn}[{repl.i}] ? {rc} : {oc};   /* MonoMod: FortRise's body */\n");
            }
            else
            c.Append(ctor ? $"        if (run) {{ void* r_ = {hn}::Orig({string.Join(", ", args)}); obj = self ? self : r_; }}\n" :
                     ret == "void" ? $"        if (run) {hn}::Orig({string.Join(", ", args)});\n" : $"        if (run) __result = {hn}::Orig({string.Join(", ", args)});\n");
            }
            foreach (var (p, i) in ordered.Where(x => x.p.Kind is "Postfix" or "Finalizer").OrderByDescending(x => x.p.Priority))
                c.Append($"        if (g_on_{hn}[{i}]) {Call(p, i)};\n");
            c.Append("        ran_ = run;\n");
            if (ctor) c.Append("        result_ = obj;\n");
            else if (ret != "void") c.Append("        result_ = __result;\n");
            c.Append("}\n");
            c.Append($"void {sn}::Orig() {{ ran_ = true; " + (ctor ? $"void* r_ = {hn}::Orig({string.Join(", ", args)}); result_ = self ? self : r_;" :
                     ret == "void" ? $"{hn}::Orig({string.Join(", ", args)});" : $"result_ = {hn}::Orig({string.Join(", ", args)});") + " }\n");
            var wbody = bodyOf(tm);
            c.Append($"void {sn}::Wrap(void (*shell)({sn}&)) {{ g_shell_{hn} = shell; if (!g_installed_{hn} && {wbody}) {{ {hn}::InstallAtOffset({wbody}); g_installed_{hn} = true; }} }}\n\n");
            c = full;
            var oname = $"Orig_{Ident(tm.ContainingType.Name)}_{tm.Name}";
            if (!ctor && origNames.Contains(oname) && raw.Count == 0)
            {
                var ops = ps.Where(x => !System.Text.RegularExpressions.Regex.IsMatch(x, @"^uint64_t x\d_$")).ToList();
                var oargs = args.Select(a => System.Text.RegularExpressions.Regex.IsMatch(a, @"^x\d_$") ? "0" : a);
                h.Append($"{ret} {oname}({string.Join(", ", ops)});   /* {tm.ToDisplayString()}: the original under the hook */\n");
                origs.Append($"{ret} {oname}({string.Join(", ", ops)}) {{ {(ret == "void" ? "" : "return ")}{hn}::Orig({string.Join(", ", oargs)}); }}\n");
            }
            var body = bodyOf(tm);
            for (int i = 0; i < t.Patches.Count; i++)
                installs.Append($"    if (Is(cls, \"{t.Patches[i].Method.ContainingType.ToDisplayString()}\") && !Listed(skip, \"{t.Patches[i].Method.Name}\")) {{ g_on_{hn}[{i}] = true; any = true;" +
                                $" if (!g_installed_{hn} && {body}) {{ {hn}::InstallAtOffset({body}); g_installed_{hn} = true; }} }}\n");
            report.Add("mechanical: Harmony patch -> exlaunch hook", $"{tm.ContainingType.Name}.{tm.Name}: {string.Join(", ", t.Patches.Select(p => p.Method.ContainingType.Name + "." + p.Method.Name))}");
            if (LoadTime.Contains(tm.ContainingType.ToDisplayString() + "." + tm.Name))
            {
                if (!tm.IsStatic || tm.Parameters.Length > 0) Problems.Add($"{tm.ToDisplayString()}: [hooks] load_time takes static methods without parameters");
                else
                    foreach (var p in t.Patches.Where(p => p.Kind == "Postfix" && p.Method.Parameters.Length == 0))
                    {
                        replays.Append($"    if (Is(cls, \"{p.Method.ContainingType.ToDisplayString()}\")) {{ tfex::gen::{p.Unit}::{p.Method.Name}(); n++; }}   /* {tm.ContainingType.Name}.{tm.Name} */\n");
                        report.Add("mechanical: a postfix on a load-time target, replayed at its mod's boot ([hooks] load_time)", $"{tm.ContainingType.Name}.{tm.Name}: {p.Method.ContainingType.Name}.{p.Method.Name}");
                    }
            }
        }
        // a patch class is named by its full name (two mods may each have a MyTFGame); a bare name (hw.txt genpatch) matches any namespace
        c.Append("/* `cls` names the patch class `full`: its full name, or a bare name for any namespace (hw.txt genpatch) */\n" +
                 "bool Is(const char* cls, const char* full) {\n    if (!std::strcmp(cls, full)) return true;\n    if (std::strchr(cls, '.')) return false;\n" +
                 "    size_t n = std::strlen(cls), k = std::strlen(full);\n    return k > n && full[k - n - 1] == '.' && !std::strcmp(full + k - n, cls);\n}\n");
        c.Append("/* `name` is one of the comma-separated names in `list` */\n" +
                 "bool Listed(const char* list, const char* name) {\n    size_t n = std::strlen(name);\n" +
                 "    for (const char* p = list; p && *p;) {\n        const char* e = std::strchr(p, ',');\n        size_t k = e ? (size_t)(e - p) : std::strlen(p);\n" +
                 "        while (k && p[0] == ' ') { p++; k--; }\n        while (k && p[k - 1] == ' ') k--;\n" +
                 "        if (k == n && !std::strncmp(p, name, n)) return true;\n        p = e ? e + 1 : nullptr;\n    }\n    return false;\n}\n");
        // EnableAll's classes: TF.EX's (it loads all its patches at start-up); a mod's units (enable_all = false: Teams+) are left to the
        // mod's own boot, which enables them by name when the mod is on the card
        var classes = targets.Values.SelectMany(t => t.Patches).Where(p => !OnDemand.Contains(p.Unit)).Select(p => p.Method.ContainingType.ToDisplayString()).Distinct().OrderBy(x => x, StringComparer.Ordinal).ToList();
        c.Append("} // namespace\n\n").Append(methods).Append(origs).Append("\nbool Enable(const char* cls, const char* skip) {\n    bool any = false;\n").Append(installs).Append("    return any;\n}\n\n")
         .Append($"const char* const kClasses[] = {{ {string.Join(", ", classes.Select(x => $"\"{x}\""))} }};\n")
         .Append("int ClassCount() { return (int)(sizeof kClasses / sizeof *kClasses); }\n")
         .Append("int EnableAll() { int n = 0; for (const char* cls : kClasses) n += Enable(cls) ? 1 : 0; return n; }\n\n")
         .Append("int ReplayLoadTime(const char* cls) {\n    int n = 0;\n").Append(replays).Append("    return n;\n}\n\n} // namespace tfex::gen::hooks\n");
        h.Append("}\n");
        foreach (var p in Problems) report.Add("hook: not generated", p);
        return (h.ToString(), c.ToString());
    }
}
