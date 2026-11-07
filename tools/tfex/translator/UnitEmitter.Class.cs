// Classes deriving from game classes (docs/ws-tfex-subclass.md: rows 1-4), delegates (row 7), patterns (row 13), LINQ (rows 14-15),
// DynamicData (rows 16-17) and the hand rules written as C# glue (translator/glue: rewrites of a call, replacements of a member).
using System.Text;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.Operations;
using Tomlyn.Model;

namespace TfexTranslator;

public sealed partial class UnitEmitter
{
    bool isClass;
    readonly Dictionary<ILocalSymbol, string> snapLocals = new(SymbolEqualityComparer.Default);
    HashSet<ILocalSymbol> boxesViaClosure = new(SymbolEqualityComparer.Default);   /* in a lambda body: its boxed captures, reached as c_.<name> */
    readonly Dictionary<ILocalSymbol, ITypeSymbol> dynLocals = new(SymbolEqualityComparer.Default);
    readonly Dictionary<ILocalSymbol, string> constLocals = new(SymbolEqualityComparer.Default);   /* locals that are a known constant (an unrolled foreach) */
    readonly StringBuilder lambdaDecls = new(), lambdaBodies = new();
    int lambdaCount, delegateIndex;
    readonly List<(IMethodSymbol m, string fn, string slot)> overrides = new();

    /* ---------------------------------------------------------------- the class ---------------------------------------------------------------- */
    enum BaseKind { Game, Ours, Object }
    /* a FortRise base translate.toml [fortrise] marks absent (ModuleSettings): not on the Switch, the class derives from object there */
    BaseKind Kind(INamedTypeSymbol bt) => gen.AbsentType(bt) != null ? BaseKind.Object : InBinary(bt) || CorlibBase(bt) ? BaseKind.Game : OriginOf(bt) == Origin.Tfex ? BaseKind.Ours : BaseKind.Object;
    readonly HashSet<string> headerIncludes = new();
    string BaseUnit(INamedTypeSymbol bt)
    {
        var u = OtherUnit(bt) ?? AutoU(bt);
        includes.Add($"tfexgen_{u["name"]}.hpp");
        return $"tfex::gen::{u["name"]}";
    }
    /* our virtual methods (declared here, not overrides): slots after the base's vtable */
    List<IMethodSymbol> NewVirtuals() => type.GetMembers().OfType<IMethodSymbol>()
        .Where(m => (m.IsVirtual || m.IsAbstract) && !m.IsOverride && !m.IsImplicitlyDeclared && m.MethodKind is MethodKind.Ordinary or MethodKind.PropertyGet or MethodKind.PropertySet)
        .OrderBy(m => m.Locations[0].SourceSpan.Start).ToList();
    string BaseVtableBytes(INamedTypeSymbol bt) => Kind(bt) switch
    {
        BaseKind.Game => B.Need("vb", SwitchName(bt)),
        BaseKind.Ours => BaseUnit(bt) + "::kVtableBytes",
        _ => B.Need("vb", "System.Object"),
    };

    void ClassPrelude(StringBuilder h, StringBuilder c)
    {
        var bt = type.BaseType;
        var kind = Kind(bt);
        R.Add("mechanical: class of ours (MakeClass, vtable overrides)", $"{type.Name} : {bt.Name}");
        headerIncludes.Add("tfexgen_binding.hpp");
        // the layout: the base's, then our instance fields (auto-properties too)
        var hs = new StringBuilder();
        string baseLayout = kind switch
        {
            BaseKind.Game => $"struct Self {{\n    uint8_t base_[{B.Need("z", SwitchName(bt))}];\n",
            BaseKind.Ours => $"struct Self : {BaseUnit(bt)}::Self {{\n",
            _ => "struct Self {\n    uint8_t base_[0x10];   /* System.Object: descriptor, sync word */\n",
        };
        if (kind == BaseKind.Ours) headerIncludes.Add($"tfexgen_{OtherUnit(bt)["name"]}.hpp");
        hs.Append($"/* {type.ToDisplayString()} : {bt.ToDisplayString()} */\n").Append(baseLayout);
        foreach (var f in type.GetMembers().OfType<IFieldSymbol>().Where(f => !f.IsStatic && !f.IsConst && f.AssociatedSymbol == null))
        {
            if (ModelCollFieldCpp(f) is string mcl) { hs.Append($"    {mcl} {Id(f.Name)};   /* {f.ToDisplayString()} (a model list, held by value) */\n"); continue; }
            if (IsTaskType(f.Type)) { hs.Append($"    uint8_t {Id(f.Name)};   /* {f.ToDisplayString()} (a Task: no state, tasks run inline, S-async) */\n"); continue; }
            hs.Append($"    {Cpp(f.Type)} {Id(f.Name)};   /* {f.ToDisplayString()} */\n");
            if (gen.Models.IsModel(f.Type))
                hs.Append($"    tfex::gen::ws::{f.Type.Name} {Id(f.Name)}_box_[{ModelBoxes}]; int32_t {Id(f.Name)}_next_;   /* its storage (gen::KeepModel) */\n");
        }
        foreach (var p in autoProps.Where(p => !p.IsStatic))
            hs.Append($"    {PropCpp(p)} {Id(p.Name)}_;   /* {p.ToDisplayString()} (auto-property) */\n");
        hs.Append("};\ninline Self* S(void* o) { return static_cast<Self*>(o); }\n");
        var statics = type.GetMembers().OfType<IFieldSymbol>().Where(f => f.IsStatic && !f.IsConst && f.AssociatedSymbol == null).ToList();
        hs.Append("/* the class's statics: one uncollectable GC block made with the class (scanned by the GC, inside the rolled-back heap) */\nstruct Statics {\n");
        foreach (var f in statics) hs.Append($"    {Cpp(f.Type)} {Id(f.Name)};   /* {f.ToDisplayString()} */\n");
        foreach (var p in autoProps.Where(p => p.IsStatic)) hs.Append($"    {PropCpp(p)} {Id(p.Name)}_;   /* {p.ToDisplayString()} (auto-property) */\n");
        if (statics.Count == 0 && !autoProps.Any(p => p.IsStatic)) hs.Append("    int unused_;\n");
        hs.Append("};\nextern Statics* g_s;\n");
        // virtual slots of ours
        var nv = NewVirtuals();
        hs.Append($"constexpr uint32_t kVtableBytes = {BaseVtableBytes(bt)} + {8 * nv.Count};\n");
        for (int i = 0; i < nv.Count; i++) hs.Append($"constexpr uint32_t kSlot_{nv[i].Name} = {BaseVtableBytes(bt)} + {8 * i};\n");
        hs.Append("/* the class (runtime type, descriptor, vtable) and its statics: C#'s class constructor. Made on first use; call it where no\n" +
                  " * rollback window is open (a menu): the type record and statics are GC allocations. */\nbool EnsureClass();\n" +
                  "gen::Class& ClassOf();\nvoid* RuntimeType();   /* null before EnsureClass */\nuintptr_t TypeGetter();   /* a function returning RuntimeType() (BRUTE's base getter) */\n");
        h.Insert(0, hs.ToString());
        c.Append("gen::Class g_class;\nStatics* g_s = nullptr;\nstatic bool g_tried = false;\n");
        c.Append("gen::Class& ClassOf() { return g_class; }\nvoid* RuntimeType() { return g_class.type; }\nstatic void* TypeGetterFn() { return g_class.type; }\n" +
                 "uintptr_t TypeGetter() { return reinterpret_cast<uintptr_t>(&TypeGetterFn); }\n\n");
        foreach (var f in statics.Where(f => f.DeclaredAccessibility == Accessibility.Public))
        {
            h.Append($"{Cpp(f.Type)} Get_{f.Name}();\nvoid Set_{f.Name}({Cpp(f.Type)} v);\n");
            c.Append($"{Cpp(f.Type)} Get_{f.Name}() {{ return EnsureClass() ? g_s->{Id(f.Name)} : {Literal(null, f.Type)}; }}\n");
            c.Append($"void Set_{f.Name}({Cpp(f.Type)} v) {{ if (EnsureClass()) g_s->{Id(f.Name)} = v; }}\n");
        }
        // auto-properties of instances / statics: accessors over the storage
        foreach (var p in autoProps)
        {
            var store = p.IsStatic ? $"g_s->{Id(p.Name)}_" : $"S(self)->{Id(p.Name)}_";
            var selfP = p.IsStatic ? "" : "void* self";
            h.Append($"{PropCpp(p)} get_{p.Name}({selfP});\nvoid set_{p.Name}({(p.IsStatic ? "" : "void* self, ")}{PropCpp(p)} v);\n");
            c.Append($"{PropCpp(p)} get_{p.Name}({selfP}) {{ {(p.IsStatic ? "EnsureClass(); " : "")}return {store}; }}\n");
            c.Append($"void set_{p.Name}({(p.IsStatic ? "" : "void* self, ")}{PropCpp(p)} v) {{ {(p.IsStatic ? "EnsureClass(); " : "")}{store} = v; }}\n");
        }
    }

    const int ModelBoxes = 4;

    /* a model-collection field of a class of ours (ICollection<Lobby> _lobbies): the model list type it is assigned from (the models'
     * capacity), held by value; null if none is found */
    // an ICollection<string> auto-property of a TF.EX class (NetplayConfig.Players): the game's List<string> object, as a parameter of
    // that type is (IsStringCollectionGuess is about lobby models' string lists)
    string PropCpp(IPropertySymbol p) => IsStringCollectionGuess(p.Type) ? "void*" : Cpp(p.Type);

    string ModelCollFieldCpp(IFieldSymbol f)
    {
        if (!IsModelCollection(f.Type)) return null;
        foreach (var r in type.DeclaringSyntaxReferences)
        {
            var sm = ws.Model(r.SyntaxTree);
            foreach (var asg in r.GetSyntax().DescendantNodes().OfType<Microsoft.CodeAnalysis.CSharp.Syntax.AssignmentExpressionSyntax>())
                if (sm.GetOperation(asg) is ISimpleAssignmentOperation a && a.Target is IFieldReferenceOperation { Field: var tf } && SymbolEqualityComparer.Default.Equals(tf, f))
                {
                    var v = Strip(a.Value);
                    while (v is IConversionOperation c) v = c.Operand;
                    // `data?.Entries ?? []` (TF.EX's LeaderboardPanel): the model list the value comes from
                    if (v is ICoalesceOperation co) { v = Strip(co.Value); while (v is IConversionOperation c2) v = c2.Operand; }
                    if (v is IConditionalAccessOperation ca) v = Strip(ca.WhenNotNull);
                    if (v is IPropertyReferenceOperation mp && gen.Models.IsModel(mp.Property.ContainingType) && gen.Models.MemberOf(mp.Property.ContainingType, mp.Property.Name) is { } mem && mem.CppType.StartsWith("List<"))
                    {
                        includes.Add("tfexgen_Models.hpp");
                        return "tfex::gen::" + mem.CppType;   /* List<ws::Lobby, N>: gen's List of ws's model */
                    }
                }
        }
        return null;
    }

    /* an assignment to a model / model-collection field of this class: into its own storage; null for any other field */
    string ModelFieldAssign(IFieldSymbol f, string obj, string value)
    {
        if (f.IsStatic || !isClass || !SymbolEqualityComparer.Default.Equals(f.ContainingType, type)) return null;
        var acc = $"S({obj})->{Id(f.Name)}";
        if (ModelCollFieldCpp(f) != null) { R.Add("rule: a model field of a class of ours owns its storage (copied in)", f.Name); return $"gen::SetModelColl({acc}, {value})"; }
        if (gen.Models.IsModel(f.Type)) { R.Add("rule: a model field of a class of ours owns its storage (copied in)", f.Name); return $"({acc} = gen::KeepModel({acc}_box_, {acc}_next_, {value}))"; }
        return null;
    }

    /* C# 12 primary constructors (class C(int x) { ... }): a parameter a member reads (not only an initializer) is captured into a
     * compiler field <x>P, which the class layout already has; the constructor stores it, the members read it */
    static bool IsPrimaryCtor(ISymbol s) => s is IMethodSymbol { MethodKind: MethodKind.Constructor } pc &&
        pc.DeclaringSyntaxReferences.FirstOrDefault()?.GetSyntax() is Microsoft.CodeAnalysis.CSharp.Syntax.TypeDeclarationSyntax;
    bool IsCapturedPrimaryParam(IParameterSymbol p)
    {
        if (!IsPrimaryCtor(p.ContainingSymbol)) return false;
        var td = (Microsoft.CodeAnalysis.CSharp.Syntax.TypeDeclarationSyntax)p.ContainingSymbol.DeclaringSyntaxReferences[0].GetSyntax();
        var model = M(td);
        foreach (var member in td.Members)
        {
            if (member is Microsoft.CodeAnalysis.CSharp.Syntax.FieldDeclarationSyntax) continue;   // (an initializer reads the parameter itself)
            if (member is Microsoft.CodeAnalysis.CSharp.Syntax.PropertyDeclarationSyntax pd && pd.AccessorList == null && pd.ExpressionBody == null) continue;
            foreach (var id in member.DescendantNodes().OfType<Microsoft.CodeAnalysis.CSharp.Syntax.IdentifierNameSyntax>())
                if (id.Identifier.Text == p.Name && SymbolEqualityComparer.Default.Equals(model.GetSymbolInfo(id).Symbol, p)) return true;
        }
        return false;
    }
    string PrimaryParamField(IParameterSymbol p) => Id($"<{p.Name}>P");

    /* C#: field initializers, then the base constructor (or this(...)), then the body. Without an explicit base call: the base's
     * parameterless one. */
    void CtorPrologue(StringBuilder sb, IOperation initializer)
    {
        if (IsPrimaryCtor(current))
            foreach (var pp in ((IMethodSymbol)current).Parameters.Where(IsCapturedPrimaryParam))
                sb.Append($"    S(self)->{PrimaryParamField(pp)} = {Id(pp.Name)};   /* the primary constructor's {pp.Name}, captured */\n");
        bool chainsThis = initializer is IExpressionStatementOperation { Operation: IInvocationOperation { TargetMethod: var tm } } && SymbolEqualityComparer.Default.Equals(tm.ContainingType, type);
        if (!chainsThis)
            foreach (var f in type.GetMembers().OfType<IFieldSymbol>().Where(f => !f.IsStatic && !f.IsConst))
            {
                var v = (f.DeclaringSyntaxReferences.FirstOrDefault()?.GetSyntax() as Microsoft.CodeAnalysis.CSharp.Syntax.VariableDeclaratorSyntax)?.Initializer?.Value;
                var target = HostedInstanceField(f) ? $"{Id(f.Name)}()" : $"S(self)->{Id(f.Name)}";
                if (f.AssociatedSymbol is IPropertySymbol ap)
                {
                    target = $"S(self)->{Id(ap.Name)}_";
                    v = (ap.DeclaringSyntaxReferences[0].GetSyntax() as Microsoft.CodeAnalysis.CSharp.Syntax.PropertyDeclarationSyntax)?.Initializer?.Value;
                }
                if (v == null) continue;
                // a field the Switch provides elsewhere (overrides.toml [service] with `set`; the socket edge's WebSocket, queues): its rule
                if (gen.ServiceRule(f) is TomlTable fr && fr.TryGetValue("set", out var fs) && fs is string fst)
                {
                    sb.Append($"    {(fst.Contains("$value") ? fst.Replace("$value", Expr(InitOp(v))) : fst)};   /* {f.Name} initializer: [service] */\n");
                    continue;
                }
                // `List<Model> x = [];`: a model list held by value starts empty in a new object (zeroed)
                if (f.AssociatedSymbol == null && ModelCollFieldCpp(f) != null && Strip(InitOp(v)) is ICollectionExpressionOperation { Elements.Length: 0 })
                { sb.Append($"    /* {f.Name} initializer: [] (a new object's list is empty) */\n"); continue; }
                if (f.AssociatedSymbol == null && ModelFieldAssign(f, "self", Expr(InitOp(v))) is string mfa) { sb.Append($"    {mfa};   /* {f.Name} initializer */\n"); continue; }
                sb.Append($"    {target} = {Expr(InitOp(v))};   /* {f.Name} initializer */\n");
            }
        var bt = type.BaseType;
        if (initializer is IExpressionStatementOperation ie && ie.Operation is IInvocationOperation baseCtor)
        {
            var args = baseCtor.Arguments.OrderBy(a => a.Parameter.Ordinal).Select(a => Expr(a.Value)).ToList();
            if (chainsThis) sb.Append($"    Ctor{CtorSuffix(baseCtor.TargetMethod)}({string.Join(", ", new[] { "self" }.Concat(args))});   /* : this(...) */\n");
            else if (Kind(bt) == BaseKind.Ours) sb.Append($"    {BaseUnit(bt)}::Ctor{CtorSuffix(baseCtor.TargetMethod)}({string.Join(", ", new[] { "self" }.Concat(args))});   /* : base(...) */\n");
            // a hosted unit (no object of ours: Teams+'s ThreeTeamModule over FortRise.Mod): its FortRise base has no Switch object either
            else if (!isClass && bt?.ContainingNamespace?.ToDisplayString() == "FortRise") sb.Append("    /* : base(...): FortRise's Mod, no object on the Switch (hosted) */\n");
            else if (Kind(bt) == BaseKind.Game) sb.Append("    ").Append(Call(baseCtor.TargetMethod, "self", args, baseCtor, true)).Append(";   /* : base(...) */\n");
        }
        else if (Kind(bt) == BaseKind.Ours) sb.Append($"    {BaseUnit(bt)}::Ctor(self);   /* : base() */\n");
        else if (Kind(bt) == BaseKind.Game)
        {
            var dc = bt.InstanceConstructors.FirstOrDefault(c => c.Parameters.Length == 0);
            if (dc != null) sb.Append("    ").Append(Call(dc, "self", new List<string>(), null, true)).Append(";   /* : base() */\n");
            else { R.Add("unsupported: game base without a parameterless constructor", type.Name); methodBlockers.Add("game base without a parameterless constructor"); sb.Append("    TFEXGEN_UNSUPPORTED;\n"); }
        }
    }

    /* New(args) = allocate + Ctor(self, args), per constructor; a class without one gets C#'s implicit parameterless one */
    void ClassConstructors(StringBuilder h, StringBuilder body)
    {
        foreach (var c in type.InstanceConstructors.Where(c => !c.IsImplicitlyDeclared || c.Parameters.Length == 0))   // not a record's copy constructor
        {
            var ps = c.Parameters.Select(p => $"{Cpp(p.Type)} {Id(p.Name)}").ToList();
            var call = string.Join(", ", new[] { "self" }.Concat(c.Parameters.Select(p => Id(p.Name))));
            if (c.IsImplicitlyDeclared)
            {
                var sb = new StringBuilder();
                current = c;
                CtorPrologue(sb, null);
                current = null;
                h.Append("void Ctor(void* self);\n");
                body.Append($"/*@M {type.Name}..ctor*/\nvoid Ctor(void* self) {{\n{sb}}}\n\n");
            }
            if (type.IsAbstract) continue;
            // a members unit of a class that doesn't list its constructor (TFCommands' scenario launch: static members only): no New()
            if (!c.IsImplicitlyDeclared && unit.ContainsKey("members") && !Config.Strings(unit, "members").Contains(".ctor")) continue;
            var cs = CtorSuffix(c);
            h.Append($"void* New{cs}({string.Join(", ", ps)});\n");
            body.Append($"void* New{cs}({string.Join(", ", ps)}) {{\n    if (!EnsureClass()) return nullptr;\n    void* self = gen::New(g_class);   /* newobj: the game's allocator, our runtime type */\n" +
                        $"    if (!self) return nullptr;\n    Ctor{cs}({call});\n    return self;\n}}\n\n");
        }
    }

    void ClassEnsure(StringBuilder h, StringBuilder body)
    {
        var bt = type.BaseType;
        var kind = Kind(bt);
        string U16(string s) => "u\"" + s.Replace("\"", "\\\"") + "\"";
        var full = type.ToDisplayString();
        var ns = type.ContainingNamespace.ToDisplayString();
        var parts = file.Split('/');
        var asm = parts.Length > 2 && parts[1].StartsWith("TF.") ? parts[1] : type.ContainingAssembly.Name;   // TF.EX's project = its assembly
        var aq = $"{full}, {asm}";
        uint hash = 2166136261; foreach (var ch in full) { hash ^= ch; hash *= 16777619; }
        // objects of the class are scanned by the GC when a field can hold a managed reference (a struct field holding one too), or the base's do
        bool refs = type.GetMembers().OfType<IFieldSymbol>().Any(f => !f.IsStatic && (!f.Type.IsValueType || HoldsRef(f.Type))) || kind != BaseKind.Object;
        body.Append($"bool EnsureClass() {{\n    if (g_s) return true;\n    if (g_tried) return false;\n    g_tried = true;\n");
        body.Append($"    static const char16_t kName[] = {U16(type.Name)}, kNs[] = {U16(ns)}, kFull[] = {U16(full)}, kAq[] = {U16(aq)};\n");
        if (kind == BaseKind.Ours)
        {
            var bu = BaseUnit(bt);
            body.Append($"    if (!{bu}::EnsureClass()) return false;\n");
            body.Append($"    if (!gen::MakeClass(g_class, {bu}::ClassOf().type, {bu}::ClassOf().desc, {bu}::TypeGetter(), {bu}::ClassOf().vtableBytes, kVtableBytes,\n" +
                        $"                        (sizeof(Self) + 7) & ~7u, {(refs ? "true" : "false")}, kName, kNs, kFull, kAq, 0x{hash:x8}u)) return false;\n");
        }
        else
        {
            var bn = kind == BaseKind.Game ? SwitchName(bt) : "System.Object";
            body.Append($"    if (!gen::MakeClass(g_class, gen::TypeOf({B.Need("t", bn)}), reinterpret_cast<const uintptr_t*>(gen::Base() + {B.Need("d", bn)}), gen::Base() + {B.Need("t", bn)},\n" +
                        $"                        {B.Need("vb", bn)}, kVtableBytes, (sizeof(Self) + 7) & ~7u, {(refs ? "true" : "false")}, kName, kNs, kFull, kAq, 0x{hash:x8}u)) return false;\n");
        }
        // overrides: of a game virtual (when the base is that game class: its slot must hold the body we call for base.X()), or of ours
        foreach (var (m, fn, slot) in overrides)
        {
            if (slot.StartsWith("bind::") && kind == BaseKind.Game && m.OverriddenMethod.IsAbstract)   // an abstract slot: null in the base's vtable
                body.Append($"    if ((g_class.vtable[{slot} / 8] & 0x7fffffffffffffffull) != 0) return false;   /* {m.OverriddenMethod.ToDisplayString()}: abstract */\n");
            else if (slot.StartsWith("bind::") && kind == BaseKind.Game && SymbolEqualityComparer.Default.Equals(m.OverriddenMethod.ContainingType, bt))
            {
                var baseBody = B.Need("m", SwitchName(m.OverriddenMethod.ContainingType), m.OverriddenMethod.Name, IlSig(m.OverriddenMethod));
                body.Append($"    if ((g_class.vtable[{slot} / 8] & 0x7fffffffffffffffull) != gen::Base() + {baseBody}) return false;   /* {m.OverriddenMethod.ToDisplayString()} */\n");
            }
            body.Append($"    gen::Override(g_class, {slot}, reinterpret_cast<void*>({(fn.StartsWith("static_cast<") ? fn : "&" + fn)}));\n");
        }
        foreach (var v in NewVirtuals().Where(v => !v.IsAbstract))
            body.Append($"    gen::Override(g_class, kSlot_{v.Name}, reinterpret_cast<void*>(&{Id(v.Name)}));\n");
        body.Append("    g_s = static_cast<Statics*>(gen::Fn<void* (*)(uint64_t)>(game::brute::MallocUncollectable)(sizeof(Statics)));\n    if (!g_s) return false;\n    std::memset(g_s, 0, sizeof *g_s);\n");
        foreach (var f in type.GetMembers().OfType<IFieldSymbol>().Where(f => f.IsStatic && !f.IsConst && !(gen.AbsentRule(f) is { } ar0 && ar0.ContainsKey("absent"))))
        {
            var v = (f.DeclaringSyntaxReferences.FirstOrDefault()?.GetSyntax() as Microsoft.CodeAnalysis.CSharp.Syntax.VariableDeclaratorSyntax)?.Initializer?.Value;
            var target = $"g_s->{Id(f.Name)}";
            if (f.AssociatedSymbol is IPropertySymbol ap)
            {
                target = $"g_s->{Id(ap.Name)}_";
                v = (ap.DeclaringSyntaxReferences[0].GetSyntax() as Microsoft.CodeAnalysis.CSharp.Syntax.PropertyDeclarationSyntax)?.Initializer?.Value;
            }
            if (v == null) continue;
            current = null;
            body.Append($"    {target} = {Expr(InitOp(v))};   /* {f.ToDisplayString()} */\n");
        }
        body.Append("    return true;\n}\n\n");
    }

    /* a field / property initializer's operation, through C#'s null-forgiving `x!` (`= null!`: Roslyn has no operation for the `!` node) */
    IOperation InitOp(Microsoft.CodeAnalysis.CSharp.Syntax.ExpressionSyntax v)
    {
        while (v is Microsoft.CodeAnalysis.CSharp.Syntax.PostfixUnaryExpressionSyntax pu && pu.IsKind(Microsoft.CodeAnalysis.CSharp.SyntaxKind.SuppressNullableWarningExpression)) v = pu.Operand;
        return M(v).GetOperation(v);
    }

    /* an override: a game virtual's slot (bound), or a virtual of a class of ours (its kSlot_) */
    void NoteOverride(IMethodSymbol m)
    {
        if (!isClass || !m.IsOverride || m.OverriddenMethod == null || m.IsImplicitlyDeclared) return;
        var root = m.OverriddenMethod;
        while (root.IsOverride && root.OverriddenMethod != null) root = root.OverriddenMethod;   // the method that made the slot
        string slot;
        // a FortRise virtual (ModuleSettings.Create: NetplaySettings' settings page): no FortRise on the Switch, nothing calls it through a
        // slot; the method stays a plain function (tfex/NetplayOptions.cpp calls NetplaySettings::Create)
        if (root.ContainingType.ContainingNamespace?.ToDisplayString() == "FortRise")
        {
            R.Add("mechanical: override of a FortRise virtual -> no slot (no FortRise on the Switch)", $"{type.Name}.{m.Name}");
            return;
        }
        if (InBinary(root.ContainingType) || (CorlibBase(root.ContainingType) && Kind(type.BaseType) == BaseKind.Game))
        {
            slot = B.Need("v", SwitchName(root.ContainingType), root.Name, IlSig(root));
            // a PC-only virtual (PlayerInput.MenuSaveReplay, Icon ...: no such member on the Switch): the Switch's game never calls it,
            // so the override needs no slot; the method stays a plain function (FakeController)
            if (B.ErrorOf(slot[6..]) is string verr && verr != "not bound" && (verr.Contains("not found") || verr.Contains("no subclass overrides")))
            {
                methodKeys.Remove(slot[6..]);
                R.Add("mechanical: override of a virtual the Switch's game lacks -> no slot (never called by the game)", $"{type.Name}.{m.Name}");
                return;
            }
        }
        else if (OriginOf(root.ContainingType) == Origin.Tfex) slot = $"{BaseUnit(root.ContainingType)}::kSlot_{root.Name}";
        else return;   // System.Object's (ToString...): not used by the game
        if (!overrides.Any(o => SymbolEqualityComparer.Default.Equals(o.m, m))) overrides.Add((m, OverrideFn(m), slot));
    }
    /* the function an override's slot holds: its name, or (overloads of one name: DeterministicRandom's Next(), Next(int), Next(int, int))
     * the address of that overload, cast to its own signature */
    string OverrideFn(IMethodSymbol m)
    {
        var n = MName(m);
        if (type.GetMembers(m.Name).OfType<IMethodSymbol>().Count(x => x.MethodKind == m.MethodKind && MName(x) == n) < 2) return n;
        var sig = Signature(m, header: false);
        int open = sig.IndexOf(" " + n + "(", StringComparison.Ordinal);
        var ret = sig[..open].Trim();
        var inner = sig[(open + n.Length + 2)..^1];
        var ps = new List<string>(); int depth = 0, from = 0;
        for (int i = 0; i <= inner.Length; i++)
        {
            if (i == inner.Length || (inner[i] == ',' && depth == 0)) { var p0 = inner[from..i].Trim(); if (p0.Length > 0) ps.Add(System.Text.RegularExpressions.Regex.Replace(p0, @"\s*\b[A-Za-z_][A-Za-z0-9_]*$", "")); from = i + 1; }
            else if (inner[i] is '<' or '(') depth++;
            else if (inner[i] is '>' or ')') depth--;
        }
        return $"static_cast<{ret} (*)({string.Join(", ", ps)})>(&{n})";
    }

    /* ---------------------------------------------------------------- hand rules (glue) ---------------------------------------------------------------- */
    IEnumerable<TomlTable> Rules(string kind) => cfg.Root.TryGetValue(kind, out var v) && v is TomlTableArray a ? a : Enumerable.Empty<TomlTable>();

    /* [[replace]] member = "<C# member>", to = "<glue static method>", key = "<S-key>": a ledger difference, the member's body is the glue's */
    string Replacement(IMethodSymbol m)
    {
        var id = m.ContainingType.ToDisplayString() + "." + m.Name;
        var rule = Rules("replace").FirstOrDefault(r => (string)r["member"] == id);
        if (rule == null) return null;
        var to = (string)rule["to"];
        R.Add($"hand rule: ledger replacement ({rule["key"]})", $"{id} -> {to}");
        var args = m.Parameters.Select(p => Id(p.Name)).ToList();
        // a generic method's copy (TF.State's GetAll<T>...) whose glue takes one more parameter per type parameter (a System.Type): each type
        // argument's runtime type after the parameters
        var gdot = to.LastIndexOf('.');
        if (m.IsGenericMethod && subst.Count > 0 &&
            ws.Compilation.GetTypeByMetadataName(to[..gdot])?.GetMembers(to[(gdot + 1)..]).OfType<IMethodSymbol>().FirstOrDefault() is IMethodSymbol gm &&
            gm.Parameters.Length == m.Parameters.Length + m.TypeParameters.Length)
            args.AddRange(m.TypeParameters.Select(tp => TypeOf(Sub(tp))));
        // an instance member whose glue takes one parameter more (FortRise's [MonoModLinkTo] base calls): the object first
        if (!m.IsStatic && ws.Compilation.GetTypeByMetadataName(to[..gdot])?.GetMembers(to[(gdot + 1)..]).OfType<IMethodSymbol>().FirstOrDefault() is IMethodSymbol sm &&
            sm.Parameters.Length == m.Parameters.Length + 1)
            args.Insert(0, "self");
        var call = GlueCall(to, args, null);
        return m.ReturnsVoid ? $"    {call};\n" : $"    return {call};\n";
    }

    /* a static method of a glue type, called through the glue's unit */
    string GlueCall(string glueMethod, List<string> args, IOperation at)
    {
        var dot = glueMethod.LastIndexOf('.');
        var t = ws.Compilation.GetTypeByMetadataName(glueMethod[..dot]) ?? gen.MainType(glueMethod[..dot]);   // (a mod's unit: TF.EX's glue)
        if (t?.GetMembers(glueMethod[(dot + 1)..]).OfType<IMethodSymbol>().FirstOrDefault() is { IsExtern: true } ext && CppAttr(ext, args) is string direct)
            return direct;   // a [Cpp] extern: the Switch's C++ itself
        var unitOf = t == null ? null : OtherUnit(t);
        if (unitOf == null) return at != null ? Unsupported(at, "glue " + glueMethod + " has no unit") : "TFEXGEN_UNSUPPORTED_EXPR()";
        var un = (string)unitOf["name"];
        includes.Add($"tfexgen_{un}.hpp");
        return $"tfex::gen::{un}::{glueMethod[(dot + 1)..]}({string.Join(", ", args)})";
    }

    /* [[rewrite]] with skip = true: the call does nothing on the Switch (a void method; `why` says what stands in) */
    string SkipRewrite(TomlTable rule, IMethodSymbol m)
    {
        if (!rule.TryGetValue("skip", out var sv) || sv is not true || !m.ReturnsVoid) return null;
        R.Add("hand rule: call skipped (translate.toml [[rewrite]] skip)", $"{m.OriginalDefinition.ToDisplayString()}: {rule["why"]}");
        return "(void)0";
    }
    /* a [[rewrite]] skip rule for the method a DynamicData call resolves to (DynamicData.For(x).Invoke("Name")), in this class */
    string DynamicSkip(IMethodSymbol target, IOperation at)
    {
        var where = at.SemanticModel?.GetEnclosingSymbol(at.Syntax.SpanStart)?.ContainingType?.ToDisplayString();
        var id = target.OriginalDefinition.ToDisplayString();
        var rule = Rules("rewrite").FirstOrDefault(r => r.ContainsKey("call") && (string)r["call"] == id && (!r.ContainsKey("in") || (string)r["in"] == where));
        return rule != null ? SkipRewrite(rule, target) : null;
    }
    bool HasRewrite(IInvocationOperation inv) => Rules("rewrite").Any(r => r.ContainsKey("call") &&
        ((string)r["call"] == inv.TargetMethod.ToDisplayString() || (string)r["call"] == inv.TargetMethod.OriginalDefinition.ToDisplayString()));

    /* [[rewrite]] call = "<C# method, as Roslyn displays its definition>", to = "<glue static method (receiver first)>" */
    string Rewrite(IInvocationOperation inv, List<string> args)
    {
        var id = inv.TargetMethod.OriginalDefinition.ToDisplayString();
        var cid = inv.TargetMethod.ToDisplayString();   // the constructed one: a rule per instantiation (Calc.ChildEnum<TowerFall.TFGame.Genders>)
        var where = inv.SemanticModel?.GetEnclosingSymbol(inv.Syntax.SpanStart)?.ContainingType?.ToDisplayString();
        var rule = Rules("rewrite").FirstOrDefault(r => r.ContainsKey("call") && (string)r["call"] == cid && (!r.ContainsKey("in") || (string)r["in"] == where))
                ?? Rules("rewrite").FirstOrDefault(r => r.ContainsKey("call") && (string)r["call"] == id && (!r.ContainsKey("in") || (string)r["in"] == where));
        if (rule == null) return null;
        if (SkipRewrite(rule, inv.TargetMethod) is string sk) return sk;
        R.Add("hand rule: call rewritten to glue (translate.toml [[rewrite]])", $"{id} -> {rule["to"]}: {rule["why"]}");
        var all = new List<string>(args);
        if (!inv.TargetMethod.IsStatic) all.Insert(0, Recv(inv.Instance));
        // a glue method with fewer parameters than the call has arguments: the trailing defaulted ones aren't passed on (MessagePack's
        // options, a CancellationToken)
        var to = (string)rule["to"]; var dot = to.LastIndexOf('.');
        var glueType = ws.Compilation.GetTypeByMetadataName(to[..dot]) ?? gen.MainType(to[..dot]);   // (a mod's unit: TF.EX's glue)
        if (glueType?.GetMembers(to[(dot + 1)..]).OfType<IMethodSymbol>().FirstOrDefault() is IMethodSymbol gm && gm.Parameters.Length < all.Count)
        {
            var ordered = inv.Arguments.OrderBy(a => a.Parameter.Ordinal).ToList();
            int offset = inv.TargetMethod.IsStatic ? 0 : 1;
            while (all.Count > gm.Parameters.Length && all.Count - 1 - offset >= 0 && ordered[all.Count - 1 - offset].ArgumentKind == ArgumentKind.DefaultValue) all.RemoveAt(all.Count - 1);
        }
        var glue = glueType?.GetMembers(to[(dot + 1)..]).OfType<IMethodSymbol>().FirstOrDefault();
        if (glue is { IsGenericMethod: true } && inv.TargetMethod.TypeArguments.Length == glue.TypeParameters.Length)
        {   // a generic glue: its copy for the call's type arguments (the glue unit emits it, as TF.State's generic methods); a mod's
            // type arguments as TF.EX's compilation knows them (one copy per type, whichever side asks)
            var foreignGlue = ws.Compilation.GetTypeByMetadataName(to[..dot]) == null;   // the glue is TF.EX's, the caller a mod's unit
            var cta = inv.TargetMethod.TypeArguments.Select(Sub).Select(a => foreignGlue ? gen.ToMain(a) : a).ToArray();
            if (cta.All(a => a is not ITypeParameterSymbol) && OtherUnit(glue.ContainingType) is TomlTable gu)
            {
                gen.RequestMethodInst(gu, glue, cta);
                includes.Add($"tfexgen_{gu["name"]}.hpp");
                return $"tfex::gen::{gu["name"]}::{glue.Name}{MethodInstSuffix(cta)}({string.Join(", ", all)})";
            }
        }
        else if (glue != null && !glue.IsGenericMethod)
        {   // a rule written for one instantiation must not take another (Calc.Shuffle's List<MapButton> glue took TF.State's List<int>)
            var callTypes = (inv.TargetMethod.IsStatic ? Enumerable.Empty<ITypeSymbol>() : new[] { inv.TargetMethod.ContainingType as ITypeSymbol })
                            .Concat(inv.TargetMethod.Parameters.Select(p => p.Type)).ToList();
            for (int k = 0; k < Math.Min(callTypes.Count, glue.Parameters.Length); k++)
            {
                var ct = Sub(callTypes[k]); var gt = glue.Parameters[k].Type;
                if (ct is INamedTypeSymbol { IsGenericType: true } cn && gt is INamedTypeSymbol { IsGenericType: true } gn &&
                    SymbolEqualityComparer.Default.Equals(cn.OriginalDefinition, gn.OriginalDefinition) && !SymbolEqualityComparer.Default.Equals(cn, gn))
                    return Unsupported(inv, $"[[rewrite]] {to} takes {gt.ToDisplayString()}, this call passes {ct.ToDisplayString()}: make the glue generic");
            }
        }
        return GlueCall((string)rule["to"], all, inv);
    }

    /* [[rewrite]] new = "<constructor, as Roslyn displays it>", in = "<containing type>" (optional), to = "<glue static method>", key = ledger key:
     * an object creation replaced by a glue call with the same arguments (Notification's `new Canvas(w, h)`: TFEX-DIFF S-rollback) */
    bool skipRewriteNew; string newFallback;
    int finallyN;   /* names of the finally scope guards (UnitEmitter.cs ITryOperation) */
    /* a try whose body calls a throwing stand-in ([service] rule `throws = true`): the catch labels in force (innermost last); a statement
     * that called one is followed by a check of the thread's fault (gen::Faulted) that jumps to the catch (UnitEmitter.cs ITryOperation) */
    readonly List<string> catchLabels = new();
    int catchN;
    bool stmtThrows;   /* the statement being emitted called a throwing stand-in */
    bool stmtYields;   /* ... or a stand-in that may have nothing yet ([service] rule yields = true: the pass returns, the pump calls again) */
    string RewriteNew(IObjectCreationOperation oc, List<string> args)
    {
        if (oc.Constructor == null || skipRewriteNew) return null;
        var id = oc.Constructor.OriginalDefinition.ToDisplayString();
        var where = oc.Constructor == null ? null : (oc.SemanticModel?.GetEnclosingSymbol(oc.Syntax.SpanStart)?.ContainingType?.ToDisplayString());
        var rule = Rules("rewrite").FirstOrDefault(r => r.ContainsKey("new") && (string)r["new"] == id && (!r.ContainsKey("in") || (string)r["in"] == where));
        if (rule == null) return null;
        R.Add("hand rule: creation rewritten to glue (translate.toml [[rewrite]] new)", $"{id} in {where} -> {rule["to"]}{(rule.ContainsKey("key") ? " (" + rule["key"] + ")" : "")}: {rule["why"]}");
        // $new in a [Cpp] glue: the creation itself (a fallback), translated only when the glue uses it (else it would pull in the class:
        // FortRise's patch_ArcherData)
        var to = (string)rule["to"]; var dot = to.LastIndexOf('.');
        var gm = ws.Compilation.GetTypeByMetadataName(to[..dot])?.GetMembers(to[(dot + 1)..]).OfType<IMethodSymbol>().FirstOrDefault();
        var cppText = gm?.GetAttributes().FirstOrDefault(x => x.AttributeClass?.Name is "CppAttribute" or "Cpp") is { ConstructorArguments.Length: > 0 } ca ? ca.ConstructorArguments[0].Value as string : null;
        if (cppText?.Contains("$new") == true) { skipRewriteNew = true; newFallback = New(oc); skipRewriteNew = false; }
        try { return GlueCall((string)rule["to"], args, oc); } finally { newFallback = null; }
    }

    /* glue: `[Cpp("expr with $0, $1...", Include = "...")] static extern T F(...)`: the C++ the Switch already has */
    string CppAttr(IMethodSymbol m, List<string> args)
    {
        var a = m.GetAttributes().FirstOrDefault(x => x.AttributeClass?.Name is "CppAttribute" or "Cpp");
        if (a == null) return null;
        var text = (string)a.ConstructorArguments[0].Value;
        var inc = a.NamedArguments.FirstOrDefault(n => n.Key == "Include").Value.Value as string;
        if (inc != null) includes.Add(inc);
        if (newFallback != null) text = text.Replace("$new", newFallback);
        for (int i = args.Count - 1; i >= 0; i--) text = text.Replace("$" + i, args[i]);
        return text;
    }

    /* ---------------------------------------------------------------- patterns ---------------------------------------------------------------- */
    void HoistPatternLocals(IOperation op, StringBuilder sb, string pad)
    {
        // (var a, var b) = ...: the deconstruction declares its targets; only patterns / out vars in the value are hoisted
        if (op is IExpressionStatementOperation { Operation: IDeconstructionAssignmentOperation des }) op = des;
        if (op is IDeconstructionAssignmentOperation da) op = da.Value;
        // foreach ((var a, var b) in xs): the loop declares its variables per element (ForEach); only the collection's out vars / patterns
        if (op is IForEachLoopOperation fe) op = fe.Collection;
        foreach (var d in Descend(op).OfType<IDeclarationPatternOperation>())
            if (d.DeclaredSymbol is ILocalSymbol l) sb.Append(pad).Append($"{Cpp(l.Type)} {Id(l.Name)} = {Literal(null, l.Type)};\n");
        foreach (var d in Descend(op).OfType<IRecursivePatternOperation>())   // x is { } f
            if (d.DeclaredSymbol is ILocalSymbol l) sb.Append(pad).Append($"{Cpp(l.Type)} {Id(l.Name)} = {Literal(null, l.Type)};\n");
        foreach (var d in Descend(op).OfType<IDeclarationExpressionOperation>())   // out var x
            if (d.Expression is ILocalReferenceOperation lr) sb.Append(pad).Append($"{Cpp(lr.Local.Type)} {Id(lr.Local.Name)} = {Literal(null, lr.Local.Type)};\n");
    }
    static IEnumerable<IOperation> Descend(IOperation op)
    {
        yield return op;
        foreach (var c in op.ChildOperations)
        {
            if (c is IAnonymousFunctionOperation or IBlockOperation) continue;
            foreach (var d in Descend(c)) yield return d;
        }
    }

    string Pattern(string value, IPatternOperation p)
    {
        switch (p)
        {
            case IDeclarationPatternOperation d when d.DeclaredSymbol is ILocalSymbol l && !l.Type.IsReferenceType:
                // `x is int ms` on an int?: has a value, and it goes into ms
                return $"([&]() {{ auto v_ = {value}; if (!v_.has) return false; {Id(l.Name)} = v_.value; return true; }}())";
            case IDeclarationPatternOperation d when d.DeclaredSymbol is ILocalSymbol l && d.MatchedType?.ToDisplayString() == "System.Version":
                // `UpdateRequest.RequiredVersion is Version required` (TFGamePatch.HandleAutoUpdate): a packed int64 (UnitEmitter.Cpp), 0 = null
                R.Add("mechanical: System.Version's type pattern (is Version v) -> a packed int64 set (0 = null)", Where(p));
                return $"(({Id(l.Name)} = {value}) != 0)";
            case IDeclarationPatternOperation d when d.DeclaredSymbol is ILocalSymbol l && gen.Models.IsModel(d.MatchedType) &&
                                                      SymbolEqualityComparer.Default.Equals(d.InputType, d.MatchedType):
                // `OwnPlayer(...) is Player own` on a model of that same type (a plain struct pointer here, no runtime type): the null test
                R.Add("mechanical: a model's own-type pattern (is T x on a T) -> a null test", Where(p));
                return $"(({Id(l.Name)} = {value}) != nullptr)";
            case IDeclarationPatternOperation d when d.DeclaredSymbol is ILocalSymbol l:
                R.Add("mechanical: type pattern with a variable (is T x / case T x)", Where(p));
                return $"(({Id(l.Name)} = {AsType(value, d.MatchedType)}) != nullptr)";
            case IDeclarationPatternOperation d:
                return $"({AsType(value, d.MatchedType)} != nullptr)";
            case ITypePatternOperation t:
                return $"({AsType(value, t.MatchedType)} != nullptr)";
            case IConstantPatternOperation c when c.Value.ConstantValue is { HasValue: true, Value: null }:
                return $"({value} == nullptr)";
            case IConstantPatternOperation c:
                return $"({value} == {Expr(c.Value)})";
            case IRecursivePatternOperation rp when rp.PropertySubpatterns.Length == 0 && rp.DeconstructionSubpatterns.Length == 0 && rp.MatchedType is { IsReferenceType: true }:
                // `x is { } f` (FortRise): not null, and it goes into f
                R.Add("mechanical: an empty property pattern (is { } x) -> a null test", Where(p));
                return rp.DeclaredSymbol is ILocalSymbol rpl ? $"(({Id(rpl.Name)} = {value}) != nullptr)" : $"({value} != nullptr)";
            case IRecursivePatternOperation rp when rp.DeconstructionSubpatterns.Length == 0 && rp.MatchedType is IArrayTypeSymbol &&
                                                   rp.PropertySubpatterns.All(sp => sp.Member is IPropertyReferenceOperation { Property.Name: "Length" }):
            {   // `x is { Length: > 0 } s` (TF.EX's InputScripter): not null, each Length subpattern, then it goes into s
                R.Add("mechanical: an array's property pattern ({ Length: ... }) -> a null test and the length's patterns", Where(p));
                var conds = string.Concat(rp.PropertySubpatterns.Select(sp => $" if (!{Pattern("v_.len", sp.Pattern)}) return false;"));
                var assign = rp.DeclaredSymbol is ILocalSymbol al ? $" {Id(al.Name)} = v_;" : "";
                return $"([&]() {{ auto v_ = {value}; if (v_ == nullptr) return false;{conds}{assign} return true; }}())";
            }
            case IRecursivePatternOperation rp when rp.DeconstructionSubpatterns.Length == 0 && rp.MatchedType is INamedTypeSymbol { IsReferenceType: true } rpt &&
                                                   rp.PropertySubpatterns.All(sp => sp.Member is IPropertyReferenceOperation { Property.GetMethod: not null }):
            {   // `x is Player { Dead: true }` (TF.State's ArrowExtensions): the type test, then each property's pattern on the cast value
                R.Add("mechanical: a type's property pattern (is T { P: ... }) -> the type test and each property's pattern", Where(p));
                var conds = string.Concat(rp.PropertySubpatterns.Select(sp =>
                    $" if (!{Pattern(Call(((IPropertyReferenceOperation)sp.Member).Property.GetMethod, "v_", new List<string>(), sp), sp.Pattern)}) return false;"));
                var assign = rp.DeclaredSymbol is ILocalSymbol pl ? $" {Id(pl.Name)} = v_;" : "";
                return $"([&]() {{ void* v_ = {AsType(value, rpt)}; if (v_ == nullptr) return false;{conds}{assign} return true; }}())";
            }
            case INegatedPatternOperation n:
                return "!" + Pattern(value, n.Pattern);
            case IRelationalPatternOperation r:
                var rop = r.OperatorKind switch { BinaryOperatorKind.LessThan => "<", BinaryOperatorKind.LessThanOrEqual => "<=", BinaryOperatorKind.GreaterThan => ">",
                                                  BinaryOperatorKind.GreaterThanOrEqual => ">=", BinaryOperatorKind.Equals => "==", BinaryOperatorKind.NotEquals => "!=", _ => null };
                if (rop != null) return $"({value} {rop} {Expr(r.Value)})";
                break;
            case IBinaryPatternOperation bp:
                return $"({Pattern(value, bp.LeftPattern)} {(bp.OperatorKind == BinaryOperatorKind.Or ? "||" : "&&")} {Pattern(value, bp.RightPattern)})";
            case IDiscardPatternOperation:
                return "true";
        }
        return Unsupported(p, "pattern " + p.Kind);
    }

    /* x switch { p1 => a, p2 when g => b, _ => c }: a C++ lambda testing the arms in order (no arm: C# throws -> gen::Throw) */
    string SwitchExpr(ISwitchExpressionOperation se)
    {
        var v = $"sx_{tmp++}";
        var t = Cpp(se.Type);
        var sb = new StringBuilder($"([&]() {(t == "auto" ? "" : "-> " + t + " ")}{{ auto {v} = {Expr(se.Value)}; ");
        foreach (var d in se.Arms.SelectMany(a => Descend(a.Pattern)).OfType<IDeclarationPatternOperation>())
            if (d.DeclaredSymbol is ILocalSymbol l) sb.Append($"{Cpp(l.Type)} {Id(l.Name)} = {Literal(null, l.Type)}; ");
        foreach (var arm in se.Arms)
        {
            var test = Pattern(v, arm.Pattern);
            if (arm.Guard != null) test = $"{test} && ({Expr(arm.Guard)})";
            sb.Append(test == "true" ? $"return {Expr(arm.Value)}; " : $"if ({test}) return {Expr(arm.Value)}; ");
        }
        R.Add("mechanical: switch expression -> arms tested in order", Where(se));
        return sb.Append($"gen::Throw(\"{name}: switch expression\"); }}())").ToString();
    }

    void Switch(ISwitchOperation sw, StringBuilder sb, int ind)
    {
        var pad = new string(' ', ind * 4);
        var v = $"sw_{tmp++}";
        // a break nested deeper than a section's own statements (inside an if): C#'s jump past the switch, a goto to a label after it
        bool nestedBreak = sw.Cases.SelectMany(c => c.Body).Where(o => o is not IBranchOperation).SelectMany(o => o.DescendantsAndSelf())
            .Any(o => o is IBranchOperation { BranchKind: BranchKind.Break } nb && SymbolEqualityComparer.Default.Equals(nb.Target, sw.ExitLabel));
        if (nestedBreak) switchExits[sw.ExitLabel] = $"{v}_end";
        sb.Append(pad).Append($"{{\n{pad}    auto {v} = {Expr(sw.Value)};\n");
        // the variables the case patterns declare (case MainMenu mainMenu:); the sections' own locals are declared where they are
        foreach (var d in sw.Cases.SelectMany(c => c.Clauses).OfType<IPatternCaseClauseOperation>().SelectMany(c => Descend(c.Pattern)).OfType<IDeclarationPatternOperation>())
            if (d.DeclaredSymbol is ILocalSymbol l) sb.Append(pad).Append($"    {Cpp(l.Type)} {Id(l.Name)} = {Literal(null, l.Type)};\n");
        bool first = true;
        IOperation[] defaultBody = null;
        foreach (var sec in sw.Cases)
        {
            var conds = new List<string>();
            foreach (var cl in sec.Clauses)
            {
                switch (cl)
                {
                    case IDefaultCaseClauseOperation: defaultBody = sec.Body.ToArray(); break;
                    case IPatternCaseClauseOperation pc:
                        conds.Add(Pattern(v, pc.Pattern) + (pc.Guard != null ? " && " + Expr(pc.Guard) : ""));
                        break;
                    case ISingleValueCaseClauseOperation svc:
                        conds.Add(svc.Value.Type?.SpecialType == SpecialType.System_String ? $"gen::StrEq({v}, {Expr(svc.Value)})" : $"{v} == {Expr(svc.Value)}");
                        break;
                    default: conds.Add(Unsupported(cl, "case clause " + cl.CaseKind)); break;
                }
            }
            if (conds.Count == 0) continue;
            sb.Append(pad).Append($"    {(first ? "" : "else ")}if ({string.Join(" || ", conds)}) {{\n");
            SectionBody(sec.Body, sb, ind + 2);
            sb.Append(pad).Append("    }\n");
            first = false;
        }
        if (defaultBody != null)
        {
            sb.Append(pad).Append(first ? "    {\n" : "    else {\n");
            SectionBody(defaultBody, sb, ind + 2);
            sb.Append(pad).Append("    }\n");
        }
        sb.Append(pad).Append("}\n");
        if (nestedBreak) { sb.Append(pad).Append($"{v}_end:;\n"); R.Add("mechanical: a break nested in a switch section -> a goto past the switch", Where(sw)); }
    }
    void SectionBody(IEnumerable<IOperation> ops, StringBuilder sb, int ind)
    {
        foreach (var s in ops)
        {
            if (s is IBranchOperation { BranchKind: BranchKind.Break }) continue;   // a switch section's break: the end of our if-branch
            Stmt(s, sb, ind);
        }
    }

    /* ---------------------------------------------------------------- delegates ---------------------------------------------------------------- */
    /* a delegate object the game will call (a Tween's OnUpdate, an Alarm's OnComplete): a lambda becomes a function of
     * (target, args...); BRUTE's Invoke calls fn(target, args...) (docs/ws-tfex-subclass.md row 7) */
    /* a delegate of type dt over a reflected game method m (Delegate.CreateDelegate / AccessTools.MethodDelegate): bound to target (the
     * game method's body as fn, as a method group), or open (target null: the instance is the delegate's first argument; a thunk) */
    string ReflectedDelegate(IOperation site, INamedTypeSymbol dt, IMethodSymbol m, string target)
    {
        var (cls, idx) = DelegateHost();
        if (cls == null) return Unsupported(site, "more than 64 delegate sites in a class (gen::Class::method)");
        var dtype = DelegateTypeKey(dt);
        if (target != null)
        {
            R.Add("mechanical: Delegate.CreateDelegate over a reflected game method -> a method-group delegate", Where(site));
            return $"gen::NewDelegate({cls}, {idx}, {dtype}, {target}, reinterpret_cast<void*>(gen::Base() + {B.Need("m", SwitchName(m.ContainingType), m.Name, IlSig(m))}))";
        }
        var inv = dt.DelegateInvokeMethod;
        if (inv == null || (!m.IsStatic && inv.Parameters.Length != m.Parameters.Length + 1) || (m.IsStatic && inv.Parameters.Length != m.Parameters.Length))
            return Unsupported(site, "AccessTools.MethodDelegate whose delegate doesn't match the method");
        var fn = $"Lambda_{lambdaCount++}";
        var ps = inv.Parameters.Select((q, i) => $"{Cpp(q.Type)} p{i}").ToList();
        ps.Insert(0, "void* /*target: an instance of the host class, unused*/");
        var sig = $"static {Cpp(inv.ReturnType)} {fn}({string.Join(", ", ps)})";
        var args = Enumerable.Range(m.IsStatic ? 0 : 1, m.Parameters.Length).Select(i => $"p{i}").ToList();
        var call = Call(m, m.IsStatic ? null : "p0", args, site);
        lambdaDecls.Append(sig).Append(";\n");
        lambdaBodies.Append($"/*@M {type.Name}.{current?.Name}*/\n/* AccessTools.MethodDelegate: {m.ContainingType.Name}.{m.Name} (open instance) */\n{sig} {{\n    {(inv.ReturnsVoid ? "" : "return ")}{call};\n}}\n\n");
        R.Add("mechanical: AccessTools.MethodDelegate over a reflected game method -> a delegate (a thunk calling it)", Where(site));
        return $"gen::NewDelegate({cls}, {idx}, {dtype}, gen::New({cls}), reinterpret_cast<void*>(&{fn}))";
    }
    /* a delegate over a static method closed over its first argument (Delegate.CreateDelegate(typeof(D), first, staticMethod)): BRUTE's Invoke
     * calls fn(target, args...), so the target is that argument and a thunk passes it on as the method's first parameter */
    string ClosedStaticDelegate(IOperation site, INamedTypeSymbol dt, IMethodSymbol m, string first)
    {
        var (cls, idx) = DelegateHost();
        if (cls == null) return Unsupported(site, "more than 64 delegate sites in a class (gen::Class::method)");
        var inv = dt.DelegateInvokeMethod;
        var fn = $"Lambda_{lambdaCount++}";
        var ps = inv.Parameters.Select((q, i) => $"{Cpp(q.Type)} p{i}").ToList();
        ps.Insert(0, "void* t_");
        var sig = $"static {Cpp(inv.ReturnType)} {fn}({string.Join(", ", ps)})";
        var args = new List<string> { "t_" }; args.AddRange(Enumerable.Range(0, inv.Parameters.Length).Select(i => $"p{i}"));
        if (SymbolEqualityComparer.Default.Equals(m.ContainingType, type)) { methodCallees.Add(m.OriginalDefinition); AutoU(m.ContainingType, m); }   // (named by reflection, no call: the auto unit must emit it)
        var call = SymbolEqualityComparer.Default.Equals(m.ContainingType, type) ? $"{MName(m)}({string.Join(", ", args)})" : Call(m, null, args, site);
        lambdaDecls.Append(sig).Append(";\n");
        lambdaBodies.Append($"/*@M {type.Name}.{current?.Name}*/\n/* Delegate.CreateDelegate: {m.ContainingType.Name}.{m.Name} closed over its first argument */\n{sig} {{\n    {(inv.ReturnsVoid ? "" : "return ")}{call};\n}}\n\n");
        R.Add("mechanical: Delegate.CreateDelegate over a static method closed over its first argument -> a delegate whose target is that argument (a thunk)", Where(site));
        includes.Add("tfexgen_state.hpp");   // its method's name for Delegate.Method.Name (statert::ActionName: TF.State's ToNames)
        return $"tfex::gen::statert::NameDelegate(gen::NewDelegate({cls}, {idx}, {DelegateTypeKey(dt)}, {first}, reinterpret_cast<void*>(&{fn})), \"{m.Name}\")";
    }
    /* the next delegate site's method record: a class unit's own class (64 records); a static unit's host classes, made on first use
     * (Object base, nothing else), 64 records each: g_dhost, g_dhost1, ... (TF.EX's ScenarioCatalog has 80-odd Expect lambdas) */
    readonly HashSet<int> dhostsDeclared = new();
    (string cls, int idx) DelegateHost()
    {
        int n = delegateIndex++;
        if (isClass) return n >= 64 ? (null, 0) : ("g_class", n);
        int h = n / 64;
        var v = h == 0 ? "g_dhost" : $"g_dhost{h}";
        var hostExpr = $"gen::HostClass({v}, u\"{type.Name}{(h == 0 ? "" : "_" + h)}\", {B.Need("t", "System.Object")}, {B.Need("d", "System.Object")}, {B.Need("vb", "System.Object")})";
        if (dhostsDeclared.Add(h))
        {
            lambdaDecls.Insert(0, $"static gen::Class {v};   /* method records of this unit's delegates (gen::HostClass) */\n");
            // made with the unit's statics (MakeStatics, before a rollback window): made at the first delegate inside the window, a
            // restore took its records back while the module's g_dhost still said made, and the next delegate jumped through them (the
            // Pickup tween re-made in a resimulation, 2026-10-21)
            lazyMakers.Add(hostExpr);
        }
        return (hostExpr, n % 64);
    }
    string DelegateObject(IDelegateCreationOperation dc)
    {
        // a static unit: its delegates' method records live in a host class made on first use (Object base, nothing else)
        var (cls, idx) = DelegateHost();
        if (cls == null) return Unsupported(dc, "more than 64 delegate sites in a class (gen::Class::method)");
        var dtype = DelegateTypeKey(dc.Type);
        bool hasThis = current != null && !current.IsStatic || lambdaHasSelf;
        switch (dc.Target)
        {
            case IAnonymousFunctionOperation af:
            {
                var fn = $"Lambda_{lambdaCount++}";
                var captured = CapturedSymbols(af);
                string closure = null;
                if (captured.Count > 0)
                {
                    // C#'s display class, for captures that never change after the lambda is made (a copy then means the same): the
                    // captured values (and `this`) in a managed object[] (scanned by the GC), the delegate's target
                    var changed = captured.Where(c => WrittenAfterDeclaration(dc, c) && !(c is ILocalSymbol bl && boxedLocals.Contains(bl))).Select(c => c.Name).ToList();
                    if (changed.Count > 0) return Unsupported(dc, "lambda capturing locals that change (" + string.Join(", ", changed) + "): needs a shared display class");
                    bool Boxed(ISymbol c) => c is ILocalSymbol bl2 && boxedLocals.Contains(bl2);
                    var types = captured.Select(c => Boxed(c) ? "void*" : Cpp(c is ILocalSymbol l ? l.Type : ((IParameterSymbol)c).Type)).ToList();
                    if (types.Any(t => t == "auto" || t.Contains("&"))) return Unsupported(dc, "lambda capturing a model collection");
                    closure = $"Clo_{fn}";
                    lambdaDecls.Append($"struct {closure} {{ {(hasThis ? "void* self; " : "")}{string.Concat(captured.Select((c, i) => $"{types[i]} {Id(c.Name)}; "))}}};\n");
                    R.Add("mechanical: lambda capturing unchanged locals -> closure (object[] target)", $"{Where(dc)}: {string.Join(", ", captured.Select(c => c.Name))}");
                }
                else R.Add("mechanical: lambda -> delegate (function of target, args)", Where(dc));
                var ps = af.Symbol.Parameters.Select(p => $"{Cpp(p.Type)} {Id(p.Name)}").ToList();
                if (closure != null) ps.Insert(0, "void* clo_");
                else if (hasThis) ps.Insert(0, "void* self");
                else ps.Insert(0, "void* /*target: an instance of the host class, unused*/");   // always a target: Invoke's calling convention either way
                var sig = $"static {Cpp(af.Symbol.ReturnType)} {fn}({string.Join(", ", ps)})";
                lambdaDecls.Append(sig).Append(";\n");
                var sb = new StringBuilder();
                if (closure != null)
                {
                    sb.Append($"    auto& c_ = *static_cast<{closure}*>(gen::ClosureData(clo_));\n");
                    if (hasThis) sb.Append("    void* self = c_.self;\n");
                    foreach (var c in captured)
                        if (c is ILocalSymbol bl3 && boxedLocals.Contains(bl3)) sb.Append($"    {Cpp(bl3.Type)}& {Id(c.Name)} = gen::ArrData<{Cpp(bl3.Type)}>(c_.{Id(c.Name)})[0];   /* shared */\n");
                        else sb.Append($"    auto {Id(c.Name)} = c_.{Id(c.Name)};\n");
                    sb.Append("    (void)c_;\n");
                }
                var saveCb = callbackDepth.ToArray(); callbackDepth.Clear();
                var saveSelf = lambdaHasSelf; lambdaHasSelf = hasThis;
                // inside this lambda its shared (boxed) captured locals are the closure's boxes, c_.<name>: a lambda made in here passes those on
                var saveBoxes = boxesViaClosure;
                boxesViaClosure = new HashSet<ILocalSymbol>(captured.OfType<ILocalSymbol>().Where(boxedLocals.Contains), SymbolEqualityComparer.Default);
                var saveCatch = catchLabels.ToList(); catchLabels.Clear();   // (a catch label of the enclosing method is out of reach here)
                var saveMarks = (stmtThrows, stmtYields);                    // (and the lambda's own throwing / yielding calls stay in it)
                BlockStmts(af.Body, sb, 1);
                (stmtThrows, stmtYields) = saveMarks;
                catchLabels.AddRange(saveCatch);
                boxesViaClosure = saveBoxes;
                lambdaHasSelf = saveSelf;
                foreach (var x in saveCb.Reverse()) callbackDepth.Push(x);
                lambdaBodies.Append($"/*@M {type.Name}.{current?.Name}*/\n/* {af.Syntax.ToString().Split('\n')[0].Trim()} */\n").Append(sig).Append(" {\n").Append(sb).Append("}\n\n");
                var tgt = closure == null ? (hasThis ? "self" : $"gen::New({cls})")   // a managed target (Invoke reads its type's flags)
                        : $"gen::NewClosure({closure}{{{(hasThis ? "self, " : "")}{string.Join(", ", captured.Select(c => c is ILocalSymbol bl4 && boxedLocals.Contains(bl4) ? (boxesViaClosure.Contains(bl4) ? "c_." + Id(c.Name) : Id(c.Name) + "_arr.obj") : Id(c.Name)))}}}, {B.Need("t", "System.Object[]")})";
                return $"gen::NewDelegate({cls}, {idx}, {dtype}, {tgt}, {ManagedAbi(dc.Type, fn, true)})";
            }
            case IMethodReferenceOperation mr:
            {
                R.Add("mechanical: method group -> delegate", Where(dc));
                var m = mr.Method;
                var target = m.IsStatic ? "nullptr" : (Recv(mr.Instance));
                string fn;
                // a virtual game method (self.FinishUnpack): C#'s ldvirtftn, the receiver's override (BombPickup.FinishUnpack arms the bomb), from
                // its vtable slot; `base.M` and non-virtuals are the declaring type's body
                if (InBinary(m.ContainingType) && !m.IsStatic && (m.IsVirtual || m.IsAbstract || m.IsOverride) && !m.IsSealed && !m.ContainingType.IsSealed &&
                    mr.Instance?.Syntax is not Microsoft.CodeAnalysis.CSharp.Syntax.BaseExpressionSyntax)
                {
                    R.Add("mechanical: a virtual game method group -> the receiver's override (its vtable slot, as ldvirtftn)", Where(dc));
                    var slot = B.Need("v", SwitchName(m.ContainingType), m.Name, IlSig(m), -1, 0);
                    return $"([&]() {{ void* t_ = {target}; return gen::NewDelegate({cls}, {idx}, {dtype}, t_, gen::VirtFn(t_, {slot})); }}())";
                }
                if (InBinary(m.ContainingType) || (OriginOf(m.ContainingType) == Origin.Bcl && m.ContainingType.IsGenericType && !m.IsVirtual && !GameLacks(m.ContainingType)))
                    // (a game instantiation of a BCL collection's method, e.g. pool.Push of a Stack<Entity> (TF.State's DropLiveFromEntityPool): its body)
                    fn = $"reinterpret_cast<void*>(gen::Base() + {B.Need("m", SwitchName(m.ContainingType), m.Name, IlSig(m))})";
                else if (SymbolEqualityComparer.Default.Equals(m.ContainingType, type)) fn = $"reinterpret_cast<void*>(&{MName(m)})";
                else if (OriginOf(m.ContainingType) == Origin.Tfex && gen.ServiceRule(m) == null && m.ContainingType.GetMembers(m.Name).Length == 1 &&
                         (OtherUnit(m.ContainingType) is TomlTable gu0 && !gu0.ContainsKey("auto") && !gu0.ContainsKey("members") ? gu0 : AutoU(m.ContainingType, m)) is TomlTable gu)
                {   // another unit's TF.EX method (panel.OnLoaded for MatchmakingService.RequestLeaderboard's callback): its function, `self` first as
                    // for the unit's own (the delegate passes its target)
                    methodCallees.Add(m.OriginalDefinition);
                    includes.Add($"tfexgen_{gu["name"]}.hpp");
                    R.Add("mechanical: another unit's method group -> delegate (its function)", Where(dc));
                    fn = $"reinterpret_cast<void*>(&tfex::gen::{gu["name"]}::{Id(m.Name)})";
                }
                else if (gen.ServiceRule(m) != null)
                {   // a [service] method as a delegate (LeaveLobby(ms.ResetLobby, ms.ResetLobby)): a thunk calling the rule, like a lambda
                    var tfn = $"Lambda_{lambdaCount++}";
                    var tps = m.Parameters.Select(q => $"{Cpp(q.Type)} {Id(q.Name)}").ToList();
                    var call = Service(m, i => i < m.Parameters.Length ? Id(m.Parameters[i].Name) : "", dc, mr.Instance);
                    if (call == null) return Unsupported(dc, "method group " + m.ToDisplayString());
                    tps.Insert(0, "void* /*target: an instance of the host class, unused*/");
                    var tsig = $"static {Cpp(m.ReturnType)} {tfn}({string.Join(", ", tps)})";
                    lambdaDecls.Append(tsig).Append(";\n");
                    lambdaBodies.Append($"/*@M {type.Name}.{current?.Name}*/\n/* {m.Name} (a service method as a delegate) */\n{tsig} {{\n    {(m.ReturnsVoid ? "" : "return ")}{call};\n}}\n\n");
                    R.Add("mechanical: a [service] method group -> delegate (a thunk calling the rule)", Where(dc));
                    return $"gen::NewDelegate({cls}, {idx}, {dtype}, gen::New({cls}), {ManagedAbi(dc.Type, tfn, true)})";
                }
                else return Unsupported(dc, "method group " + m.ToDisplayString());
                // a function of ours (not the game's, already on the managed ABI): arrays through the delegate as managed arrays
                if (fn.StartsWith("reinterpret_cast<void*>(&") && fn.EndsWith(")"))
                    fn = ManagedAbi(dc.Type, fn["reinterpret_cast<void*>(&".Length..^1], !m.IsStatic);
                return $"gen::NewDelegate({cls}, {idx}, {dtype}, {target}, {fn})";
            }
        }
        return Unsupported(dc, "delegate from " + dc.Target.Kind);
    }
    bool lambdaHasSelf, dhostDeclared;

    /* A delegate is a game object: its Invoke (the game's stub) passes and returns an array as the managed array (one register), where
     * our functions take and return gen::Arr (24 bytes: a pointer to a copy / the x8 result buffer, which the stub doesn't keep). A
     * delegate whose signature has an array gets a thunk on the managed ABI around our function: managed arrays in (gen::GameArr), the
     * managed array out (gen::ManagedOf). Invoke sites convert the same way (UnitEmitter.Expr: delegate Invoke). Without an array, the
     * function itself. `withTarget`: the function takes the delegate's target first (a lambda, an instance method), else not (a static
     * method: the stub's target register ignored). Returns the function pointer expression. (TF.State's Func<byte[]> state events
     * calling a translated mod's SaveState: the x8 write crashed, 2026-11-01.) */
    string ManagedAbi(ITypeSymbol delegateType, string fnName, bool withTarget)
    {
        var ptr = $"reinterpret_cast<void*>(&{fnName})";
        if (delegateType is not INamedTypeSymbol { DelegateInvokeMethod: IMethodSymbol inv } ||
            (inv.ReturnType is not IArrayTypeSymbol && !inv.Parameters.Any(p => p.Type is IArrayTypeSymbol))) return ptr;
        var len = B.Need("m", "System.Array", "get_Length", "", 0, 1);
        var tn = $"ManagedAbi_{lambdaCount++}";
        var ps = new List<string> { "void* t_" };
        var cargs = new List<string>();
        if (withTarget) cargs.Add("t_");
        for (int i = 0; i < inv.Parameters.Length; i++)
        {
            var pt = inv.Parameters[i].Type;
            if (pt is IArrayTypeSymbol pa) { ps.Add($"void* a{i}_"); cargs.Add($"gen::GameArr<{Cpp(pa.ElementType)}>(a{i}_, {len})"); }
            else { ps.Add($"{Cpp(pt)} a{i}_"); cargs.Add($"a{i}_"); }
        }
        var call = $"{fnName}({string.Join(", ", cargs)})";
        var ret = inv.ReturnType is IArrayTypeSymbol ? "void*" : Cpp(inv.ReturnType);
        var body = inv.ReturnType is IArrayTypeSymbol ra ? $"return gen::ManagedOf({call}, {B.Need("t", SwitchName(ra))});" : inv.ReturnsVoid ? $"{call};" : $"return {call};";
        var sig = $"static {ret} {tn}({string.Join(", ", ps)})";
        lambdaDecls.Append(sig).Append(";\n");
        lambdaBodies.Append($"/* {fnName} on a delegate's managed ABI (arrays as managed arrays) */\n{sig} {{\n    {body}\n}}\n\n");
        R.Add("mechanical: a delegate with an array in its signature -> a thunk on the managed ABI", $"{type.Name}: {delegateType.ToDisplayString()}");
        return $"reinterpret_cast<void*>(&{tn})";
    }

    List<ISymbol> CapturedSymbols(IAnonymousFunctionOperation af)
    {
        var inside = new HashSet<ISymbol>(SymbolEqualityComparer.Default);
        foreach (var p in af.Symbol.Parameters) inside.Add(p);
        foreach (var d in af.Body.Descendants().OfType<IVariableDeclaratorOperation>()) inside.Add(d.Symbol);
        foreach (var d in af.Body.Descendants().OfType<IDeclarationPatternOperation>()) if (d.DeclaredSymbol != null) inside.Add(d.DeclaredSymbol);
        foreach (var f in af.Body.Descendants().OfType<IAnonymousFunctionOperation>()) foreach (var p in f.Symbol.Parameters) inside.Add(p);
        var outside = new List<ISymbol>();
        foreach (var r in af.Body.Descendants())
        {
            ISymbol s = r switch { ILocalReferenceOperation l => l.Local, IParameterReferenceOperation p => p.Parameter, _ => null };
            // a local declared inside the lambda any other way (var (a, b) = ..., out var x): by where its declaration is
            if (s is ILocalSymbol ls && ls.DeclaringSyntaxReferences.FirstOrDefault() is { } dr && dr.SyntaxTree == af.Syntax.SyntaxTree && af.Syntax.Span.Contains(dr.Span)) continue;
            if (s != null && !inside.Contains(s) && !outside.Contains(s, SymbolEqualityComparer.Default)) outside.Add(s);
        }
        return outside;
    }
    /* a local or parameter assigned anywhere in the method other than its declaration (incl. ref/out and inside lambdas) */
    /* locals a lambda captures and the code changes after (C#'s display class, shared by the method and its lambdas): each lives in a
     * one-element managed array the method reaches through a C++ reference and the closures hold (the GC keeps it alive) */
    readonly HashSet<ILocalSymbol> boxedLocals = new(SymbolEqualityComparer.Default);
    void FindBoxedLocals(SyntaxNode syntax)
    {
        boxedLocals.Clear();
        if (syntax.SyntaxTree != model.SyntaxTree) return;
        foreach (var lam in syntax.DescendantNodes().OfType<Microsoft.CodeAnalysis.CSharp.Syntax.AnonymousFunctionExpressionSyntax>())
        {
            if (M(lam).GetOperation(lam) is not IAnonymousFunctionOperation af) continue;
            IOperation dc = af; while (dc.Parent is IDelegateCreationOperation or IConversionOperation) dc = dc.Parent;
            foreach (var c in CapturedSymbols(af).OfType<ILocalSymbol>())
                if (WrittenAfterDeclaration(dc, c) && c.DeclaringSyntaxReferences.FirstOrDefault()?.GetSyntax() is Microsoft.CodeAnalysis.CSharp.Syntax.VariableDeclaratorSyntax &&
                    Cpp(c.Type) is var ct && ct != "auto" && !ct.Contains("&") && !gen.Models.IsModel(c.Type) && c.Type is not IArrayTypeSymbol && !IsValColl(c.Type))
                    boxedLocals.Add(c);
        }
    }
    string BoxArrayType(ILocalSymbol l) => B.Need("t", SwitchName(ws.Compilation.CreateArrayTypeSymbol(l.Type is INamedTypeSymbol { TypeKind: TypeKind.Enum } e ? e.EnumUnderlyingType! : l.Type)));

    static IEnumerable<IOperation> Ups(IOperation o) { for (var p = o.Parent; p != null; p = p.Parent) yield return p; }
    static bool WrittenAfterDeclaration(IOperation within, ISymbol s)
    {
        var root = within;
        while (root.Parent != null) root = root.Parent;
        bool Is(IOperation t) => t switch { ILocalReferenceOperation l => SymbolEqualityComparer.Default.Equals(l.Local, s),
                                            IParameterReferenceOperation p => SymbolEqualityComparer.Default.Equals(p.Parameter, s), _ => false };
        foreach (var o in root.DescendantsAndSelf())
            switch (o)
            {
                case IAssignmentOperation a when Is(a.Target) && !(a.Target is ILocalReferenceOperation { IsDeclaration: true }): return true;
                case IIncrementOrDecrementOperation inc when Is(inc.Target): return true;
                case IArgumentOperation arg when arg.Parameter?.RefKind is RefKind.Ref or RefKind.Out && Is(arg.Value): return true;
                // `var (a, b) = ...` / `(var a, var b) = ...` declares them: not a write after the declaration
                case IDeconstructionAssignmentOperation da when da.Target.DescendantsAndSelf().Any(t => Is(t) && !Ups(t).TakeWhile(x => x != da).Any(x => x is IDeclarationExpressionOperation) && da.Target is not IDeclarationExpressionOperation): return true;
            }
        return false;
    }
    List<string> CapturedLocals(IAnonymousFunctionOperation af)
    {
        var inside = new HashSet<ISymbol>(SymbolEqualityComparer.Default);
        foreach (var p in af.Symbol.Parameters) inside.Add(p);
        foreach (var d in af.Body.Descendants().OfType<IVariableDeclaratorOperation>()) inside.Add(d.Symbol);
        foreach (var d in af.Body.Descendants().OfType<IDeclarationPatternOperation>()) if (d.DeclaredSymbol != null) inside.Add(d.DeclaredSymbol);
        foreach (var f in af.Body.Descendants().OfType<IAnonymousFunctionOperation>()) foreach (var p in f.Symbol.Parameters) inside.Add(p);
        var outside = new List<string>();
        foreach (var r in af.Body.Descendants())
        {
            ISymbol s = r switch { ILocalReferenceOperation l => l.Local, IParameterReferenceOperation p => p.Parameter, _ => null };
            if (s != null && !inside.Contains(s) && !outside.Contains(s.Name)) outside.Add(s.Name);
        }
        return outside;
    }

    /* a lambda used where it runs at once (a LINQ predicate, List.ForEach): a C++ lambda capturing by reference */
    string InlineLambda(IOperation arg)
    {
        while (arg is IConversionOperation c) arg = c.Operand;
        if (arg is IDelegateCreationOperation dc && dc.Target is IAnonymousFunctionOperation af)
        {
            var ps = string.Join(", ", af.Symbol.Parameters.Select(p => $"{(modelLambdaParams.Contains(p) ? "auto" : Cpp(p.Type))} {Id(p.Name)}"));
            var sb = new StringBuilder();
            var saveCb = callbackDepth.ToArray(); callbackDepth.Clear();
            var saveLoop = loopDepthInCallback; loopDepthInCallback = 0;
            BlockStmts(af.Body, sb, 1);
            loopDepthInCallback = saveLoop;
            foreach (var x in saveCb.Reverse()) callbackDepth.Push(x);
            var body = sb.ToString().TrimEnd('\n').Replace("\n", " ");
            return $"[&]({ps}) -> {Cpp(af.Symbol.ReturnType)} {{ {body.Trim()} }}";
        }
        // a method group (Where(IsCodeChar), Select(GetVariantTitle), Any(list.Contains)): a lambda calling it with the delegate's parameters
        if (arg is IDelegateCreationOperation { Target: IMethodReferenceOperation mr } dc2 && dc2.Type is INamedTypeSymbol { DelegateInvokeMethod: { } di })
        {
            var names = di.Parameters.Select((p, i) => $"a{i}_").ToList();
            var ps = string.Join(", ", di.Parameters.Select((p, i) => $"{Cpp(p.Type)} {names[i]}"));
            var mm = mr.Method;
            string call = null;
            if (gen.ServiceRule(mm) != null) call = Service(mm, i => i < names.Count ? names[i] : "", arg, mr.Instance);
            else if (mm.IsExtern && mm.GetDllImportData() is DllImportData dd && mm.Parameters.All(p => p.Type.IsValueType && p.RefKind == RefKind.None) &&
                     (mm.ReturnsVoid || mm.ReturnType.IsValueType))
            {   // a P/Invoke extern as a method group (Select(GGRSFFI.netplay_local_player_handle_at)): its direct C call, as ExternCall makes
                var entry = dd.EntryPointName ?? mm.Name;
                externDecls.Add($"extern \"C\" {(mm.ReturnsVoid ? "void" : Cpp(mm.ReturnType))} {entry}({string.Join(", ", mm.Parameters.Select(p => Cpp(p.Type)))});");
                call = $"::{entry}({string.Join(", ", names)})";
            }
            else if (OriginOf(mm.ContainingType) == Origin.Tfex && mm.IsStatic)
            {
                if (SymbolEqualityComparer.Default.Equals(mm.ContainingType, type)) { methodCallees.Add(mm.OriginalDefinition); call = $"{Id(mm.Name)}({string.Join(", ", names)})"; }
                else if ((OtherUnit(mm.ContainingType) is TomlTable mu0 && !mu0.ContainsKey("auto") && !mu0.ContainsKey("members") ? mu0 : AutoU(mm.ContainingType, mm)) is TomlTable mu)
                { methodCallees.Add(mm.OriginalDefinition); includes.Add($"tfexgen_{mu["name"]}.hpp"); call = $"tfex::gen::{mu["name"]}::{Id(mm.Name)}({string.Join(", ", names)})"; }
            }
            else if (OriginOf(mm.ContainingType) == Origin.Tfex && !mm.IsStatic && SymbolEqualityComparer.Default.Equals(mm.ContainingType, type) && isClass)
            {   // an instance method of this class as a method group (AIJimmy's .Where(IsArrowPickupCandidate)): its call on the receiver
                methodCallees.Add(mm.OriginalDefinition);
                call = $"{Id(mm.Name)}({string.Join(", ", new[] { Recv(mr.Instance) }.Concat(names))})";
            }
            else if (InBinary(mm.ContainingType) || OriginOf(mm.ContainingType) == Origin.Bcl)
                call = Call(mm, mm.IsStatic ? null : Expr(mr.Instance), names, arg);
            if (call != null)
            {
                R.Add("mechanical: method group as a LINQ argument -> a lambda calling it", Where(arg));
                return $"[&]({ps}) -> {Cpp(di.ReturnType)} {{ return {call}; }}";
            }
        }
        return Unsupported(arg, "LINQ argument that isn't a lambda");
    }

    /* ---------------------------------------------------------------- LINQ ---------------------------------------------------------------- */
    static bool IsEnumerable(IMethodSymbol m) => m.ContainingType.ToDisplayString() == "System.Linq.Enumerable";
    /* a TF.EX method returning IEnumerable<T> (not an iterator, not a model's collection): it returns a snapshot (gen::Snap<T>, C#'s
     * lazy sequence evaluated at the return; SnapCap elements at most), and its callers use that as a sequence */
    bool ReturnsSnap(IMethodSymbol m) =>
        m != null && OriginOf(m.ContainingType) == Origin.Tfex && !m.IsIterator && m.MethodKind != MethodKind.Constructor && gen.ServiceRule(m) == null &&
        // a TF.State model's sequence property holds a List object (see SeqOf): not a snapshot
        !(m.MethodKind is MethodKind.PropertyGet or MethodKind.PropertySet && m.ContainingType.DeclaringSyntaxReferences.FirstOrDefault()?.SyntaxTree.FilePath.StartsWith("state/") == true) &&
        m.ReturnType is INamedTypeSymbol rt && rt.OriginalDefinition.SpecialType == SpecialType.System_Collections_Generic_IEnumerable_T &&
        !IsModelCollection(rt) && !gen.Models.IsModel(rt.TypeArguments[0]) && rt.TypeArguments[0] is not ITypeParameterSymbol { IsReferenceType: false };
    string SnapElem(IMethodSymbol m) => Cpp(((INamedTypeSymbol)m.ReturnType).TypeArguments[0]);
    static IMethodSymbol CalledSnap(IOperation op, Func<IMethodSymbol, bool> returnsSnap)
    {
        while (op is IConversionOperation c && c.IsImplicit) op = c.Operand;
        if (op is IPropertyReferenceOperation { Property.GetMethod: { } pg } && returnsSnap(pg)) return pg;   // a TF.EX IEnumerable property (Series.PlayedMapIds)
        return op is IInvocationOperation inv && returnsSnap(inv.TargetMethod) ? inv.TargetMethod : null;
    }

    /* a value handed on (an argument, an assignment to a field/property/element, a return), not consumed where it is made */
    static IOperation SkipConv(IOperation p) { while (p is IConversionOperation or IParenthesizedOperation) p = p.Parent; return p; }
    static bool Escapes(IOperation op)
    {
        var p = op.Parent;
        while (p is IConversionOperation or IParenthesizedOperation) p = p.Parent;
        // a branch of ?: / ?? / ?. (TF.State: levels?.ToList() ?? new List<string>(), cond ? x.ToArray() : y): as the whole expression is used
        if (p is IConditionalOperation or ICoalesceOperation or IConditionalAccessOperation && op.Syntax.SyntaxTree.FilePath.StartsWith("state/"))
            return p is IConditionalOperation ? true : Escapes(p);
        // a branch of ?: elsewhere: as the whole is (LobbyVersusModeButton.AvailableModes => series ? new[] { .. } : modes.Concat(..).ToArray(),
        // a returned array: both branches the managed array)
        if (p is IConditionalOperation pcond && !ReferenceEquals(pcond.Condition, SkipConvDown(op, p))) return Escapes(p);
        // `x?.ToList() ?? []` stored (MatchStatsReport.RecordAwards: _awards[seat] = awards?.Select(..).ToList() ?? []): as the whole is
        if (p is ICoalesceOperation pco && ReferenceEquals(pco.Value, SkipConvDown(op, p))) return Escapes(p);
        // the not-null branch of a ?. chain (x?.M()?.Select(..).ToList()): used as the whole chain is (TFGamePatch.WarnAboutDesyncRiskMods'
        // riskyMods, handed to GetDesyncRiskModsMessage(List<string>))
        if (p is IConditionalAccessOperation pca && ReferenceEquals(pca.WhenNotNull, SkipConvDown(op, p))) return Escapes(p);
        // `var x = ....ToList()`: a snapshot only while x is just enumerated (foreach, LINQ); a List member call, an argument, a return
        // or an assignment needs the managed List
        if (p is IVariableInitializerOperation { Parent: IVariableDeclaratorOperation d }) return LocalEscapes(d.Symbol, op);
        // an element of a tuple (PlayerProfilePanel._awards.Add((icon, names.ToArray()))): stored with it, the tuple's array type
        if (p is ITupleOperation) return true;
        return p is IArgumentOperation { Parent: not IInvocationOperation { TargetMethod.ContainingType.Name: "Enumerable" } } || p is IReturnOperation ||
               (p is ISimpleAssignmentOperation a && a.Target is not ILocalReferenceOperation);
    }
    static IOperation SkipConvDown(IOperation op, IOperation parent) { var o = op; while (o.Parent != parent && o.Parent != null) o = o.Parent; return o; }
    static bool LocalEscapes(ILocalSymbol l, IOperation inMethod)
    {
        var root = inMethod;
        while (root.Parent != null) root = root.Parent;
        foreach (var r in root.Descendants().OfType<ILocalReferenceOperation>().Where(x => SymbolEqualityComparer.Default.Equals(x.Local, l)))
        {
            var p = r.Parent;
            while (p is IConversionOperation or IParenthesizedOperation) p = p.Parent;
            if (p is IForEachLoopOperation fe && fe.Collection.DescendantsAndSelf().Contains(r)) continue;
            if (p is IArgumentOperation { Parent: IInvocationOperation { TargetMethod.ContainingType.Name: "Enumerable" } }) continue;
            if (p is IPropertyReferenceOperation { Property: { Name: "Count" or "Length" } or { IsIndexer: true } } pr && pr.Instance == r) continue;   // a snapshot has Count (Length) / items
            return true;
        }
        return false;
    }

    /* the element C++ type when op is a .ToList()/.ToArray() of a LINQ chain (a snapshot), else null */
    /* a ?. chain ending in a materialized LINQ snapshot (x?.M()?.Select(..).ToList()): the snapshot's element type, else null */
    string ChainSnapElem(IOperation op)
    {
        op = Strip(op);
        if (op is not IConditionalAccessOperation) return null;
        while (Strip(op) is IConditionalAccessOperation ca) op = ca.WhenNotNull;
        return LinqMaterialized(op);
    }
    string LinqMaterialized(IOperation op)
    {
        while (op is IConversionOperation c && c.IsImplicit) op = c.Operand;
        if (op is IInvocationOperation inv && IsEnumerable(inv.TargetMethod) && inv.TargetMethod.Name is "ToList" or "ToArray" &&
            (!Escapes(inv) || gen.Models.IsModel(inv.TargetMethod.TypeArguments[0]) || IsNullable(inv.TargetMethod.TypeArguments[0])))   // models: always a snapshot
            return Cpp(inv.TargetMethod.TypeArguments[0]);
        return null;
    }

    /* C#'s default equality for T, as a C++ lambda: strings by content, everything else by == (a reference by identity) */
    /* C#'s EqualityComparer<T>.Default as a C++ comparer: a string by value; a tuple element by element (ValueTuple.Equals), its strings by
     * value (TF.EX's LobbyVersusModeButton: Array.IndexOf over (Modes, string) options, the lobby's CustomMode a fresh string) */
    static string EqOf(ITypeSymbol t) =>
        t.SpecialType == SpecialType.System_String ? "[](void* a, void* b) { return gen::StrEq(a, b); }" :
        t is INamedTypeSymbol { IsTupleType: true } tt && tt.TupleElements.Any(e => e.Type.SpecialType == SpecialType.System_String)
            ? $"[](auto a, auto b) {{ return {string.Join(" && ", tt.TupleElements.Select((e, i) => e.Type.SpecialType == SpecialType.System_String ? $"gen::StrEq(a.Item{i + 1}, b.Item{i + 1})" : $"a.Item{i + 1} == b.Item{i + 1}"))}; }}"
            : "[](auto a, auto b) { return a == b; }";

    /* the runtime type a delegate of ours is made as: its own instantiation, or, when the game never compiled it (Func<OptionsButton>,
     * Action<(string, int)>: TF.EX's settings entries), one of the same register shape: every argument as object (a reference, an int or
     * a bool travels in one x register; a tuple of a reference and an int in two: AAPCS64 passes the 16-byte pair that way). Only our code invokes such a delegate
     * (vtable slot 0x58), so the shape is all that matters. */
    string DelegateTypeKey(ITypeSymbol t)
    {
        var own = B.Need("t", SwitchName(t));
        if (Bound(own) || t is not INamedTypeSymbol { TypeKind: TypeKind.Delegate }) return own;
        if (DelegateShapeType(t) is INamedTypeSymbol sub)
        {
            R.Add("mechanical: a delegate type the game never compiled -> one of the same register shape", $"{t.ToDisplayString()} -> {sub.ToDisplayString()}");
            return B.Need("t", SwitchName(sub));
        }
        // no type of the same register shape either (a Vector2 argument: float registers): made as System.Action, a stand-in, and
        // invoked directly (DelegateDirect: its function called, target first, as Invoke would), never through its type's Invoke
        R.Add("mechanical: a delegate type the game never compiled, of no shape it has -> a stand-in type, invoked directly", t.ToDisplayString());
        return B.Need("t", SwitchName(ws.Compilation.GetTypeByMetadataName("System.Action")));
    }
    /* a delegate type our code makes that the game never compiled and has no same-shape stand-in for: its invocation calls its function
     * directly (gen::DlgInvoke) */
    bool DelegateDirect(INamedTypeSymbol t) => t.TypeKind == TypeKind.Delegate && KnownUnbound(B.Need("t", SwitchName(t))) && DelegateShapeType(t) == null;
    /* a binding the binder looked for and the Switch's binary doesn't have (not: one not looked for yet) */
    bool KnownUnbound(string key)
    {
        var k = key.StartsWith("bind::") ? key[6..] : key;
        return B.Binding.ContainsKey(k) && B.Binding[k]?["error"] != null;
    }
    INamedTypeSymbol DelegateShapeType(ITypeSymbol t)
    {
        if (t is not INamedTypeSymbol { TypeKind: TypeKind.Delegate } d || d.DelegateInvokeMethod is not IMethodSymbol inv) return null;
        var obj = ws.Compilation.GetSpecialType(SpecialType.System_Object);
        var ps = new List<ITypeSymbol>();
        foreach (var p in inv.Parameters)
        {
            if (p.Type.IsReferenceType) ps.Add(obj);
            else if (p.Type is INamedTypeSymbol { IsTupleType: true } tt && tt.TupleElements.Length == 2 && tt.TupleElements[0].Type.IsReferenceType &&
                     tt.TupleElements[1].Type.SpecialType == SpecialType.System_Int32)
            { ps.Add(obj); ps.Add(obj); }
            else if (p.Type.SpecialType is SpecialType.System_Int32 or SpecialType.System_Boolean or SpecialType.System_Int64 or SpecialType.System_UInt32 or
                     SpecialType.System_Char or SpecialType.System_Byte or SpecialType.System_Int16 || p.Type.TypeKind == TypeKind.Enum) ps.Add(obj);   // (an integer / bool / enum: the same x register)
            else if (p.Type.ToDisplayString() == "Microsoft.Xna.Framework.Point") ps.Add(obj);   // (two ints, 8 bytes, not floats: one x register by AAPCS64; AIJimmy's Func<Point, ...>)
            else if (IsNullable(p.Type) && ((INamedTypeSymbol)p.Type).TypeArguments[0].SpecialType is SpecialType.System_Int32 or SpecialType.System_Boolean)
                ps.Add(obj);   // (an int? / bool?: 8 bytes, one x register too; LeaderboardPanel's onSelect: Action<int?>)
            else return null;
        }
        INamedTypeSymbol sub;
        if (inv.ReturnsVoid)
            sub = ps.Count == 0 ? ws.Compilation.GetTypeByMetadataName("System.Action") : ws.Compilation.GetTypeByMetadataName($"System.Action`{ps.Count}")?.Construct(ps.ToArray());
        else if (inv.ReturnType.IsReferenceType || inv.ReturnType.SpecialType is SpecialType.System_Boolean or SpecialType.System_Int32)
            // (a bool / int result: the same x0 register; TF.EX's Scenario.Expect is a Func<Level, bool>)
            sub = ws.Compilation.GetTypeByMetadataName($"System.Func`{ps.Count + 1}")?.Construct(ps.Append(obj).ToArray());
        else return null;
        return sub == null || KnownUnbound(B.Need("t", SwitchName(sub))) ? null : sub;   // (not yet known: the binder resolves it this run)
    }

    /* the List<T> every call of m in TF.EX's code passes as argument `ordinal` (directly, or a method returning List<T>), else null */
    INamedTypeSymbol ListPassedEverywhere(IMethodSymbol m, int ordinal)
    {
        INamedTypeSymbol list = null; int calls = 0;
        foreach (var t in ws.Trees)
        {
            var sm = ws.Model(t);
            foreach (var ie in t.GetRoot().DescendantNodes().OfType<Microsoft.CodeAnalysis.CSharp.Syntax.InvocationExpressionSyntax>())
            {
                if (sm.GetSymbolInfo(ie).Symbol is not IMethodSymbol cm || !SymbolEqualityComparer.Default.Equals(cm.OriginalDefinition, m.OriginalDefinition)) continue;
                if (sm.GetOperation(ie) is not IInvocationOperation ci) return null;
                var arg = ci.Arguments.FirstOrDefault(x => x.Parameter?.Ordinal == ordinal)?.Value;
                while (arg is IConversionOperation c && c.IsImplicit) arg = c.Operand;
                if (arg?.Type is not INamedTypeSymbol at || at.OriginalDefinition.ToDisplayString() != "System.Collections.Generic.List<T>") return null;
                list ??= at; calls++;
            }
        }
        return calls > 0 ? list : null;
    }

    /* a sequence expression (callable visitor) for a LINQ source */
    /* Monocle's layer.GetList<T>(): the layer's entities that are T (the game has no instantiation for our T to bind; Scene.Remove only
     * queues, so walking the live list is what C#'s copy gives) */
    static bool IsLayerGetList(IOperation op) =>
        Strip(op) is IInvocationOperation { TargetMethod: { Name: "GetList", IsGenericMethod: true, Parameters.Length: 0, ContainingType.Name: "Layer" } };
    string LayerGetListSeq(IInvocationOperation gl)
    {
        var layer = gl.TargetMethod.ContainingType;
        var t = gl.TargetMethod.TypeArguments[0];
        ISymbol ents = layer.GetMembers("Entities").FirstOrDefault(x => x is IPropertySymbol or IFieldSymbol);
        if (ents == null || (ents is IPropertySymbol ep0 ? ep0.Type : ((IFieldSymbol)ents).Type) is not INamedTypeSymbol { OriginalDefinition.MetadataName: "List`1" } lt)
            return Unsupported(gl, "Layer.GetList<T> (no Entities list)");
        var recv = Expr(gl.Instance);
        var list = ents is IPropertySymbol ep ? Call(ep.GetMethod, recv, new List<string>(), gl) : $"gen::Fld<void*>({recv}, {B.Need("f", SwitchName(layer), ents.Name)})";
        R.Add("mechanical: Layer.GetList<T>() -> the layer's entities of type T", Where(gl));
        return $"gen::OfType(gen::FromList<void*>({list}, {MethodBody(lt, "get_Count", null)}, {MethodBody(lt, "get_Item", new[] { "int32" })}), {TypeOfAny(t)})";
    }

    IOperation ArrRule(IOperation op)
    {
        op = Strip(op);
        while (op is IConversionOperation c) op = Strip(c.Operand);
        // (`rule ?? []`, `a?.b?.Rule`: the rule itself; its receivers are the Switch's service tokens, never null, and it never answers null)
        if (op is ICoalesceOperation co) return ArrRule(co.Value);
        if (op is IConditionalAccessOperation ca) return ArrRule(ca.WhenNotNull);
        ISymbol m = op is IPropertyReferenceOperation pr ? pr.Property : op is IInvocationOperation iv ? iv.TargetMethod : null;
        return m != null && gen.ServiceRule(m) is TomlTable r && r.TryGetValue("arr", out var a) && a is true ? op : null;
    }
    string SeqOf(IOperation op)
    {
        while (op is IConversionOperation c && c.IsImplicit) op = c.Operand;
        if (IsLayerGetList(op)) return LayerGetListSeq((IInvocationOperation)Strip(op));
        var sepOp = Strip(op);
        if (sepOp is IConditionalAccessInstanceOperation cai)   // p?.ToList(): the parameter the access tests
        {   // (the access whose not-null branch holds this instance: in `a?.M()?.X` M()'s receiver is a, not the `?.X` access M() is the test of)
            IOperation prev = cai;
            for (var up = cai.Parent; up != null; prev = up, up = up.Parent)
                if (up is IConditionalAccessOperation ca && ReferenceEquals(ca.WhenNotNull, prev)) { sepOp = Strip(ca.Operation); break; }
        }
        if (sepOp is IParameterReferenceOperation { Parameter: var sep } && StateEnumListOf(sep.OriginalDefinition) is INamedTypeSymbol selt)
        {   // TF.State's IEnumerable<int / Vector2> parameter: the game List<T> its callers pass (Expr.cs copies other sequences into one)
            R.Add("mechanical: TF.State's IEnumerable<T> parameter -> the game List<T> it holds", Where(op));
            return $"gen::FromList<{Cpp(selt.TypeArguments[0])}>({Expr(op)}, {MethodBody(selt, "get_Count", null)}, {MethodBody(selt, "get_Item", new[] { "int32" })})";
        }
        if (Strip(op) is IInvocationOperation { TargetMethod: var srm } && !ReturnsSnap(srm.OriginalDefinition) && StateEnumListOf(srm.ReturnType, srm) is INamedTypeSymbol srlt)
        {   // a TF.State method returning its game List<T> as IEnumerable<T>
            R.Add("mechanical: TF.State's IEnumerable<T> result -> the game List<T> it returns", Where(op));
            return $"gen::FromList<{Cpp(srlt.TypeArguments[0])}>({Expr(op)}, {MethodBody(srlt, "get_Count", null)}, {MethodBody(srlt, "get_Item", new[] { "int32" })})";
        }
        // a game / FNA ReadOnlyCollection<T> (SpriteFont.Characters): its own Count and indexer, as a List's
        if (op.Type is INamedTypeSymbol { TypeArguments.Length: 1 } roc && roc.OriginalDefinition.ToDisplayString() == "System.Collections.ObjectModel.ReadOnlyCollection<T>")
        {
            R.Add("mechanical: a ReadOnlyCollection<T> -> its Count and indexer", Where(op));
            return $"gen::FromList<{Cpp(roc.TypeArguments[0])}>({Expr(op)}, {MethodBody(roc, "get_Count", null)}, {MethodBody(roc, "get_Item", new[] { "int32" })})";
        }
        // a [service] rule marked `arr = true` gives the collection as an array of ours (FortRise's IModInterop.LoadedMods, a mod's
        // Dependencies: the glue's mod objects): its elements; `rule ?? []` likewise (the rule never answers null)
        // x?.M()?.Select(...): the receiver is M()'s snapshot (a TF.EX method returning IEnumerable<T>: TFGamePatch.WarnAboutDesyncRiskMods)
        if (Strip(op) is IConditionalAccessInstanceOperation && CalledSnap(sepOp, ReturnsSnap) != null) return Expr(op);
        if ((ArrRule(op) ?? ArrRule(sepOp)) is IOperation arrOp) { R.Add("rule: an `arr` [service] collection -> its array's elements (gen::FromArr)", Where(op)); return $"gen::FromArr({Expr(arrOp)})"; }
        if (op.Type?.SpecialType == SpecialType.System_String) { R.Add("mechanical: LINQ over a string -> its characters (gen::StrSeq)", Where(op)); return $"gen::StrSeq({Expr(op)})"; }
        if (op is IInvocationOperation { TargetMethod.Name: "Range" } rg && IsEnumerable(rg.TargetMethod) && rg.Arguments.Length == 2)
        {
            var ra = rg.Arguments.OrderBy(a => a.Parameter.Ordinal).ToList();
            return $"gen::Range({Expr(ra[0].Value)}, {Expr(ra[1].Value)})";
        }
        // Enumerable.Repeat(value, count) (TF.State's ExplosionExtensions: a layout of -1s): count copies of the value
        if (op is IInvocationOperation { TargetMethod.Name: "Repeat" } rpt && IsEnumerable(rpt.TargetMethod) && rpt.Arguments.Length == 2 && rpt.TargetMethod.IsGenericMethod)
        {
            var ra = rpt.Arguments.OrderBy(a => a.Parameter.Ordinal).ToList();
            R.Add("mechanical: Enumerable.Repeat -> gen::Repeat", Where(op));
            return $"gen::Repeat<{Cpp(rpt.TargetMethod.TypeArguments[0])}>({Expr(ra[0].Value)}, {Expr(ra[1].Value)})";
        }
        if (op is IInvocationOperation inv && IsEnumerable(inv.TargetMethod))
        {
            var args = inv.Arguments.OrderBy(a => a.Parameter.Ordinal).ToList();
            var src = args[0].Value;
            switch (inv.TargetMethod.Name)
            {
                case "OfType": return $"gen::OfType({SeqOf(src)}, {TypeOfAny(inv.TargetMethod.TypeArguments[0])})";
                case "DefaultIfEmpty" when args.Count == 2: return $"gen::DefaultIfEmpty({SeqOf(src)}, {Expr(args[1].Value)})";   // (TF.EX's IsValidBundle: .DefaultIfEmpty(0).Max())
                case "Where" when args.Count == 2 && inv.TargetMethod.Parameters[1].Type is INamedTypeSymbol { TypeArguments.Length: 2 }:
                    return $"gen::Where({SeqOf(src)}, {InlineLambda(args[1].Value)})";
                case "Select" when args.Count == 2 && inv.TargetMethod.Parameters[1].Type is INamedTypeSymbol { TypeArguments.Length: 2 }:
                    return $"gen::Select({SeqOf(src)}, {InlineLambda(args[1].Value)})";
                case "Select" when args.Count == 2 && inv.TargetMethod.Parameters[1].Type is INamedTypeSymbol { TypeArguments.Length: 3 }:   // (x, i) => ...
                    return $"gen::SelectI({SeqOf(src)}, {InlineLambda(args[1].Value)})";
                case "Where" when args.Count == 2 && inv.TargetMethod.Parameters[1].Type is INamedTypeSymbol { TypeArguments.Length: 3 }:   // (x, i) => ...
                    return $"gen::WhereI({SeqOf(src)}, {InlineLambda(args[1].Value)})";
                case "GroupBy" when args.Count == 2 && inv.TargetMethod.TypeArguments.Length == 2:
                    R.Add("mechanical: LINQ GroupBy(key) -> gen::GroupBy (the groups made at once, in order of their first element)", Where(op));
                    return $"gen::GroupBy<{Cpp(inv.TargetMethod.TypeArguments[1])}, {Cpp(inv.TargetMethod.TypeArguments[0])}>({SeqOf(src)}, {InlineLambda(args[1].Value)}, {EqOf(inv.TargetMethod.TypeArguments[1])})";
                case "ToList": case "ToArray":
                    return Expr(op);
                // OrderBy(k1).ThenBy(k2[, StringComparer.Ordinal / OrdinalIgnoreCase]): one stable sort by k1, then k2 (TF.State's
                // SortByFolderAndName); a string k2 without an ordinal comparer is the culture's order, which the Switch doesn't reproduce
                case "ThenBy" when Strip(src) is IInvocationOperation { TargetMethod.Name: "OrderBy" } ob && IsEnumerable(ob.TargetMethod) && ob.Arguments.Length == 2 &&
                                   (inv.TargetMethod.TypeArguments[1].SpecialType != SpecialType.System_String ? args.Count == 2 :
                                    args.Count == 3 && Strip(args[2].Value) is IPropertyReferenceOperation { Property: { Name: "Ordinal" or "OrdinalIgnoreCase", ContainingType.Name: "StringComparer" } }):
                {
                    var oa = ob.Arguments.OrderBy(a => a.Parameter.Ordinal).ToList();
                    var t = Cpp(inv.TargetMethod.TypeArguments[0]);
                    var c2 = args.Count == 3 ? ((IPropertyReferenceOperation)Strip(args[2].Value)).Property.Name == "Ordinal" ? "gen::CompareOrdinal(x2_, y2_)" : "gen::CompareOrdinalIgnoreCase(x2_, y2_)"
                                             : "(x2_ < y2_ ? -1 : y2_ < x2_ ? 1 : 0)";
                    R.Add("mechanical: LINQ OrderBy(k1).ThenBy(k2) -> one stable sort by k1, then k2 (gen::OrderByCmp)", Where(op));
                    return $"([&]() {{ auto k1_ = {InlineLambda(oa[1].Value)}; auto k2_ = {InlineLambda(args[1].Value)}; return gen::OrderByCmp<{t}>({SeqOf(oa[0].Value)}, [&]({t} a_, {t} b_) -> int32_t {{ " +
                           $"auto x1_ = k1_(a_); auto y1_ = k1_(b_); if (x1_ < y1_) return -1; if (y1_ < x1_) return 1; auto x2_ = k2_(a_); auto y2_ = k2_(b_); return {c2}; }}); }}())";
                }
                case "OrderBy": case "OrderByDescending":
                    return $"gen::OrderBy<{Cpp(inv.TargetMethod.TypeArguments[0])}>({SeqOf(src)}, {InlineLambda(args[1].Value)}, {(inv.TargetMethod.Name == "OrderByDescending" ? "true" : "false")})";
                // Order() / Order(StringComparer.Ordinal) (.NET 7): sorted by the element itself; strings only ordinal (the default comparer is
                // the culture's, which the Switch doesn't reproduce), numbers by value
                case "Order" when inv.TargetMethod.TypeArguments[0].SpecialType == SpecialType.System_String && args.Count == 2 &&
                                  Strip(args[1].Value) is IPropertyReferenceOperation { Property: { Name: "Ordinal", ContainingType.Name: "StringComparer" } }:
                    R.Add("mechanical: LINQ Order(StringComparer.Ordinal) -> a stable sort by ordinal comparison (gen::OrderByCmp)", Where(op));
                    return $"gen::OrderByCmp<void*>({SeqOf(src)}, [](void* a_, void* b_) {{ return gen::CompareOrdinal(a_, b_); }})";
                case "Order" when args.Count == 1 && inv.TargetMethod.TypeArguments[0].IsValueType && inv.TargetMethod.TypeArguments[0].SpecialType != SpecialType.None:
                    return $"gen::OrderBy<{Cpp(inv.TargetMethod.TypeArguments[0])}>({SeqOf(src)}, [](auto x_) {{ return x_; }}, false)";
                case "Reverse" when args.Count == 1: return $"gen::ReverseSeq<{Cpp(inv.TargetMethod.TypeArguments[0])}>({SeqOf(src)})";   // (a snapshot, backwards)
                case "Take": return $"gen::Take({SeqOf(src)}, {Expr(args[1].Value)})";
                case "Skip": return $"gen::Skip({SeqOf(src)}, {Expr(args[1].Value)})";
                case "Cast": return SeqOf(src);
                case "Concat" when args.Count == 2: return $"gen::SeqConcat({SeqOf(src)}, {SeqOf(args[1].Value)})";
                case "Distinct" when args.Count == 1:
                    return $"gen::Distinct<{Cpp(inv.TargetMethod.TypeArguments[0])}>({SeqOf(src)}, {EqOf(inv.TargetMethod.TypeArguments[0])})";
                case "Distinct" when args.Count == 2 && inv.TargetMethod.TypeArguments[0].SpecialType == SpecialType.System_String &&
                                     Strip(args[1].Value) is IPropertyReferenceOperation { Property: { Name: "OrdinalIgnoreCase" or "Ordinal", ContainingType.Name: "StringComparer" } } dcmp:
                    return $"gen::Distinct<void*>({SeqOf(src)}, {(dcmp.Property.Name == "Ordinal" ? EqOf(inv.TargetMethod.TypeArguments[0]) : "[](void* a, void* b) { return gen::StrEqI(a, b); }")})";
                case "Except" when args.Count == 2:
                    return $"gen::Except<{Cpp(inv.TargetMethod.TypeArguments[0])}>({SeqOf(src)}, {SeqOf(args[1].Value)}, {EqOf(inv.TargetMethod.TypeArguments[0])})";
                case "SelectMany" when args.Count == 2:
                {
                    var lam = args[1].Value;
                    while (lam is IConversionOperation c3) lam = c3.Operand;
                    if (lam is IDelegateCreationOperation { Target: IAnonymousFunctionOperation af } &&
                        af.Body.Operations.Length == 1 && af.Body.Operations[0] is IReturnOperation { ReturnedValue: var rv })
                    {
                        var p0 = af.Symbol.Parameters[0];
                        return $"gen::SelectMany({SeqOf(src)}, [&]({Cpp(p0.Type)} {Id(p0.Name)}) {{ return {SeqOf(rv)}; }})";
                    }
                    // .SelectMany(Method): one of this class's iterators (yield return), by its visitor form Method(args..., f)
                    if (lam is IDelegateCreationOperation { Target: IMethodReferenceOperation { Method: { IsIterator: true, Parameters.Length: 1 } im } imr } &&
                        SymbolEqualityComparer.Default.Equals(im.ContainingType, type))
                    {
                        methodCallees.Add(im.OriginalDefinition);
                        var recv = im.IsStatic ? "" : (imr.Instance == null || imr.Instance is IInstanceReferenceOperation ? "self" : Expr(imr.Instance)) + ", ";
                        return $"gen::SelectMany({SeqOf(src)}, [&]({Cpp(im.Parameters[0].Type)} x_) {{ return gen::MakeSeq([&, x_](auto&& f_) {{ return {Id(im.Name)}({recv}x_, f_); }}); }})";
                    }
                    break;
                }
            }
            return Unsupported(op, "LINQ " + inv.TargetMethod.Name + " as a source");
        }
        if (IsGrouping(op.Type)) return $"gen::GroupItems({Expr(op)})";   // an IGrouping's elements
        if (op is ILocalReferenceOperation l && snapLocals.ContainsKey(l.Local)) return Id(l.Local.Name);
        if (AbsentMember(op) != null || (op is IPropertyReferenceOperation { Instance: not null } ap && AbsentMember(ap.Instance) != null))
            return "gen::MakeSeq([](auto&&) { return true; })";   // a FortRise collection: empty on the Switch
        // a TF.State model's sequence property (IEnumerable<T> / ICollection<T> / IList<T>): TF.State always stores a List<T> there
        // (ToList(), new List<T>, the MessagePack reader), so it is read as that List
        if (Strip(op) is IPropertyReferenceOperation { Property: var sp } && OriginOf(sp.ContainingType) == Origin.Tfex &&
            sp.ContainingType.DeclaringSyntaxReferences.FirstOrDefault()?.SyntaxTree.FilePath.StartsWith("state/") == true &&
            sp.Type is INamedTypeSymbol { TypeArguments.Length: 1 } sit && sit.OriginalDefinition.ToDisplayString() is "System.Collections.Generic.IEnumerable<T>" or
                "System.Collections.Generic.ICollection<T>" or "System.Collections.Generic.IList<T>" or "System.Collections.Generic.IReadOnlyCollection<T>" or "System.Collections.Generic.IReadOnlyList<T>" &&
            ws.Compilation.GetTypeByMetadataName("System.Collections.Generic.List`1")?.Construct(sit.TypeArguments[0]) is INamedTypeSymbol slt)
        {
            R.Add("mechanical: a TF.State model's sequence property -> the List<T> TF.State stores there", $"{sp.ContainingType.Name}.{sp.Name}");
            if (IsValColl(slt)) return $"gen::FromValMap(static_cast<{ValCpp(slt)}*>({Expr(op)}))";
            return $"gen::FromList<{Cpp(slt.TypeArguments[0])}>({Expr(op)}, {MethodBody(slt, "get_Count", null)}, {MethodBody(slt, "get_Item", new[] { "int32" })})";
        }
        if (CalledSnap(op, ReturnsSnap) != null) return Expr(op);
        if (op.Type is IArrayTypeSymbol) return $"gen::FromArr({Expr(op)})";
        // a game Stack<T> / Queue<T> (TF.State's DropLiveFromEntityPool pool, QuestSpawnPortal's toSpawn): its own ToArray, which is in its
        // enumeration order (a stack's top first, a queue's head first)
        if (op.Type is INamedTypeSymbol { OriginalDefinition.MetadataName: "Stack`1" or "Queue`1" } sqt && !IsValColl(sqt) && !GameLacks(sqt) &&
            sqt.GetMembers("ToArray").OfType<IMethodSymbol>().FirstOrDefault(x => x.Parameters.Length == 0) is IMethodSymbol sqa)
        {
            R.Add("mechanical: a game Stack<T> / Queue<T> as a sequence -> its ToArray (its enumeration order)", Where(op));
            return $"gen::FromArr({Call(sqa, Expr(op), new List<string>(), op)})";   // (Call gives a game array as gen::Arr)
        }
        if (IsValColl(op.Type) || op is IParameterReferenceOperation { Parameter: var vsp } && IsPrimSeqParam(vsp))
            return $"gen::FromValMap({Expr(op)})";   // the container's own visitor (a map's: KV pairs)
        // an IEnumerable<T> field initialized with a List<T> (Constants.NETPLAY_SAFE_MAP = new List<string> { ... }), or with a collection
        // expression (v0.28.0's NETPLAY_SAFE_MAP = [ ... ]: CollectionExpr makes a game List<T> for it): that List
        if (op is IFieldReferenceOperation { Field: var ef } && op.Type is INamedTypeSymbol { OriginalDefinition.SpecialType: SpecialType.System_Collections_Generic_IEnumerable_T } fet &&
            ef.DeclaringSyntaxReferences.FirstOrDefault()?.GetSyntax() is Microsoft.CodeAnalysis.CSharp.Syntax.VariableDeclaratorSyntax { Initializer: not null } evd &&
            ws.Model(evd.SyntaxTree).GetOperation(evd.Initializer.Value) is IOperation eio &&
            (Strip(eio) is IObjectCreationOperation { Type: INamedTypeSymbol { OriginalDefinition.MetadataName: "List`1" } } ||
             Strip(eio) is ICollectionExpressionOperation fce && !fce.Elements.Any(e => e is ISpreadOperation) && !IsValColl(fet)))
        {
            var elt = Strip(eio) is IObjectCreationOperation { Type: INamedTypeSymbol oct } ? oct
                : ws.Compilation.GetTypeByMetadataName("System.Collections.Generic.List`1")!.Construct(fet.TypeArguments[0]);
            return $"gen::FromList<{Cpp(elt.TypeArguments[0])}>({Expr(op)}, {MethodBody(elt, "get_Count", null)}, {MethodBody(elt, "get_Item", new[] { "int32" })})";
        }
        // a value map's Keys / Values (TF.EX's SkinStreamService: _incoming.Keys.Where(...)): the map's own sequence
        if (op is IPropertyReferenceOperation { Instance: not null, Property.Name: "Keys" or "Values" } vmk && IsValMap(Strip(vmk.Instance).Type)) return Expr(op);
        if (IListOfOp(op) is string il0) return $"gen::FromIList<{Cpp(((INamedTypeSymbol)op.Type).TypeArguments[0])}>({Expr(op)}, {il0})";
        if (op is IPropertyReferenceOperation { Instance: not null, Property.Name: "Keys" or "Values" } dkv && Strip(dkv.Instance).Type is INamedTypeSymbol { OriginalDefinition.MetadataName: "Dictionary`2" } && !IsValMap(Strip(dkv.Instance).Type))
            return $"gen::Select({SeqOf(Strip(dkv.Instance))}, [](auto kv_) {{ return kv_.{(dkv.Property.Name == "Keys" ? "Key" : "Value")}; }})";
        if (op is IPropertyReferenceOperation { Instance: not null, Property.Name: "Keys" or "Values" } vk && IsValMap(Strip(vk.Instance).Type)) return Expr(op);
        if (op is IInvocationOperation it && it.TargetMethod.IsIterator && OriginOf(it.TargetMethod.ContainingType) == Origin.Tfex && SymbolEqualityComparer.Default.Equals(it.TargetMethod.ContainingType, type))
        {
            var a = string.Join(", ", it.Arguments.OrderBy(x => x.Parameter.Ordinal).Select(x => Expr(x.Value)));
            return $"gen::MakeSeq([&](auto&& f_) {{ return {Id(it.TargetMethod.Name)}({a}{(a.Length > 0 ? ", " : "")}f_); }})";
        }
        if (op.Type is INamedTypeSymbol dt && dt.OriginalDefinition.ToDisplayString() == "System.Collections.Generic.Dictionary<TKey, TValue>" && InBinary(dt.TypeArguments[1]) || op.Type is INamedTypeSymbol { OriginalDefinition.MetadataName: "Dictionary`2" })
        {
            var d = (INamedTypeSymbol)op.Type;
            var dn = SwitchName(d);
            R.Add("mechanical: game Dictionary iterated through Mono's slots", Where(op));
            return $"gen::FromDict<{Cpp(d.TypeArguments[0])}, {Cpp(d.TypeArguments[1])}>({Expr(op)}, {B.Need("f", dn, "linkSlots")}, {B.Need("f", dn, "keySlots")}, {B.Need("f", dn, "valueSlots")}, {B.Need("f", dn, "touchedSlots")})";
        }
        if (op.Type is INamedTypeSymbol { OriginalDefinition.MetadataName: "HashSet`1" } hs && !IsValColl(op.Type))
        {   // a game HashSet (TF.State's LoadState: its live entities): Mono's slots
            var hn = SwitchName(hs);
            R.Add("mechanical: game HashSet iterated through Mono's slots", Where(op));
            return $"gen::FromSet<{Cpp(hs.TypeArguments[0])}>({Expr(op)}, {B.Need("f", hn, "links")}, {B.Need("f", hn, "slots")}, {B.Need("f", hn, "touched")})";
        }
        if (IsModelList(op))
            return $"gen::FromModelList({Expr(op)})";
        // an IEnumerable<T> parameter (T a reference type) that every call in TF.EX's code passes a List<T> (Changelog.SelectUnseen's
        // releases, from Parse): enumerated as that List (the game has List code for references, none for IEnumerable<our T>)
        // (also through `p?.`: MatchStatsReport.RecordAwards' awards?.Select(...), sepOp: the parameter the access tests)
        if (sepOp is IParameterReferenceOperation { Parameter: var ep } && ep.Type is INamedTypeSymbol { OriginalDefinition.SpecialType: SpecialType.System_Collections_Generic_IEnumerable_T } eit &&
            eit.TypeArguments[0].IsReferenceType && ep.ContainingSymbol is IMethodSymbol epm && ListPassedEverywhere(epm, ep.Ordinal) is INamedTypeSymbol plt)
        {
            R.Add("mechanical: an IEnumerable<T> parameter every caller passes a List<T> -> that List", $"{epm.ContainingType.Name}.{epm.Name}({ep.Name})");
            return $"gen::FromList<{Cpp(plt.TypeArguments[0])}>({Expr(op)}, {MethodBody(plt, "get_Count", null)}, {MethodBody(plt, "get_Item", new[] { "int32" })})";
        }
        if (op.Type is INamedTypeSymbol lt && lt.OriginalDefinition.ToDisplayString() == "System.Collections.Generic.List<T>")
        {
            var el = Cpp(lt.TypeArguments[0]);
            var count = MethodBody(lt, "get_Count", null);
            var item = MethodBody(lt, "get_Item", new[] { "int32" });
            return $"gen::FromList<{el}>({Expr(op)}, {count}, {item})";
        }
        // (last) a [service] call returning IEnumerable<T> of no shape above: the hand-written side returns a visitor (gen::Snap<T>;
        // tfexgen_services.hpp GetArchers)
        if (op is IInvocationOperation { TargetMethod: var svm } && gen.ServiceRule(svm) is TomlTable svr0 && svr0.ContainsKey("cpp") &&
            svm.ReturnType is INamedTypeSymbol { OriginalDefinition.SpecialType: SpecialType.System_Collections_Generic_IEnumerable_T })
            return Expr(op);
        return Unsupported(op, "LINQ source " + op.Type?.ToDisplayString());
    }

    /* IReadOnlyList<T>, IReadOnlyCollection<T>, IList<T>, ICollection<T> over game objects (not a model's, not a value map): the
     * List<T> instantiation's bodies and T[]'s type, for gen::ICount / IItem / FromIList */
    /* the collection-interface access of an operation that isn't a lobby model's list (an ICollection<string> parameter given game
     * lists: MainMenuPatch.MissingVariantsMessage), or null */
    string IListOfOp(IOperation op) => op == null || IsModelList(op) ? null : IListOf(op.Type, allowGuess: true);
    string IListOf(ITypeSymbol t, bool allowGuess = false)
    {
        if (t is not INamedTypeSymbol n || n.TypeArguments.Length != 1 || (IsModelCollection(t) && !(allowGuess && IsStringCollectionGuess(t))) ||
            n.OriginalDefinition.ToDisplayString() is not ("System.Collections.Generic.IReadOnlyList<T>" or "System.Collections.Generic.IReadOnlyCollection<T>" or
                "System.Collections.Generic.IList<T>" or "System.Collections.Generic.ICollection<T>")) return null;
        var el = n.TypeArguments[0];
        if (gen.Models.IsModel(el) || IsPodS(el) || IsNullable(el) || el is ITypeParameterSymbol) return null;
        var lt = ws.Compilation.GetTypeByMetadataName("System.Collections.Generic.List`1")?.Construct(el);
        if (lt == null) return null;
        var arr = ws.Compilation.CreateArrayTypeSymbol(el);
        return $"gen::IList_{{{B.Need("t", SwitchName(lt))}, {MethodBody(lt, "get_Count", null)}, {MethodBody(lt, "get_Item", new[] { "int32" })}, {B.Need("t", SwitchName(arr))}, {B.Need("m", "System.Array", "get_Length", "", 0, 1)}}}";
    }

    /* `is`/OfType against a game type, or our own class (its runtime type, made by EnsureClass) */
    string TypeOfAny(ITypeSymbol t0) => TypeOfAnyS(subst.Count > 0 ? Sub(t0) : t0);
    string TypeOfAnyS(ITypeSymbol t) =>
        t is ITypeParameterSymbol tp ? tp.Name + "_type" :
        // a TF.EX interface (x is IRoundPlayerSpawner): no runtime type of its own on the Switch yet; a blocker, and null (nothing matches)
        t.TypeKind == TypeKind.Interface && OriginOf(t) == Origin.Tfex ? InterfaceTypeTest(t) :
        SymbolEqualityComparer.Default.Equals(t, type) && isClass ? "(EnsureClass(), g_class.type)" :
        OriginOf(t) == Origin.Tfex && (OtherUnit((INamedTypeSymbol)t) is TomlTable ou) ? IncludeThen($"tfexgen_{ou["name"]}.hpp", $"tfex::gen::{ou["name"]}::RuntimeType()") :   // (with its header: Auto_InputDelayAdvisor's `is FakeController`)
        OriginOf(t) == Origin.Tfex && t is INamedTypeSymbol ant && IsClassType(ant) && AutoU(ant) is TomlTable au   // a class of ours an auto unit makes
            ? IncludeThen($"tfexgen_{au["name"]}.hpp", $"tfex::gen::{au["name"]}::RuntimeType()") : TypeOf(t);   // null until made: no instance yet
    string IncludeThen(string header, string text) { includes.Add(header); return text; }
    /* `x as T` / `x is T`: the game's IsInst; a TF.EX interface with an `is` [service] rule (overrides.toml "<Interface>" = { is = "<C++
     * with $0>" }): the rule decides which Switch objects count as implementing it (vanilla round logics as IRoundPlayerSpawner); a TF.EX
     * class with an `is` rule likewise (FakeController = nullptr: the class isn't on the Switch, so it isn't pulled in) */
    string AsType(string value, ITypeSymbol t)
    {
        if ((t.TypeKind == TypeKind.Interface || t.TypeKind == TypeKind.Class) && OriginOf(t) == Origin.Tfex &&
            gen.Services.FirstOrDefault(sv => sv.pattern == t.ToDisplayString()).rule is TomlTable ir && ir.TryGetValue("is", out var isv) && isv is string iss)
        {
            gen.Rules.Use("service", t.ToDisplayString());
            R.Add("service map: type test by rule", t.ToDisplayString());
            includes.Add("tfexgen_services.hpp");
            if (ir.TryGetValue("include", out var ii) && ii is string iis) includes.Add(iis);
            return $"([&](void* v_) -> void* {{ return {iss.Replace("$0", "v_")}; }}({value}))";
        }
        return $"gen::As({value}, {TypeOfAny(t)})";
    }

    string InterfaceTypeTest(ITypeSymbol t)
    {
        // a mod's interop interface ([[interop]]): the API object FortRise's GetApi<T> gave is a T (TF.EX's Resolve<ITfStateApi>'s cast)
        if (t is INamedTypeSymbol nt && (gen.InteropProvider(nt) != null || gen.InteropAbsent(nt) != null))
        {
            R.Add("mechanical: a type test against a mod's interop interface -> the API object GetApi gave ([[interop]])", t.ToDisplayString());
            return "gen::kInteropType";
        }
        // every member of the interface has a [service] rule (IRoundPlayerSpawner.SpawnRoundPlayers: none on the Switch): no object on the
        // Switch implements it, so the test is a definite no
        if (t.GetMembers().OfType<IMethodSymbol>().All(im => gen.ServiceRule(im.AssociatedSymbol ?? im) != null))   // (a property's accessors: its rule)
        {
            R.Add("mechanical: a type test against a TF.EX interface the Switch replaces (all members ruled) -> false", t.ToDisplayString());
            return "nullptr";
        }
        // one class of ours implements it (IRoundPlayerSpawner: NetplayRoundLogic, a game subclass a unit makes): the test is against that
        // class's runtime type (null until made: then nothing matches, as no instance exists yet)
        if (gen.ImplsOf((INamedTypeSymbol)t) is { Count: 1 } one && IsClassType(one[0]) &&
            (OtherUnit(one[0]) is TomlTable iu && !iu.ContainsKey("hosted") ? iu : AutoU(one[0])) is TomlTable u)
        {
            R.Add("mechanical: a type test against a TF.EX interface -> its single implementing class of ours", $"{t.ToDisplayString()}: {one[0].Name}");
            return IncludeThen($"tfexgen_{u["name"]}.hpp", $"tfex::gen::{u["name"]}::RuntimeType()");
        }
        R.Add("unsupported: type test against a TF.EX interface", t.ToDisplayString());
        methodBlockers.Add("type test against a TF.EX interface");
        return "nullptr";
    }

    /* the body offset of a member of a corlib generic instance (List<Entity>.get_Count ...) */
    string MethodBody(INamedTypeSymbol t, string method, string[] il)
    {
        var m = t.GetMembers().OfType<IPropertySymbol>().Select(p => method.StartsWith("get_") ? p.GetMethod : p.SetMethod).FirstOrDefault(x => x?.Name == method && (il == null || x.Parameters.Length == il.Length))
             ?? t.GetMembers(method).OfType<IMethodSymbol>().FirstOrDefault(x => il == null || x.Parameters.Length == il.Length);
        if (m == null) return "0 /* no " + method + " */";
        var (k, count, kp, cp) = order.Of4(m);
        return B.Need("m", SwitchName(t), m.Name, IlSig(m), k, count, kp, cp);
    }

    string Linq(IInvocationOperation inv)
    {
        var m = inv.TargetMethod;
        var args = inv.Arguments.OrderBy(a => a.Parameter.Ordinal).ToList();
        R.Add("mechanical: LINQ " + m.Name + " -> internal iteration", Where(inv));
        // a lazy sequence stored into a TF.State model's IEnumerable<T> property (PlayerArrowsInventory.Arrows = arrows.Select(...)): a List
        // of its elements now (C# enumerates it when the state is serialized, the same tick). An array stored there (v0.27's GameState.MatchStats
        // = ....ToArray()) is the same: the property's storage is a List
        IPropertySymbol IntoState() => SkipConv(inv.Parent) is ISimpleAssignmentOperation { Target: IPropertyReferenceOperation { Property: var lp } } &&
            lp.ContainingType.DeclaringSyntaxReferences.FirstOrDefault()?.SyntaxTree.FilePath.StartsWith("state/") == true ? lp : null;
        bool lazyIntoState = m.Name is not ("ToList" or "ToArray") && m.ReturnType is INamedTypeSymbol { OriginalDefinition.SpecialType: SpecialType.System_Collections_Generic_IEnumerable_T } &&
            IntoState() != null;
        var toName = m.Name == "ToArray" && IntoState()?.Type is INamedTypeSymbol { OriginalDefinition.SpecialType: SpecialType.System_Collections_Generic_IEnumerable_T } ? "ToList" : m.Name;
        switch (lazyIntoState ? "ToList" : m.Name)
        {
            case "ToList": case "ToArray":
            {
                if (lazyIntoState) { R.Add("mechanical: a lazy sequence into a TF.State model property -> a List of it", Where(inv)); args = new List<IArgumentOperation> { null }; }
                var el = lazyIntoState ? ((INamedTypeSymbol)m.ReturnType).TypeArguments[0] : m.TypeArguments[0];
                // a type parameter (DeleteAll<T> where T : Entity): the game instantiation over its constraint (List<Entity>), as C#'s
                // generic sharing; an unconstrained reference T: object
                if (el is ITypeParameterSymbol etp && (etp.IsReferenceType || etp.ConstraintTypes.Any(c => c.IsReferenceType)))
                    el = etp.ConstraintTypes.FirstOrDefault(c => c.IsReferenceType) ?? ws.Compilation.GetSpecialType(SpecialType.System_Object);
                if (Escapes(inv) && !gen.Models.IsModel(el) && !IsNullable(el))
                {
                    // handed on (an argument, a field, a return): a managed array / game List<T>, as C# makes
                    var seq = lazyIntoState ? SeqOf(inv) : SeqOf(args[0].Value);
                    // an array of numbers (TF.EX's SkinStreamService: Parts.SelectMany(p => p).ToArray(), a bundle of kilobytes): counted,
                    // then filled, straight into the managed array (a snapshot holds only SnapCap elements)
                    if (toName == "ToArray" && el.SpecialType is SpecialType.System_Byte or SpecialType.System_Int32 or SpecialType.System_UInt32 or SpecialType.System_Int64 or SpecialType.System_Single or SpecialType.System_Boolean or SpecialType.System_Char)
                        return $"([&]() {{ auto q_ = {seq}; int32_t n_ = 0; q_([&]({Cpp(el)}) {{ n_++; return true; }}); auto a_ = gen::NewArr<{Cpp(el)}>({B.Need("t", SwitchName(ws.Compilation.CreateArrayTypeSymbol(el)))}, n_); int32_t i_ = 0; q_([&]({Cpp(el)} x_) {{ if (a_.data && i_ < n_) a_.data[i_++] = x_; return true; }}); return a_; }}())";
                    if (toName == "ToArray")
                        // (the array's runtime type as `new T[n]` makes it: a stand-in when the game never compiled T[], an array of tuples)
                        return $"([&]() {{ auto s_ = gen::ToSnap<{Cpp(el)}>({seq}); auto a_ = {NewArrOf(ws.Compilation.CreateArrayTypeSymbol(el), "s_.count")}; for (int i_ = 0; a_.data && i_ < s_.count; i_++) a_.data[i_] = s_.items[i_]; return a_; }}())";
                    var list = ws.Compilation.GetTypeByMetadataName("System.Collections.Generic.List`1")?.Construct(el);
                    if (list != null && IsValColl(list))   // a List<T> the game has no instance of (a TF.State enum): ours, as everywhere else
                        return $"([&]() {{ auto* v_ = gen::{NewValFn()}<{ValCpp(list)}>(); {seq}([&]({Cpp(el)} x_) {{ v_->Add(x_); return true; }}); return v_; }}())";
                    var ctor = list?.InstanceConstructors.FirstOrDefault(c => c.Parameters.Length == 0);
                    var add = list?.GetMembers("Add").OfType<IMethodSymbol>().FirstOrDefault(x => x.Parameters.Length == 1);
                    if (ctor != null && add != null)
                        return $"([&]() {{ void* l_ = {Call(ctor, "nullptr", new List<string>(), inv)}; {seq}([&]({Cpp(el)} x_) {{ {Call(add, "l_", new List<string> { "x_" }, inv)}; return true; }}); return l_; }}())";
                }
                return $"gen::ToSnap<{Cpp(el)}>({(lazyIntoState ? SeqOf(inv) : SeqOf(args[0].Value))})";
            }
            case "FirstOrDefault" when args.Count == 3:   // FirstOrDefault(pred, defaultValue) (.NET 6)
                return $"gen::FirstOrDefault<{Cpp(m.TypeArguments[0])}>({SeqOf(args[0].Value)}, {InlineLambda(args[1].Value)}, {Expr(args[2].Value)})";
            case "FirstOrDefault" when args.Count == 2:
                return $"gen::FirstOrDefault<{Cpp(m.TypeArguments[0])}>({SeqOf(args[0].Value)}, {InlineLambda(args[1].Value)}, {Literal(null, m.TypeArguments[0])})";
            case "Any" when args.Count == 2:
                return $"gen::Any({SeqOf(args[0].Value)}, {InlineLambda(args[1].Value)})";
            case "Count" when args.Count == 1:
                return $"gen::Count({SeqOf(args[0].Value)})";
            case "Count" when args.Count == 2:
                return $"gen::CountIf({SeqOf(args[0].Value)}, {InlineLambda(args[1].Value)})";
            case "Any" when args.Count == 1 && Strip(args[0].Value).Type is INamedTypeSymbol { OriginalDefinition.MetadataName: "Stack`1" or "Queue`1" or "HashSet`1" } cst && !IsValColl(cst):
                R.Add("mechanical: Any() of a game collection -> its Count > 0", cst.Name);   // (TF.State's AlarmPatch: the Alarm cache, a Stack<Alarm>)
                return $"({Call(cst.GetMembers("Count").OfType<IPropertySymbol>().First().GetMethod, Expr(Strip(args[0].Value)), new List<string>(), inv)} > 0)";
            case "Any" when args.Count == 1:
                return $"gen::AnyAt({SeqOf(args[0].Value)})";
            case "All" when args.Count == 2:
                return $"gen::All({SeqOf(args[0].Value)}, {InlineLambda(args[1].Value)})";
            case "ElementAtOrDefault" when args.Count == 2:
                return $"gen::ElementAtOrDefault<{Cpp(m.TypeArguments[0])}>({SeqOf(args[0].Value)}, {Expr(args[1].Value)}, {Literal(null, m.TypeArguments[0])})";
            case "ToDictionary" when args.Count == 3 && IsValColl(inv.Type):
            {   // a Dictionary the game has no code for: a gen::ValMap filled through the key and value selectors
                R.Add("rule: a collection the game has no code for -> a C++ container in scanned GC memory (gen::ValMap/ValSet/VList)", Where(inv));
                return $"([&]() {{ auto* v_ = gen::{NewValFn()}<{ValCpp(inv.Type)}>(); auto k_ = {InlineLambda(args[1].Value)}; auto e_ = {InlineLambda(args[2].Value)}; " +
                       $"{SeqOf(args[0].Value)}([&]({Cpp(m.TypeArguments[0])} x_) {{ v_->Set(k_(x_), e_(x_)); return true; }}); return v_; }}())";
            }
            case "ToDictionary" when args.Count == 3 && inv.Type is INamedTypeSymbol { OriginalDefinition.MetadataName: "Dictionary`2" } gdt &&
                                     gdt.InstanceConstructors.FirstOrDefault(c => c.Parameters.Length == 0) is IMethodSymbol gctor &&
                                     gdt.GetMembers("Add").OfType<IMethodSymbol>().FirstOrDefault(x => x.Parameters.Length == 2) is IMethodSymbol gadd:
            {   // a game Dictionary (TF.State's AddLayerState: the gameplay layer's actualDepthLookup ordered by key, ToDictionary): made with its
                // own constructor, each element Added through the key and value selectors
                R.Add("mechanical: LINQ ToDictionary(key, value) -> the game Dictionary, Add per element", Where(inv));
                return $"([&]() {{ void* d_ = {Call(gctor, "nullptr", new List<string>(), inv)}; auto k_ = {InlineLambda(args[1].Value)}; auto e_ = {InlineLambda(args[2].Value)}; " +
                       $"{SeqOf(args[0].Value)}([&]({Cpp(m.TypeArguments[0])} x_) {{ {Call(gadd, "d_", new List<string> { "k_(x_)", "e_(x_)" }, inv)}; return true; }}); return d_; }}())";
            }
            case "Contains" when args.Count == 2:
            {
                var et = m.TypeArguments[0];
                return $"gen::Contains<{Cpp(et)}>({SeqOf(args[0].Value)}, {Expr(args[1].Value)}, {EqOf(et)})";
            }
            case "Contains" when args.Count == 3 && m.TypeArguments[0].SpecialType == SpecialType.System_String &&
                                 Strip(args[2].Value) is IPropertyReferenceOperation { Property: { Name: "OrdinalIgnoreCase" or "Ordinal" or "InvariantCultureIgnoreCase" or "InvariantCulture", ContainingType.Name: "StringComparer" } } cmp:
            {   // Contains(value, StringComparer.X): ordinal, ignoring case or not (TF.EX's mod tags are ASCII)
                var eqI = cmp.Property.Name.EndsWith("IgnoreCase") ? "[](void* a, void* b) { return gen::StrEqI(a, b); }" : EqOf(m.TypeArguments[0]);
                return $"gen::Contains<void*>({SeqOf(args[0].Value)}, {Expr(args[1].Value)}, {eqI})";
            }
            case "Distinct" when args.Count == 1: case "Except" when args.Count == 2: case "Distinct" when args.Count == 2:
                return SeqOf(inv);   // a snapshot
            // ToHashSet(StringComparer.OrdinalIgnoreCase / Ordinal) over strings: a set whose Add / Contains compare that way (gen::StrKI)
            case "ToHashSet" when args.Count == 2 && m.TypeArguments[0].SpecialType == SpecialType.System_String &&
                                  Strip(args[1].Value) is IPropertyReferenceOperation { Property: { Name: "OrdinalIgnoreCase" or "Ordinal", ContainingType.Name: "StringComparer" } } hcmp:
            {
                var icase = hcmp.Property.Name == "Ordinal" ? "false" : "true";
                R.Add("rule: ToHashSet(StringComparer) over strings -> a gen::ValSet comparing that way (its ignoreCase flag)", Where(inv));
                return $"([&]() {{ auto* v_ = gen::{NewValFn()}<gen::ValSet<gen::StrK>>(); v_->ignoreCase = {icase}; {SeqOf(args[0].Value)}([&](void* x_) {{ v_->Add(x_); return true; }}); return v_; }}())";
            }
            case "ToHashSet" when args.Count == 1 && IsValColl(inv.Type):
            {   // a HashSet<T> the game has no code for: a gen::ValSet filled from the sequence
                R.Add("rule: a collection the game has no code for -> a C++ container in scanned GC memory (gen::ValMap/ValSet/VList)", Where(inv));
                return $"([&]() {{ auto* v_ = gen::{NewValFn()}<{ValCpp(inv.Type)}>(); {SeqOf(args[0].Value)}([&]({Cpp(m.TypeArguments[0])} x_) {{ v_->Add(x_); return true; }}); return v_; }}())";
            }
            case "First" when args.Count == 1: case "FirstOrDefault" when args.Count == 1:
                return $"gen::FirstAt<{Cpp(m.TypeArguments[0])}>({SeqOf(args[0].Value)}, {Literal(null, m.TypeArguments[0])})";
            case "SequenceEqual" when args.Count == 2:
                return $"gen::SequenceEqual<{Cpp(m.TypeArguments[0])}>({SeqOf(args[0].Value)}, {SeqOf(args[1].Value)}, {EqOf(m.TypeArguments[0])})";
            case "Empty" when args.Count == 0 && m.ReturnType is INamedTypeSymbol:
            {   // Enumerable.Empty<T>() (TF.State's model initializers: Layer.BackgroundElements, RoundLevels.Nexts): an empty game List<T>, or
                // List<object> for a type the game has no list of (the serializer's own representation: an empty array, not nil, in the bytes)
                var el = m.TypeArguments[0];
                var lt = ws.Compilation.GetTypeByMetadataName("System.Collections.Generic.List`1")!.Construct(el);
                if (GameLacks(lt) && el.IsReferenceType) lt = ws.Compilation.GetTypeByMetadataName("System.Collections.Generic.List`1")!.Construct(ws.Compilation.GetSpecialType(SpecialType.System_Object));
                if (GameLacks(lt)) break;
                R.Add("mechanical: Enumerable.Empty<T>() -> a new empty game List<T> (List<object> for a type the game has no list of)", Where(inv));
                return Call(lt.InstanceConstructors.First(c => c.Parameters.Length == 0), "nullptr", new List<string>(), inv);
            }
            case "First" when args.Count == 2:
            case "Single" when args.Count == 2: case "SingleOrDefault" when args.Count == 2:   // the one match (C#'s throw on two or, for Single, none: not checked)
                return $"gen::FirstOrDefault<{Cpp(m.TypeArguments[0])}>({SeqOf(args[0].Value)}, {InlineLambda(args[1].Value)}, {Literal(null, m.TypeArguments[0])})";
            case "Last" when args.Count == 1: case "LastOrDefault" when args.Count == 1:
                return $"gen::LastOrDefault<{Cpp(m.TypeArguments[0])}>({SeqOf(args[0].Value)}, [](auto) {{ return true; }}, {Literal(null, m.TypeArguments[0])})";
            case "LastOrDefault" when args.Count == 2: case "Last" when args.Count == 2:
                return $"gen::LastOrDefault<{Cpp(m.TypeArguments[0])}>({SeqOf(args[0].Value)}, {InlineLambda(args[1].Value)}, {Literal(null, m.TypeArguments[0])})";
            case "Max" when args.Count == 2: case "Min" when args.Count == 2:
                return $"gen::{m.Name}Of<{Cpp(m.ReturnType)}>({SeqOf(args[0].Value)}, {InlineLambda(args[1].Value)}, {Literal(null, m.ReturnType)})";
            case "Max" when args.Count == 1: case "Min" when args.Count == 1:
                return $"gen::{m.Name}Of<{Cpp(m.ReturnType)}>({SeqOf(args[0].Value)}, [](auto x) {{ return x; }}, {Literal(null, m.ReturnType)})";
            case "Sum" when args.Count == 2:
                return $"gen::SumOf<{Cpp(m.ReturnType)}>({SeqOf(args[0].Value)}, {InlineLambda(args[1].Value)})";
            case "OrderBy": case "OrderByDescending": case "Order": case "Select": case "Where": case "OfType": case "Take": case "Skip": case "Cast": case "Reverse":
            case "Range":   // (TF.State's CalcExtensions.OwnShuffledIndexes: OwnShuffle(Enumerable.Range(0, count)))
                return SeqOf(inv);   // a lazy sequence used as a value (passed on, or enumerated by foreach)
        }
        return Unsupported(inv, "LINQ " + m.Name);
    }

    /* dict.Single(kv => kv.Key == k).Value is dict[k] (keys are unique: the same element; C#'s exception when absent isn't kept) */
    string DictSingleValue(IPropertyReferenceOperation p)
    {
        if (p.Property.Name != "Value" || p.Instance is not IInvocationOperation inv || !IsEnumerable(inv.TargetMethod) || inv.TargetMethod.Name != "Single") return null;
        var args = inv.Arguments.OrderBy(a => a.Parameter.Ordinal).ToList();
        if (args.Count != 2) return null;
        var src = args[0].Value;
        while (src is IConversionOperation c) src = c.Operand;
        if (src.Type is not INamedTypeSymbol dt || dt.OriginalDefinition.ToDisplayString() != "System.Collections.Generic.Dictionary<TKey, TValue>") return null;
        var lam = args[1].Value;
        while (lam is IConversionOperation c2) lam = c2.Operand;
        if (lam is not IDelegateCreationOperation { Target: IAnonymousFunctionOperation af }) return null;
        var ret = af.Body.Operations.OfType<IReturnOperation>().FirstOrDefault()?.ReturnedValue as IBinaryOperation;
        if (ret == null || ret.OperatorKind != BinaryOperatorKind.Equals) return null;
        bool IsKey(IOperation o) => o is IPropertyReferenceOperation { Property.Name: "Key", Instance: IParameterReferenceOperation };
        var key = IsKey(ret.LeftOperand) ? ret.RightOperand : IsKey(ret.RightOperand) ? ret.LeftOperand : null;
        if (key == null) return null;
        R.Add("mechanical: LINQ dict.Single(kv => kv.Key == k).Value -> dict[k]", Where(p));
        var indexer = dt.GetMembers().OfType<IPropertySymbol>().First(x => x.IsIndexer).GetMethod;
        return Call(indexer, Expr(src), new List<string> { Expr(key) }, p);
    }

    /* ---------------------------------------------------------------- Traverse (Harmony) ---------------------------------------------------------------- */
    /* Traverse.Create(x).Field("n") / .Field<T>("n"): (x, its type, "n") */
    /* the object a DynamicData / Traverse value stands for, by its static type: DynamicData.For(x) / Traverse.Create(x) -> x's type; a local
     * -> its initializer; a parameter -> every call site of the method in this type (all must agree) */
    ITypeSymbol WrappedType(IOperation op, int depth = 0)
    {
        op = Strip(op);
        if (op == null || depth > 6) return null;
        if (op is IInvocationOperation inv && inv.TargetMethod.Name is "For" or "Create" && inv.Arguments.Length == 1 &&
            (IsDynamicData(inv.TargetMethod.ContainingType) || inv.TargetMethod.ContainingType.ToDisplayString() == "HarmonyLib.Traverse"))
            return Strip(inv.Arguments[0].Value).Type;
        if (op is IObjectCreationOperation oc && IsDynamicData(oc.Type) && oc.Arguments.Length == 1)   // DynData<T>: its members are T's
            return oc.Type is INamedTypeSymbol { TypeArguments.Length: 1 } dt1 ? dt1.TypeArguments[0] : Strip(oc.Arguments[0].Value).Type;
        // c ? DynamicData.For(x) : null, and dyn?.Get(...): the wrapped object of the branch / of the tested value (TF.State's AddSessionState)
        if (op is IConditionalOperation co) return WrappedType(co.WhenTrue, depth + 1) ?? (co.WhenFalse != null ? WrappedType(co.WhenFalse, depth + 1) : null);
        if (op is IConditionalAccessInstanceOperation cai)
            for (var up = cai.Parent; up != null; up = up.Parent)
                if (up is IConditionalAccessOperation ca) return WrappedType(ca.Operation, depth + 1);
        if (op is ILocalReferenceOperation l)
        {
            if (dynLocals.TryGetValue(l.Local, out var dt)) return dt;
            var decl = l.Local.DeclaringSyntaxReferences.FirstOrDefault()?.GetSyntax() as Microsoft.CodeAnalysis.CSharp.Syntax.VariableDeclaratorSyntax;
            return decl?.Initializer != null && decl.SyntaxTree == model.SyntaxTree ? WrappedType(M(decl.Initializer.Value).GetOperation(decl.Initializer.Value), depth + 1) : null;
        }
        if (op is IParameterReferenceOperation pr && pr.Parameter.ContainingSymbol is IMethodSymbol pm)
        {
            ITypeSymbol found = null;
            foreach (var r in type.DeclaringSyntaxReferences)
                foreach (var call in r.GetSyntax().DescendantNodes().OfType<Microsoft.CodeAnalysis.CSharp.Syntax.InvocationExpressionSyntax>())
                {
                    if (r.SyntaxTree != model.SyntaxTree || M(call).GetOperation(call) is not IInvocationOperation ci || !SymbolEqualityComparer.Default.Equals(ci.TargetMethod.OriginalDefinition, pm.OriginalDefinition)) continue;
                    var arg = ci.Arguments.FirstOrDefault(a => a.Parameter?.Ordinal == pr.Parameter.Ordinal);
                    var t = arg == null ? null : WrappedType(arg.Value, depth + 1);
                    if (t == null || (found != null && !SymbolEqualityComparer.Default.Equals(found, t))) return null;
                    found = t;
                }
            return found;
        }
        return null;
    }

    (string recv, ITypeSymbol t, string member)? TraverseField(IOperation op)
    {
        while (op is IConversionOperation c && c.IsImplicit) op = c.Operand;
        if (op is not IInvocationOperation f || f.TargetMethod.Name is not ("Field" or "Property") || f.Arguments.Length is < 1 or > 2) return null;   // .Property(n): the same lookup (fields and properties)
        var fargs = f.Arguments.OrderBy(a => a.Parameter.Ordinal).ToList();
        if (fargs.Count == 2 && !(fargs[1].ArgumentKind == ArgumentKind.DefaultValue || fargs[1].Value.ConstantValue is { HasValue: true, Value: null })) return null;   // Property(n, index): an indexer
        if (fargs[0].Value.ConstantValue.Value is not string n) return null;
        var create = f.Instance;
        while (create is IConversionOperation c2 && c2.IsImplicit) create = c2.Operand;
        if (create is IInvocationOperation ci && ci.TargetMethod.Name == "Create")
        {
            var target = ci.Arguments[0].Value;
            while (target is IConversionOperation c3 && c3.IsImplicit) target = c3.Operand;
            return (Expr(target), target.Type, n);
        }
        // a Traverse held in a local / parameter (var t = Traverse.Create(x); t.Field<int>("i").Value): the local holds x
        if (create is ILocalReferenceOperation or IParameterReferenceOperation && WrappedType(create) is ITypeSymbol wt) return (Expr(create), wt, n);
        return null;
    }
    /* every field / property named n on a game subclass of t */
    List<ISymbol> SubclassMembers(ITypeSymbol t, string n)
    {
        if (t is not INamedTypeSymbol nt || nt.ContainingAssembly == null) return new List<ISymbol>();
        IEnumerable<INamedTypeSymbol> All(INamespaceSymbol ns) => ns.GetTypeMembers().Concat(ns.GetNamespaceMembers().SelectMany(All));
        // (by name too: a code mod's class is another compilation's, its TowerFall.RoundLogic another symbol of the same game type)
        bool Derives(INamedTypeSymbol x) { for (var b = x.BaseType; b != null; b = b.BaseType) if (SymbolEqualityComparer.Default.Equals(b, nt) || (b.ToDisplayString() == nt.ToDisplayString() && b.ContainingAssembly?.Name == nt.ContainingAssembly?.Name)) return true; return false; }
        // the game's own subclasses, then the classes of ours a configured unit makes as real subclasses of it (TowerFall.RoundLogic ->
        // TF.EX's NetplayRoundLogic: TF.State's DynamicData.For(roundLogic).Get("roundEndCounter") reads its field on the PC too)
        var types = All(nt.ContainingAssembly.GlobalNamespace).Where(Derives).ToList();
        if (OriginOf(nt) != Origin.Tfex)
            types.AddRange(gen.ConfiguredClassUnits().Where(x => Derives(x) && OtherUnit(x) is TomlTable u && !u.ContainsKey("hosted") && !u.ContainsKey("members")));
        return types.Select(x => x.GetMembers(n).FirstOrDefault(y => y is IFieldSymbol or IPropertySymbol)).Where(y => y != null).Distinct(SymbolEqualityComparer.Default).ToList();
    }
    /* a field by name on its declaring class: the game's by offset, a class of ours (a unit's Self) by its member */
    string FieldOf(string recv, IFieldSymbol fs)
    {
        if (!fs.IsStatic && OriginOf(fs.ContainingType) == Origin.Tfex && OtherUnit(fs.ContainingType) is TomlTable u && !u.ContainsKey("hosted"))
        {
            if (SymbolEqualityComparer.Default.Equals(fs.ContainingType, type) && isClass) return $"S({recv})->{Id(fs.Name)}";
            includes.Add($"tfexgen_{u["name"]}.hpp");
            return $"static_cast<tfex::gen::{u["name"]}::Self*>({recv})->{Id(fs.Name)}";
        }
        return FieldByName(recv, fs);
    }
    /* a field / property named n on exactly one game subclass of t (Traverse resolves by the object's runtime type) */
    ISymbol SubclassMember(ITypeSymbol t, string n, IOperation at = null)
    {
        if (t is not INamedTypeSymbol nt || nt.ContainingAssembly == null) return null;
        var hits = SubclassMembers(t, n);
        if (hits.Count == 1) return hits[0];
        if (hits.Count > 1 && at != null)
        {   // several subclasses have it (HUD -> VersusMatchResults.finished, VersusRoundResults.finished ...): the one the enclosing code
            // tests the object for (`__instance is VersusMatchResults`); the read checks the type at run time anyway
            var body = at.Syntax.Ancestors().OfType<Microsoft.CodeAnalysis.CSharp.Syntax.BaseMethodDeclarationSyntax>().FirstOrDefault();
            if (body != null)
            {
                var sm = ws.Model(body.SyntaxTree);
                var tested = body.DescendantNodes().Select(x => x switch
                {
                    Microsoft.CodeAnalysis.CSharp.Syntax.BinaryExpressionSyntax { RawKind: (int)Microsoft.CodeAnalysis.CSharp.SyntaxKind.IsExpression or (int)Microsoft.CodeAnalysis.CSharp.SyntaxKind.AsExpression } be => sm.GetTypeInfo(be.Right).Type,
                    Microsoft.CodeAnalysis.CSharp.Syntax.DeclarationPatternSyntax dp => sm.GetTypeInfo(dp.Type).Type,
                    Microsoft.CodeAnalysis.CSharp.Syntax.TypePatternSyntax tp => sm.GetTypeInfo(tp.Type).Type,
                    _ => null,
                }).Where(x => x != null).ToList();
                var narrowed = hits.Where(h => tested.Any(x => SymbolEqualityComparer.Default.Equals(x, h.ContainingType))).ToList();
                if (narrowed.Count == 1) { R.Add("mechanical: Traverse member on several subclasses -> the one the code tests for", $"{narrowed[0].ContainingType.Name}.{n} of {string.Join(", ", hits.Select(h => h.ContainingType.Name))}"); return narrowed[0]; }
            }
        }
        if (hits.Count > 1) R.Add("note: Traverse member on several subclasses, none tested for (default value)", $"{nt.Name}.{n}: {string.Join(", ", hits.Select(h => h.ContainingType.Name))}");
        return null;
    }
    ISymbol MemberByName(ITypeSymbol t, string n)
    {
        for (var c = t as INamedTypeSymbol; c != null; c = c.BaseType)
            if (c.GetMembers(n).FirstOrDefault(x => x is IFieldSymbol or IPropertySymbol) is ISymbol s) return s;
        return null;
    }
    /* a game field reached by name (Traverse / DynamicData): a static one is its storage (after its class constructor; Traverse and
     * DynamicData find statics through an instance too: TF.State's AlarmPatch / BramblesPatch read the pools `cached`), an instance one
     * is at its offset in recv. An lvalue either way. */
    string FieldByName(string recv, IFieldSymbol fs) => fs.IsStatic
        ? $"(gen::Cctor({B.Need("c", SwitchName(fs.ContainingType))}), gen::Static<{Cpp(fs.Type)}>({B.Need("f", SwitchName(fs.ContainingType), fs.Name)}))"
        : $"gen::Fld<{Cpp(fs.Type)}>({recv}, {B.Need("f", SwitchName(fs.ContainingType), fs.Name)})";
    string ReadMember(string recv, ISymbol member, IOperation at) => member switch
    {
        IFieldSymbol fs => FieldOf(recv, fs),
        IPropertySymbol ps when ps.GetMethod != null => Call(ps.GetMethod, recv, new List<string>(), at),
        _ => Unsupported(at, "member by name"),
    };
    /* System.Reflection over a game field: typeof(T).GetField("n", flags) (inline, or held in a local) -> T's field n; FieldInfo.GetValue(obj) /
     * SetValue(obj, v) on it -> the field itself, as Traverse (TF.State's MonoclePatch reads Monocle.Audio's private static pitchList) */
    readonly Dictionary<ILocalSymbol, IFieldSymbol> reflFieldLocals = new(SymbolEqualityComparer.Default);
    IFieldSymbol ReflectedField(IOperation op)
    {
        op = Strip(op);
        if (op is ILocalReferenceOperation l) return reflFieldLocals.TryGetValue(l.Local, out var lf) ? lf : null;
        // a static field holding one, assigned once (FieldInit: its initializer or its one assignment)
        if (op is IFieldReferenceOperation { Field: { Type.Name: "FieldInfo" } hf } && FieldInit(hf) is IOperation hfi && hfi is not IFieldReferenceOperation) return ReflectedField(hfi);
        // AccessTools.Field / DeclaredField(typeof(T), "n")
        if (op is IInvocationOperation { TargetMethod: { Name: "Field" or "DeclaredField", ContainingType: var at } } af && at?.ToDisplayString() == "HarmonyLib.AccessTools" && af.Arguments.Length == 2)
        {
            var aa = af.Arguments.OrderBy(a => a.Parameter.Ordinal).ToList();
            return Strip(aa[0].Value) is ITypeOfOperation ato && aa[1].Value.ConstantValue.Value is string an ? MemberByName(ato.TypeOperand, an) as IFieldSymbol : null;
        }
        if (op is not IInvocationOperation { TargetMethod: { Name: "GetField", ContainingType: var ct } } gf || ct.ToDisplayString() != "System.Type" || gf.Arguments.Length < 1) return null;
        if (Strip(gf.Instance) is not ITypeOfOperation to || gf.Arguments.OrderBy(a => a.Parameter.Ordinal).First().Value.ConstantValue.Value is not string n) return null;
        return MemberByName(to.TypeOperand, n) as IFieldSymbol;
    }
    /* a reflection call's FieldInfo: its instance, or, under `fi?.GetValue(x)`, the conditional access's operand (AINova's
     * endCounter?.GetValue(me) over a static field assigned AccessTools.Field once, with ??=) */
    IOperation ReflectionInstance(IInvocationOperation inv)
    {
        if (inv.Instance is IConditionalAccessInstanceOperation)
            for (var up = inv.Parent; up != null; up = up.Parent) if (up is IConditionalAccessOperation ca) return ca.Operation;
        return inv.Instance;
    }
    /* the field a `fi.GetValue(x)` (or `fi?.GetValue(x)`) reads, resolved at translate time, and the read itself; else null */
    (IFieldSymbol fs, string read)? ReflectedFieldRead(IOperation op)
    {
        op = Strip(op);
        if (op is IConditionalAccessOperation ca && ReflectedField(ca.Operation) != null) op = Strip(ca.WhenNotNull);
        if (op is not IInvocationOperation inv || inv.TargetMethod.ContainingType.ToDisplayString() != "System.Reflection.FieldInfo" || inv.TargetMethod.Name != "GetValue" ||
            ReflectedField(ReflectionInstance(inv)) is not IFieldSymbol fs) return null;
        var recv = fs.IsStatic ? "nullptr" : Expr(Strip(inv.Arguments.OrderBy(a => a.Parameter.Ordinal).First().Value));
        return (fs, FieldByName(recv, fs));
    }
    string ReflectionFieldCall(IInvocationOperation inv)
    {
        var m = inv.TargetMethod;
        if (m.ContainingType.ToDisplayString() != "System.Reflection.FieldInfo" || m.Name is not ("GetValue" or "SetValue") || ReflectedField(ReflectionInstance(inv)) is not IFieldSymbol fs) return null;
        var args = inv.Arguments.OrderBy(a => a.Parameter.Ordinal).ToList();
        var recv = fs.IsStatic ? "nullptr" : Expr(Strip(args[0].Value));
        R.Add("mechanical: reflection FieldInfo.GetValue / SetValue of typeof(T).GetField(\"n\") -> the field (as Traverse)", $"{fs.ContainingType.Name}.{fs.Name}");
        return m.Name == "GetValue" ? FieldByName(recv, fs) : $"{FieldByName(recv, fs)} = {Expr(args[1].Value)}";
    }
    string TraverseCall(IInvocationOperation inv)
    {
        var m = inv.TargetMethod;
        if (m.Name is "GetValue" or "SetValue" && TraverseField(inv.Instance) is var (recv, t, n))
        {
            var member = MemberByName(t, n);
            if (member == null && SubclassMember(t, n, inv) is ISymbol sm)
            {   // Traverse looks at the object's own type: a subclass's field (HUD -> VersusMatchResults.finished), when the object is one
                R.Add("mechanical: Harmony Traverse member of a subclass, by the object's type", $"{sm.ContainingType.Name}.{n}");
                var sub = TypeOfAny(sm.ContainingType);
                if (m.Name == "GetValue") return $"(gen::As({recv}, {sub}) ? {ReadMember(recv, sm, inv)} : {Literal(null, m.ReturnType)})";
                var v0 = Expr(inv.Arguments[0].Value);
                return sm is IFieldSymbol sfs ? $"(void)(gen::As({recv}, {sub}) ? (gen::Fld<{Cpp(sfs.Type)}>({recv}, {B.Need("f", SwitchName(sfs.ContainingType), sfs.Name)}) = {v0}, 0) : 0)"
                                              : Unsupported(inv, "Traverse set of a subclass property");
            }
            if (member == null)
            {   // no such member anywhere: Traverse reads null (the default) and its write does nothing (TF.EX's VersusMatchResults.HasReset)
                R.Add("mechanical: Harmony Traverse member that exists nowhere -> default / no-op (Traverse's own result)", $"{t.Name}.{n}");
                return m.Name == "GetValue" ? Literal(null, m.ReturnType) : "((void)0)";
            }
            R.Add("mechanical: Harmony Traverse member by name", $"{member.ContainingType.Name}.{n}");
            if (m.Name == "GetValue") return ReadMember(recv, member, inv);
            var v = Expr(inv.Arguments[0].Value);
            return member switch
            {
                IFieldSymbol fs => $"{FieldByName(recv, fs)} = {v}",
                IPropertySymbol ps when ps.SetMethod != null => Call(ps.SetMethod, recv, new List<string> { v }, inv),
                _ => Unsupported(inv, "Traverse set"),
            };
        }
        if (m.Name == "Create" && inv.Arguments.Length == 1) return Expr(Strip(inv.Arguments[0].Value));   // a Traverse value: the object it wraps
        return Unsupported(inv, "Traverse." + m.Name);
    }
    string TraverseValue(IInvocationOperation fieldCall, IOperation at)
    {
        if (TraverseField(fieldCall) is not var (recv, t, n)) return Unsupported(at, "Traverse<T>.Value");
        var member = MemberByName(t, n);
        if (member == null) return Unsupported(at, $"Traverse member {n} not found on {t}");
        R.Add("mechanical: Harmony Traverse member by name", $"{member.ContainingType.Name}.{n}");
        return ReadMember(recv, member, at);
    }

    /* ---------------------------------------------------------------- DynamicData ---------------------------------------------------------------- */
    // MonoMod's DynamicData, and its generic DynData<T> (TF.State's platform extensions: `using var dynSolid = new DynData<Solid>(entity)`): the
    // same Get / Set by name on the wrapped object's type
    static bool IsDynamicData(ITypeSymbol t) => t?.ToDisplayString() == "MonoMod.Utils.DynamicData" || (t as INamedTypeSymbol)?.OriginalDefinition.ToDisplayString() == "MonoMod.Utils.DynData<TTarget>";
    /* the wrapped object's type when every assignment of a DynamicData local in its method is DynamicData.For over one type, else null */
    ITypeSymbol DynAssignedType(ILocalSymbol local)
    {
        var body = local.DeclaringSyntaxReferences.FirstOrDefault()?.GetSyntax().FirstAncestorOrSelf<Microsoft.CodeAnalysis.CSharp.Syntax.BaseMethodDeclarationSyntax>();
        if (body == null) return null;
        ITypeSymbol t = null;
        foreach (var a in body.DescendantNodes().OfType<Microsoft.CodeAnalysis.CSharp.Syntax.AssignmentExpressionSyntax>())
        {
            if (model.GetOperation(a) is not ISimpleAssignmentOperation { Target: ILocalReferenceOperation lr } sa || !SymbolEqualityComparer.Default.Equals(lr.Local, local)) continue;
            if (!IsDynamicDataFor(sa.Value, out var tg) || (t != null && !SymbolEqualityComparer.Default.Equals(t, tg.Type))) return null;
            t = tg.Type;
        }
        return t;
    }
    bool IsDynamicDataFor(IOperation op, out IOperation target)
    {
        target = null;
        while (op is IConversionOperation c && c.IsImplicit) op = c.Operand;
        if (op is IInvocationOperation inv && IsDynamicData(inv.TargetMethod.ContainingType) && inv.TargetMethod.Name == "For")
        {
            target = inv.Arguments[0].Value;
            while (target is IConversionOperation c2 && c2.IsImplicit) target = c2.Operand;
            return true;
        }
        if (op is IObjectCreationOperation oc && IsDynamicData(oc.Type) && oc.Arguments.Length == 1)   /* new DynData<T>(x) */
        {
            target = oc.Arguments[0].Value;
            while (target is IConversionOperation c3 && c3.IsImplicit) target = c3.Operand;
            return true;
        }
        return false;
    }

    /* DynamicData.For(x).Get<T>("name") / .Set("name", v): the member by name on x's type (private ones included: our compilation
     * imports them from TowerFall.exe; the binder or a [pin] gives the offset) */
    string DynamicDataCall(IInvocationOperation inv)
    {
        var m = inv.TargetMethod;
        if (m.Name == "For") return Expr(inv.Arguments[0].Value);
        // DynamicData.Dispose(): drops MonoMod's weak-table entry; nothing is kept per object here (DynamicData is the object itself)
        if (m.Name == "Dispose" && inv.Arguments.Length == 0) return "(void)0";
        string recv; ITypeSymbol t;
        if (inv.Instance is ILocalReferenceOperation l && dynLocals.TryGetValue(l.Local, out t)) recv = Id(l.Local.Name);
        else if (inv.Instance is IParameterReferenceOperation or ILocalReferenceOperation or IConditionalAccessInstanceOperation && WrappedType(inv.Instance) is ITypeSymbol wt0) { recv = Expr(inv.Instance); t = wt0; }
        else if (inv.Instance != null && IsDynamicDataFor(inv.Instance, out var target)) { recv = Expr(target); t = target.Type; }
        else return Unsupported(inv, "DynamicData on an unknown object");
        // a generic method's T (TF.State's Level.Spawn<T>: Scene, Level, actualDepth): the instantiated type, else T's constraint
        if (t is ITypeParameterSymbol && subst.Count > 0) t = Sub(t);
        if (t is ITypeParameterSymbol tp && tp.ConstraintTypes.FirstOrDefault() is ITypeSymbol ct) t = ct;
        var args = inv.Arguments.OrderBy(a => a.Parameter.Ordinal).ToList();
        var memberName = args[0].Value.ConstantValue.Value as string ?? (Strip(args[0].Value) is ILocalReferenceOperation { Local: var cl } && constLocals.TryGetValue(cl, out var cv) ? cv : null);
        if (memberName == null) return Unsupported(inv, "DynamicData with a computed member name");
        if (m.Name == "Invoke")   // DynamicData.For(x).Invoke("Name", a, b): the game method by name and argument count (params object[])
        {
            var rest = args.Skip(1).Select(a => Strip(a.Value)).ToList();
            if (rest.Count == 1 && rest[0] is IArrayCreationOperation { Initializer: var pi } && args[1].ArgumentKind == ArgumentKind.ParamArray)
                rest = pi?.ElementValues.Select(Strip).ToList() ?? new List<IOperation>();
            else if (args.Count > 1 && args[1].ArgumentKind == ArgumentKind.ParamArray && args[1].Value is IArrayCreationOperation { Initializer: var pi2 })
                rest = pi2?.ElementValues.Select(Strip).ToList() ?? new List<IOperation>();
            rest = rest.Select(r => { while (r is IConversionOperation c) r = c.Operand; return r; }).ToList();
            IMethodSymbol target = null;
            for (var c = t as INamedTypeSymbol; c != null && target == null; c = c.BaseType)
                target = c.GetMembers(memberName).OfType<IMethodSymbol>().FirstOrDefault(x => x.Parameters.Length == rest.Count && !x.IsStatic);
            if (target == null) return Unsupported(inv, $"DynamicData method {memberName}/{rest.Count} not found on {t}");
            if (DynamicSkip(target, inv) is string dsk) return dsk;
            R.Add("mechanical: DynamicData method by name", $"{target.ContainingType.Name}.{memberName}/{rest.Count}");
            var cargs = rest.Select((r, i) => $"({Cpp(target.Parameters[i].Type)})({Expr(r)})").ToList();
            var call = Call(target, recv, cargs, inv);
            return target.ReturnsVoid || m.ReturnsVoid ? call : call;
        }
        ISymbol member = null;
        for (var c = t as INamedTypeSymbol; c != null && member == null; c = c.BaseType)
            member = c.GetMembers(memberName).FirstOrDefault(x => x is IPropertySymbol or IFieldSymbol);
        if (member != null && gen.AbsentRule(member) is TomlTable absent && absent.TryGetValue("side", out var side) && side is true && m.Name is "Get" or "Set")
        {   // a PC member the Switch's class lacks, which the Switch's own code supplies per object: DynamicData's side table under its name
            var mt = member is IFieldSymbol fs0 ? fs0.Type : ((IPropertySymbol)member).Type;
            var vt = m.Name == "Get" ? (m.TypeArguments.Length == 1 ? m.TypeArguments[0] : mt) : Strip(args[1].Value).Type ?? mt;   // (a null literal: the member's type)
            R.Add("hand rule: a PC member the Switch lacks, kept in the side table ([fortrise] side = true)", $"{member.ContainingType.Name}.{memberName}");
            if (m.Name == "Set") return vt.IsReferenceType ? $"gen::SideSet<void*>(\"{memberName}\", {recv}, {Expr(args[1].Value)})" : $"gen::SideSet<{Cpp(vt)}>(\"{memberName}\", {recv}, {Expr(args[1].Value)})";
            return vt.IsReferenceType ? $"gen::As(gen::SideGet<void*>(\"{memberName}\", {recv}), {TypeOfAny(vt)})" : $"gen::SideGet<{Cpp(vt)}>(\"{memberName}\", {recv})";
        }
        // a member with a [service] rule (FortRise's VersusModeButton.currentIndex, read by name by TF.EX's VersusModeButtonPatch: the
        // translated FortRise unit's static): its rule, get (cpp) or set
        if (member != null && gen.ServiceRule(member) is TomlTable dsr && m.Name is "Get" or "Set")
        {
            R.Add("hand rule: a member read by name (DynamicData) -> its [service] rule", $"{member.ContainingType.Name}.{memberName}");
            if (m.Name == "Set" && dsr.TryGetValue("set", out var dset) && dset is string dsets) return dsets.Replace("$value", Expr(args[1].Value)).Replace("$self", recv);
            if (m.Name == "Get" && dsr.TryGetValue("cpp", out var dget) && dget is string dgets) { if (dsr.TryGetValue("include", out var dinc) && dinc is string dincs) includes.Add(dincs); return dgets.Replace("$self", recv); }
        }
        if (member != null && gen.AbsentRule(member) != null)
        {
            R.Add("hand rule: FortRise member absent on the Switch ([fortrise], DynamicData)", $"{member.ContainingType.Name}.{memberName}");
            return m.Name == "Set" ? $"(void)({Expr(args[1].Value)})" : Literal(null, m.ReturnType);
        }
        if (member == null && m.Name is "Get" or "Set" && (m.Name != "Get" || m.TypeArguments.Length == 1) && SubclassMembers(t, memberName) is { Count: > 0 } subs
            && subs.All(x => x is IFieldSymbol { Type: not IArrayTypeSymbol } || x is IPropertySymbol { GetMethod: not null } && m.Name == "Get"))
        {   // DynamicData looks the name up on the object's own type: a subclass's field (Pickup -> ShieldPickup.sprite, BombPickup.image),
            // tested at run time like DynamicData does; an object of none of them: the side table, as for TF.EX's own state
            var vt = m.Name == "Get" ? m.TypeArguments[0] : Strip(args[1].Value).Type;
            var vc = Cpp(vt);
            R.Add("mechanical: DynamicData member of a subclass, by the object's type", $"{t.Name}.{memberName}: {string.Join(", ", subs.Select(x => x.ContainingType.Name))}");
            if (m.Name == "Get")
            {
                var chain = $"gen::SideGet<{vc}>(\"{memberName}\", {recv})";
                foreach (var sm in Enumerable.Reverse(subs))
                    chain = $"(gen::As({recv}, {TypeOfAny(sm.ContainingType)}) ? ({vc})({ReadMember(recv, sm, inv)}) : {chain})";
                return chain;
            }
            var sv = Expr(args[1].Value);
            var schain = $"gen::SideSet<{vc}>(\"{memberName}\", {recv}, v_)";
            foreach (var sm in Enumerable.Reverse(subs))
            {
                var sf = (IFieldSymbol)sm;
                schain = $"if (gen::As({recv}, {TypeOfAny(sf.ContainingType)})) {(OriginOf(sf.ContainingType) == Origin.Tfex ? FieldOf(recv, sf) : $"gen::Fld<{Cpp(sf.Type)}>({recv}, {B.Need("f", SwitchName(sf.ContainingType), sf.Name)})")} = v_; else {{ {schain}; }}";
            }
            return $"([&]() {{ auto v_ = {sv}; {schain}; }}())";
        }
        if (member == null && m.Name is "Set" or "Add" or "Get" && (m.Name != "Get" || m.TypeArguments.Length <= 1))
        {   // TF.EX's own state on a game object (a member its class doesn't have): the side table. A plain Get("x") is an object: the
            // entry as a reference (TF.State's Lantern.LoadState: `dynLantern.Get("homeChain") is Chain`, set by its LanternPatch)
            var vt = m.Name == "Get" ? (m.TypeArguments.Length == 1 ? m.TypeArguments[0] : ws.Compilation.GetSpecialType(SpecialType.System_Object)) : Strip(args[1].Value).Type;
            while (args.Count > 1 && m.Name != "Get" && args[1].Value is IConversionOperation bc && bc.Operand.Type != null) { vt = bc.Operand.Type; break; }
            var vc = vt == null ? null : Cpp(vt);
            if (vc == null || vc.StartsWith("gen::Arr") || vc.StartsWith("auto") || vc.Contains("ws::") && !vc.EndsWith("*")) return Unsupported(inv, $"DynamicData member {memberName} not found on {t}");
            R.Add("rule: DynamicData state TF.EX adds to a game object -> the side table (gen::SideGet/SideSet)", $"{t.Name}.{memberName}");
            if (m.Name == "Get") return $"gen::SideGet<{vc}>(\"{memberName}\", {recv})";
            var sv = args[1].Value is IConversionOperation bc2 && bc2.Operand.Type != null ? Expr(bc2.Operand) : Expr(args[1].Value);
            return $"gen::SideSet<{vc}>(\"{memberName}\", {recv}, {sv})";
        }
        if (member == null && m.Name == "TryGet" && m.TypeArguments.Length == 1 && args.Count == 2)
        {   // dyn.TryGet("NAME", out T v) of TF.EX's own state (TF.State's CoroutinePatch: the coroutine's name, set at its constructor):
            // the side table's entry, when there is one
            var vc = Cpp(m.TypeArguments[0]);
            if (vc.StartsWith("gen::Arr") || vc.StartsWith("auto") || vc.Contains("ws::") && !vc.EndsWith("*")) return Unsupported(inv, $"DynamicData member {memberName} not found on {t}");
            R.Add("rule: DynamicData state TF.EX adds to a game object -> the side table (gen::SideGet/SideSet)", $"{t.Name}.{memberName} (TryGet)");
            return $"([&]() {{ auto o_ = gen::SideGetOpt<{vc}>(\"{memberName}\", {recv}); if (!o_.has) return false; {LValue(args[1].Value)} = o_.value; return true; }}())";
        }
        if (member == null) return Unsupported(inv, $"DynamicData member {memberName} not found on {t}");
        R.Add("mechanical: DynamicData member by name", $"{member.ContainingType.Name}.{memberName} ({m.Name})");
        if (m.Name == "Set")
        {
            var v = Expr(args[1].Value);
            if (member is IPropertySymbol ps && ps.SetMethod != null) return Call(ps.SetMethod, recv, new List<string> { v }, inv);
            if (member is IFieldSymbol afs && afs.Type is IArrayTypeSymbol) return $"gen::Fld<void*>({recv}, {B.Need("f", SwitchName(afs.ContainingType), afs.Name)}) = ({ArrValue(args[1].Value)}).obj";
            if (member is IFieldSymbol fs) return $"{FieldByName(recv, fs)} = {v}";
        }
        else if (m.Name == "Get")
        {
            if (member is IPropertySymbol ps && ps.GetMethod != null) return Call(ps.GetMethod, recv, new List<string>(), inv);
            if (member is IFieldSymbol afg && afg.Type is IArrayTypeSymbol)   // a game array field: the managed array, wrapped (as a direct read)
                return GameArray(afg.Type, $"gen::Fld<void*>({recv}, {B.Need("f", SwitchName(afg.ContainingType), afg.Name)})");
            if (member is IFieldSymbol fs) return FieldByName(recv, fs);
        }
        return Unsupported(inv, "DynamicData." + m.Name);
    }
}
