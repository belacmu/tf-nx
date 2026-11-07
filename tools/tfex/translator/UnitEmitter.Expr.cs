using System.Collections.Immutable;
// Expressions: C# operations -> C++ on the game (see CodeGen.cs).
using System.Globalization;
using System.Text;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.Operations;
using Tomlyn.Model;

namespace TfexTranslator;

public sealed partial class UnitEmitter
{
    /* [[rewrite]] expr = "<a C# expression as TF.EX writes it, whitespace collapsed>", in = "<containing type>", cpp = "<C++>", key, why:
     * one expression of TF.EX's for the PC replaced where the Switch differs (InputService's `WiderSet ? 8 : 4` controller slots: the
     * Switch's six). Matched on the source text inside that type only, and listed in the report. */
    readonly HashSet<IOperation> exprRewriting = new();
    string ExprRewrite(IOperation op)
    {
        if (op.Syntax is not Microsoft.CodeAnalysis.CSharp.Syntax.ExpressionSyntax || exprRewriting.Contains(op)) return null;
        string text = null;
        foreach (var r in Rules("rewrite"))
        {
            if (!r.ContainsKey("expr") || (r.ContainsKey("in") && (string)r["in"] != type.ToDisplayString())) continue;
            text ??= System.Text.RegularExpressions.Regex.Replace(op.Syntax.ToString(), @"\s+", " ");
            if ((string)r["expr"] != text) continue;
            R.Add($"hand rule: expression rewritten (translate.toml [[rewrite]] expr, {r["key"]})", $"{type.Name}: {text} -> {r["cpp"]}: {r["why"]}");
            var rc = (string)r["cpp"];
            if (r.TryGetValue("include", out var rinc) && rinc is string rincs) includes.Add(rincs);
            // $0: the call's first operand (its receiver, or an extension's first argument), translated: `level.Session.Scores.ToArray()` -> Scores
            if (rc.Contains("$0") && op is IInvocationOperation ri)
                rc = rc.Replace("$0", Expr(ri.Instance ?? ri.Arguments.OrderBy(a => a.Parameter.Ordinal).First().Value));
            // $e: the matched expression itself, translated as without the rule (`self.Session.MatchStats` -> the game array, then wrapped)
            if (rc.Contains("$e"))
            {
                exprRewriting.Add(op);
                try { rc = rc.Replace("$e", Expr(op)); } finally { exprRewriting.Remove(op); }
            }
            return rc;
        }
        return null;
    }

    string Unsupported(IOperation op, string what)
    {
        R.Add("unsupported: " + what, Where(op));
        // a call that needs a Switch [service] rule keeps its member (the coverage report's per-owner rule lists); the rest by construct
        methodBlockers.Add(what.Contains("no unit translates") || what.Contains("without a set rule") || what.Contains("without a [service] rule") ? what : Construct(what));
        return $"TFEXGEN_UNSUPPORTED_EXPR(/* {what}: {op.Syntax.ToString().Split('\n')[0].Replace("*/", "* /")} */)";
    }

    /* ---------------------------------------------------------------- literals ---------------------------------------------------------------- */
    string Literal(object v, ITypeSymbol t)
    {
        switch (v)
        {
            case null:
                if (t != null && subst.Count > 0) t = Sub(t);   // default(T) in a generic copy: T's
                if (t is IArrayTypeSymbol) return Cpp(t) + "{}";
                if (t?.ToDisplayString() is "System.Version" or "System.Version?") return "0";   // a packed int64 (UnitEmitter.Cpp)
                if (t == null || t.SpecialType == SpecialType.System_String || t.IsReferenceType) return "nullptr";
                return t.SpecialType == SpecialType.System_Boolean ? "false" : Cpp(t) + "{}";
            case string s: return StringLiteral(s);
            case bool b: return b ? "true" : "false";
            case char ch: return ch >= 32 && ch < 127 && ch != '\'' && ch != '\\' ? $"u'{ch}'" : $"(char16_t){(int)ch}";
            // float.NaN / double.NaN are .NET's negative quiet NaNs (0xFFC00000 / 0xFFF8000000000000); a NaN negated keeps its payload
            case float f when float.IsNaN(f): return "(-__builtin_nanf(\"\"))";
            case float f when float.IsInfinity(f): return f > 0 ? "__builtin_inff()" : "(-__builtin_inff())";
            case double d when double.IsNaN(d): return "(-__builtin_nan(\"\"))";
            case double d when double.IsInfinity(d): return d > 0 ? "__builtin_inf()" : "(-__builtin_inf())";
            case float f: return FloatLit(f.ToString("R", CultureInfo.InvariantCulture)) + "f";
            case double d: return FloatLit(d.ToString("R", CultureInfo.InvariantCulture));
            case long l: return l + "LL";
            case ulong ul: return ul + "ULL";
            case uint ui: return ui + "u";
            case int i when t?.TypeKind == TypeKind.Enum: return EnumLiteral(i, (INamedTypeSymbol)t).ToString(CultureInfo.InvariantCulture);
            default: return Convert.ToString(v, CultureInfo.InvariantCulture);
        }
    }
    /* Game enums (goal 4, PC -> Switch): their literals differ on the Switch (MenuState: VersusSub 7 and VersusOptions 8 there, VersusOptions 7
     * on the PC). Generated code holds game enum values as the Switch's: a literal is mapped by name, a conversion to or from another type
     * (an int, a TF.EX enum: TF.EX's ToDomainModel casts) maps the PC's numbers both ways; a value neither has (TF.EX's MenuState 59..)
     * passes unchanged. */
    List<(long pc, long sw)> EnumPairs(INamedTypeSymbol e)
    {
        if (e == null || e.TypeKind != TypeKind.Enum || OriginOf(e) != Origin.Game) return null;
        var sw = B.EnumValues(SwitchName(e));
        if (sw == null) return null;
        var pairs = new List<(long, long)>();
        foreach (var f in e.GetMembers().OfType<IFieldSymbol>().Where(f => f.HasConstantValue))
            if (sw.TryGetValue(f.Name, out var v)) pairs.Add((Convert.ToInt64(f.ConstantValue), v));
            else R.Add("PC -> Switch: game enum literal missing on the Switch (value kept)", $"{e.Name}.{f.Name}");
        return pairs;
    }
    long EnumLiteral(long pc, INamedTypeSymbol e)
    {
        var pairs = EnumPairs(e);
        if (pairs == null || !pairs.Any(p => p.pc == pc)) return pc;
        var sw = pairs.First(p => p.pc == pc).sw;
        if (sw != pc) R.Add("PC -> Switch: game enum literal by name", $"{e.Name}.{e.GetMembers().OfType<IFieldSymbol>().FirstOrDefault(f => f.HasConstantValue && Convert.ToInt64(f.ConstantValue) == pc)?.Name}: {pc} -> {sw}");
        return sw;
    }
    string EnumConvert(string e, ITypeSymbol from, ITypeSymbol to)
    {
        if (SymbolEqualityComparer.Default.Equals(from, to)) return e;
        string Map(string x, List<(long pc, long sw)> pairs, bool toSwitch)
        {
            if (pairs == null || pairs.All(p => p.pc == p.sw)) return x;
            var src = pairs.Select(p => toSwitch ? p.pc : p.sw); var dst = pairs.Select(p => toSwitch ? p.sw : p.pc);
            return $"gen::EnumMap({x}, {{{string.Join(", ", src)}}}, {{{string.Join(", ", dst)}}})";
        }
        var r = Map(e, EnumPairs(from as INamedTypeSymbol), false);
        r = Map(r, EnumPairs(to as INamedTypeSymbol), true);
        if (r != e) R.Add("PC -> Switch: game enum conversion maps the PC's numbers", $"{from.Name} -> {to.Name}");
        return r;
    }
    static string FloatLit(string s) => s.Contains('E') || s.Contains('.') || s.Contains("Infinity") || s.Contains("NaN") ? s.Replace("E", "e") : s + ".0";

    static string CppU16(string s)   /* "..." body of a u"" literal */
    {
        var sb = new StringBuilder("\"");
        foreach (var ch in s)
            if (ch == '"' || ch == '\\') sb.Append('\\').Append(ch);
            else if (ch >= 32 && ch < 127) sb.Append(ch);
            else sb.Append($"\\u{(int)ch:x4}");
        return sb.Append('"').ToString();
    }

    string StringLiteral(string s)
    {
        if (!litNames.TryGetValue(s, out var n))
        {
            n = $"L_{litNames.Count}";
            litNames[s] = n;
            var sb = new StringBuilder();
            foreach (var ch in s)
                if (ch == '"' || ch == '\\') sb.Append('\\').Append(ch);
                else if (ch >= 32 && ch < 127) sb.Append(ch);
                else sb.Append($"\\u{(int)ch:x4}");
            lits.Append($"static gen::LitN<{s.Length + 1}> {n}{{u\"{sb}\"}};\n");
        }
        return n + ".P()";
    }

    /* ---------------------------------------------------------------- expressions ---------------------------------------------------------------- */
    string Expr(IOperation op)
    {
        if (op == null) return "";
        if (ExprRewrite(op) is string rewritten) return rewritten;
        // a TF.EX const with a [service] rule (InputDelayAdvisor.GuideRightMargin): the rule, where the const stands alone (a constant
        // expression it is part of still folds)
        if (op is IFieldReferenceOperation { Field: { IsConst: true } cf } cfr && OriginOf(cf.ContainingType) == Origin.Tfex && gen.ServiceRule(cf) != null &&
            Service(cf, new List<string>(), cfr) is string constRule)
            return constRule;
        if (op.ConstantValue.HasValue && op is not IInterpolatedStringOperation && op.Type != null && !(op is IConversionOperation { Operand.ConstantValue.HasValue: false }))
            return Literal(op.ConstantValue.Value, op.Type);
        switch (op)
        {
            case ILocalReferenceOperation l when refSlots.TryGetValue(l.Local, out var slot): return slot.get;
            case ILocalReferenceOperation l: return Id(l.Local.Name);
            // `out var x` as an argument: x is declared before the statement (HoistPatternLocals); here it is just x
            case IDeclarationExpressionOperation { Expression: ILocalReferenceOperation dl }: return Id(dl.Local.Name);
            // a hosted unit's primary-constructor parameter (InputService(IGameContext context), v0.28.0): its object is a token, so the
            // value is a [service] rule's ("<Type>.<parameter>"), as a hosted unit's fields
            case IParameterReferenceOperation p when IsCapturedPrimaryParam(p.Parameter) && !SymbolEqualityComparer.Default.Equals(current, p.Parameter.ContainingSymbol) &&
                                                     gen.ServiceRule(p.Parameter) is TomlTable ppr && ppr.TryGetValue("cpp", out var ppc) && ppc is string ppcs:
                R.Add("rule: a hosted unit's primary-constructor parameter -> its [service] rule", $"{type.Name}.{p.Parameter.Name}");
                return ppcs;
            case IParameterReferenceOperation p when IsCapturedPrimaryParam(p.Parameter) && !SymbolEqualityComparer.Default.Equals(current, p.Parameter.ContainingSymbol):
                R.Add("mechanical: a primary constructor's parameter read by a member -> its backing field", $"{type.Name}.{p.Parameter.Name}");
                return $"S(self)->{PrimaryParamField(p.Parameter)}";
            case IParameterReferenceOperation p: return Id(p.Parameter.Name);
            case IInstanceReferenceOperation ir: return ir.ReferenceKind == InstanceReferenceKind.ImplicitReceiver && implicitRecv != null ? implicitRecv : "self";
            case IParenthesizedOperation p: return "(" + Expr(p.Operand) + ")";
            // a mod's import field read as a value (`Imports.X != null`): MonoMod filled it on the PC, the export is always there on the Switch
            case IFieldReferenceOperation { Field: { IsStatic: true } mif } when ModImportUnit(mif) is string miu:
                includes.Add($"tfexgen_{miu}.hpp");
                R.Add("mechanical: a mod import field read -> its export's function (never null)", $"{mif.ContainingType.Name}.{mif.Name}");
                return $"reinterpret_cast<void*>(&tfex::gen::{miu}::{Id(mif.Name)})";
            case IFieldReferenceOperation f: return Field(f);
            // an event with a [service] rule (FortRise's patch_VersusModeButton.ModeSwitch: no handler on the Switch): the rule's value
            case IEventReferenceOperation ev when gen.ServiceRule(ev.Event) != null && Service(ev.Event, _ => "", ev, ev.Instance) is string evs: return evs;
            case IPropertyReferenceOperation p: return PropertyGet(p);
            case IInvocationOperation inv: return Invoke(inv);
            case IBinaryOperation b: return Binary(b);
            case ITupleBinaryOperation tb when tb.OperatorKind is BinaryOperatorKind.Equals or BinaryOperatorKind.NotEquals &&
                                                tb.LeftOperand.Type is INamedTypeSymbol { IsTupleType: true } tlt:
            {
                // (a, b) == (c, d): C#'s element-wise ==, strings by value (gen::StrEq)
                var els = tlt.TupleElements.Select((e, i) => e.Type.SpecialType == SpecialType.System_String
                    ? $"gen::StrEq(l_.Item{i + 1}, r_.Item{i + 1})" : $"l_.Item{i + 1} == r_.Item{i + 1}");
                var all = $"([&]() {{ auto l_ = {Expr(tb.LeftOperand)}; auto r_ = {Expr(tb.RightOperand)}; return {string.Join(" && ", els)}; }}())";
                return tb.OperatorKind == BinaryOperatorKind.NotEquals ? "!" + all : all;
            }
            case IUnaryOperation u: return Unary(u);
            case IConversionOperation c: return Conversion(c);
            case IArrayCreationOperation ac: return ArrayNew(ac);
            case IArrayInitializerOperation ai when ai.Parent is IArrayCreationOperation pac: return ArrayNew(pac);   // `= { ... }` asked for by its initializer node
            case IArrayInitializerOperation ai when ai.Parent is IFieldInitializerOperation { InitializedFields.Length: 1 } fi && fi.InitializedFields[0].Type is IArrayTypeSymbol fat:
                return ArrayFromItems(fat, ai, ai);   // static readonly string[] Options = { "RESUME", "QUIT" } (a class's static)
            case ICollectionExpressionOperation ce: return CollectionExpr(ce);
            case IWithOperation wo when IsPodS(wo.Type) && wo.Initializer is IObjectOrCollectionInitializerOperation wi:
            {   // `s with { A = x }` on a struct of ours: a copy, then the members set
                R.Add("mechanical: struct `with { ... }` -> a copy with the members set", Where(wo));
                var wsb = new StringBuilder($"([&]() {{ auto w_ = {Expr(wo.Operand)}; ");
                foreach (var init in wi.Initializers)
                    if (init is ISimpleAssignmentOperation { Target: IPropertyReferenceOperation wp } wa) wsb.Append($"w_.{wp.Property.Name} = {Expr(wa.Value)}; ");
                    else if (init is ISimpleAssignmentOperation { Target: IFieldReferenceOperation wf } wa2) wsb.Append($"w_.{StructsGen.FieldName(wf.Field)} = {Expr(wa2.Value)}; ");
                    else wsb.Append(Unsupported(init, "with initializer " + init.Kind) + "; ");
                return wsb.Append("return w_; }())").ToString();
            }
            case ISwitchExpressionOperation se: return SwitchExpr(se);
            case IAwaitOperation aw when inTaskRun > 0: return Expr(aw.Operation);   // inside an inlined Task.Run (TaskRun)
            // `await Send(m)` in an async method: a TF.EX method (translated synchronously: Task -> void, Task<T> -> T) or a [service] call
            // is just the call (S-async: the Switch's services don't block; the work is issued before the code after it runs)
            case IAwaitOperation aw2 when Strip(aw2.Operation) is IInvocationOperation ai &&
                                          (OriginOf(ai.TargetMethod.ContainingType) == Origin.Tfex || gen.ServiceRule(ai.TargetMethod) != null):
                R.Add("rule: await of a TF.EX / service call -> the call (S-async: the Switch's services don't block)", Where(aw2));
                return Expr(aw2.Operation);
            case IConditionalOperation c when c.WhenFalse != null: return $"({Expr(c.Condition)} ? {Operand(c.WhenTrue)} : {Operand(c.WhenFalse)})";
            case ISimpleAssignmentOperation { Target: IDiscardOperation } da0: return $"(void)({Expr(da0.Value)})";   // _ = x: evaluated, dropped
            // a Task field (MatchmakingService._skinBundleSendQueue, v0.28.0): no state (IsTaskType), the value only evaluated
            case ISimpleAssignmentOperation { Target: IFieldReferenceOperation { Field.Type: var tft } } tfa when IsTaskType(tft):
                R.Add("mechanical: a Task field assigned -> the value evaluated (tasks run inline, S-async)", Where(tfa));
                return $"(void)({Expr(tfa.Value)})";
            case ISimpleAssignmentOperation a when ModelListFromSeq(a) is string ml: return ml;
            // a tracked DynamicData local assigned DynamicData.For(x) (UnitEmitter's declaration): the wrapped object
            case ISimpleAssignmentOperation { Target: ILocalReferenceOperation { Local: var dl } } dla when dynLocals.ContainsKey(dl) && IsDynamicDataFor(dla.Value, out var dlt):
                return $"{Id(dl.Name)} = {Expr(dlt)}";
            case ISimpleAssignmentOperation { Target: IFieldReferenceOperation { Field: var rqf } } ra0 when ResolvedReflectionField(rqf): return Assign(ra0.Target, "");   // (its value isn't translated: resolved where used)
            case ISimpleAssignmentOperation { Target: IPropertyReferenceOperation } pa when pa.Parent is not IExpressionStatementOperation && pa.Type != null:
            {   // a property assignment used as a value (`Collider = (Grid = new Grid(...))`): the value, set, then yielded (a setter returns nothing)
                var pv = $"pa_{tmp++}";
                return $"([&]() {{ auto {pv} = {Expr(pa.Value)}; {Assign(pa.Target, pv)}; return {pv}; }}())";
            }
            case ISimpleAssignmentOperation a: return Assign(a.Target, Expr(a.Value));
            case ICompoundAssignmentOperation ca: return CompoundAssign(ca);
            // x.P++ on a property with accessors (not storage): get, then set (C#'s value: the old one postfix, the new one prefix)
            case IIncrementOrDecrementOperation id when id.Target is IPropertyReferenceOperation ipr && !ipr.Property.IsIndexer &&
                ipr.Property.SetMethod != null && LValueOrNull(id.Target) == null:
            {
                var o = id.Kind == OperationKind.Increment ? "+ 1" : "- 1";
                var get = PropertyGet(ipr);
                var set = Assign(ipr, $"(v_ {o})");
                R.Add("mechanical: ++/-- on a property with accessors -> get, then set", Where(id));
                return $"([&]() {{ auto v_ = {get}; {set}; return {(id.IsPostfix ? "v_" : $"(v_ {o})")}; }}())";
            }
            case IIncrementOrDecrementOperation id:
            {
                var t = LValue(id.Target);
                var o = id.Kind == OperationKind.Increment ? "++" : "--";
                return id.IsPostfix ? t + o : o + t;
            }
            case IObjectCreationOperation oc: return New(oc);
            case IInterpolatedStringOperation s: return Interpolated(s);
            case IArrayElementReferenceOperation ae when ae.Indices.Length == 1: return $"{Expr(ae.ArrayReference)}[{Expr(ae.Indices[0])}]";
            case IArrayElementReferenceOperation ae2 when ae2.Indices.Length == 2 && ae2.ArrayReference.Type is IArrayTypeSymbol { Rank: 2 } ga && IsGridElem(ga.ElementType):
                return $"{Expr(ae2.ArrayReference)}({Expr(ae2.Indices[0])}, {Expr(ae2.Indices[1])})";
            // typeof(T) of a translated type (a mod's class): no System.Type object on the Switch; reflection over it (GetMethod) is resolved at
            // translate time where it's used (ReflectedMethod, a foreach unrolled over typeof lists), so the value itself is null
            case ITypeOfOperation tofo when tofo.TypeOperand is INamedTypeSymbol tot0 && OriginOf(tot0) == Origin.Tfex && !InBinary(tot0) && subst.Count == 0:
                R.Add("mechanical: typeof(a translated type) -> null (no Type object; its reflection resolved at translate time)", Where(tofo));
                return "nullptr";
            // typeof(int) / typeof(string), also typeof(T) in a generic copy for them (FortRise's SpriteEntry<T>: `var type = typeof(T); if (type
            // == typeof(int))`): the runtime type object, compared by Type == (one object per type)
            case ITypeOfOperation tofp when (subst.Count > 0 ? Sub(tofp.TypeOperand) : tofp.TypeOperand) is var totp && totp.SpecialType != SpecialType.None && !InBinary(totp):
                R.Add("mechanical: typeof(a primitive / string) -> its runtime type object", Where(tofp));
                return TypeOfAny(totp);
            // typeof(GameType), also typeof(T) in a generic copy (TF.State's DropLiveFromEntityPool<T>: Monocle.Cache's key): the runtime type
            case ITypeOfOperation tof when (subst.Count > 0 ? Sub(tof.TypeOperand) : tof.TypeOperand) is var tot && tot is not ITypeParameterSymbol && InBinary(tot):
                // handed to a game / corlib method as a System.Type (Monocle.Cache's Dictionary<Type, ...> key): the Type object, made by the
                // game's own Type.GetTypeFromHandle (+0x401f70, as Cache.Create<T> does); elsewhere (type tests, our own comparisons) the handle
                if (tof.Parent is IArgumentOperation { Parameter: { } tp } && tp.Type?.ToDisplayString() == "System.Type" &&
                    tp.ContainingSymbol is IMethodSymbol tpm && OriginOf(tpm.ContainingType) != Origin.Tfex)
                {
                    R.Add("mechanical: typeof(T) passed as a System.Type to game code -> the Type object (GetTypeFromHandle)", Where(tof));
                    return $"gen::TypeObject({TypeOf(tot)})";
                }
                return TypeOf(tot);
            case IDefaultValueOperation d: return Literal(null, d.Type);
            case ILiteralOperation lit: return Literal(lit.ConstantValue.Value, lit.Type);
            case IIsTypeOperation it: return $"({AsType(Expr(it.ValueOperand), it.TypeOperand)} != nullptr)";
            // fi?.X(...) on a FieldInfo resolved at translate time (it is never null): the access itself
            case IConditionalAccessOperation rca when ReflectedField(rca.Operation) != null && Strip(rca.Operation) is IFieldReferenceOperation: return Expr(rca.WhenNotNull);
            // fi ??= AccessTools.Field(...) on such a field: nothing to do (its uses are the field itself)
            case ICoalesceAssignmentOperation { Target: IFieldReferenceOperation { Field: var cqf } } when ResolvedReflectionField(cqf): return "(void)0";
            case IConditionalAccessOperation ca: return ConditionalAccess(ca);
            case IInvalidOperation inv0 when InvalidRule(inv0) is string ir: return ir;
            case IConditionalAccessInstanceOperation: return condAccessStack.Count > 0 ? condAccessStack.Peek() : Unsupported(op, "?. instance");
            case IDelegateCreationOperation dc: return DelegateObject(dc);
            case ITupleOperation tu when tu.Type is INamedTypeSymbol { IsTupleType: true }: return $"{Cpp(tu.Type)}{{{string.Join(", ", tu.Elements.Select(Expr))}}}";
            // (a throw expression has no type of its own: a switch-expression arm's is the switch's, `x ?? throw` the operand's)
            case IThrowOperation: R.Add("rule: throw -> gen::Throw (fatal, as the game's own exceptions)", Where(op)); return $"(gen::Throw(\"{name}: {current?.Name}\"), {Literal(null, op.Type ?? (op.Parent is ISwitchExpressionArmOperation { Parent: ISwitchExpressionOperation tse } ? tse.Type : op.Parent is IConversionOperation tcv ? tcv.Type : null))})";
            // `fi.GetValue(x) is bool b` over a field resolved at translate time: the field's own value (a value type: always matches)
            case IIsPatternOperation rip when ReflectedFieldRead(rip.Value) is var (rfs, rread) && rip.Pattern is IDeclarationPatternOperation { DeclaredSymbol: ILocalSymbol rl } &&
                                              !rfs.Type.IsReferenceType && SymbolEqualityComparer.Default.Equals(rl.Type, rfs.Type):
                return $"([&]() {{ {Id(rl.Name)} = {rread}; return true; }}())";
            case IIsPatternOperation ip: return Pattern(Expr(ip.Value), ip.Pattern);
            // modelList ?? []: a model's list is storage, never a null pointer (a null one counts 0): the list itself
            case ICoalesceOperation co when IsModelList(co.Value) && Strip(co.WhenNull) is ICollectionExpressionOperation { Elements.Length: 0 }:
                R.Add("mechanical: a model list ?? [] -> the list (a null one is empty)", Where(co));
                return Expr(co.Value);
            case ICoalesceOperation co when co.Type?.IsReferenceType == true:
            {
                var v = $"c_{tmp++}";
                return $"([&]() {{ auto {v} = {Expr(co.Value)}; return {v} ? {v} : {Expr(co.WhenNull)}; }}())";
            }
            case ICoalesceOperation co when co.Value.Type != null && IsNullable(co.Value.Type):
            {
                var v = $"c_{tmp++}";
                var whenNull = Expr(co.WhenNull);
                var rt = Cpp(co.Type);
                return $"([&]() -> {rt} {{ auto {v} = {Expr(co.Value)}; if ({v}.has) return {(IsNullable(co.Type) ? v : v + ".value")}; return {whenNull}; }}())";
            }
            // x.P ??= v on a property with accessors (TF.EX's SkinStreamService: skinSprite.ExtraData ??= new ...): get, set when null
            case ICoalesceAssignmentOperation cpa when cpa.Target is IPropertyReferenceOperation cpr && !cpr.Property.IsIndexer && cpr.Property.SetMethod != null &&
                LValueOrNull(cpa.Target) == null && !IsNullable(cpa.Target.Type):
            {
                R.Add("mechanical: ??= on a property with accessors -> get, then set when null", Where(cpa));
                return $"([&]() {{ auto v_ = {PropertyGet(cpr)}; if (v_) return v_; v_ = {Expr(cpa.Value)}; {Assign(cpr, "v_")}; return v_; }}())";
            }
            case ICoalesceAssignmentOperation caa:
            {
                var target = LValue(caa.Target);
                if (caa.Target.Type != null && IsNullable(caa.Target.Type))
                    return $"(({target}).has ? ({target}) : (({target}) = {Cpp(caa.Target.Type)}({Expr(caa.Value)})))";
                return $"(({target}) ? ({target}) : (({target}) = {Expr(caa.Value)}))";
            }
        }
        return Unsupported(op, op.Kind.ToString());
    }

    /* a FortRise member the game class lacks that translate.toml [fortrise] keeps per object (absent + side = true: MatchSettings.IsCustom,
     * CustomVersusModeName): DynamicData's side table under its name, read (value null) or written */
    string SideMember(IOperation op, string value = null)
    {
        op = Strip(op);
        (ISymbol sym, ITypeSymbol t, IOperation inst) = op switch
        {
            IFieldReferenceOperation f => ((ISymbol)f.Field, f.Field.Type, f.Instance),
            IPropertyReferenceOperation { Arguments.Length: 0 } p => (p.Property, p.Property.Type, p.Instance),
            _ => (null, null, null),
        };
        if (sym == null || sym.IsStatic || inst == null || gen.AbsentRule(sym) is not TomlTable r || !(r.TryGetValue("side", out var sd) && sd is true)) return null;
        R.Add("hand rule: a PC member the Switch lacks, kept in the side table ([fortrise] side = true)", $"{sym.ContainingType.Name}.{sym.Name}");
        var recv = Expr(inst);
        var vt = t.IsReferenceType ? "void*" : Cpp(t);
        return value == null ? $"gen::SideGet<{vt}>(\"{sym.Name}\", {recv})" : $"gen::SideSet<{vt}>(\"{sym.Name}\", {recv}, {value})";
    }

    /* a member translate.toml [fortrise] marks absent on the Switch (the operation, or the member it's called on / read from) */
    ISymbol AbsentMember(IOperation op)
    {
        op = Strip(op);
        var sym = op switch
        {
            IFieldReferenceOperation f => (ISymbol)f.Field,
            IPropertyReferenceOperation p => p.Property,
            IInvocationOperation i => i.TargetMethod,
            _ => null,
        };
        if (sym != null && gen.AbsentRule(sym) is TomlTable t)
        {
            R.Add($"hand rule: FortRise member absent on the Switch ([fortrise], {t["key"]})", sym.ContainingType.Name + "." + sym.Name);
            return sym;
        }
        return null;
    }

    /* translate.toml [invalid]: "<C# source text>" = { cpp, key, why }: members the reference TowerFall.exe lacks (FortRise's additions:
     * MapScene.NoRandom ...), by their source text (Roslyn has no symbol for them) */
    string InvalidRule(IInvalidOperation op, bool set = false)
    {
        var text = string.Concat(op.Syntax.ToString().Where(c => !char.IsWhiteSpace(c)));
        foreach (var (k, v) in cfg.Table("invalid"))
            if (v is TomlTable t && string.Concat(k.Where(c => !char.IsWhiteSpace(c))) == text && t.ContainsKey(set ? "set" : "cpp"))
            {
                R.Add($"hand rule: member missing from the reference game ([invalid], {t["key"]})", text + (set ? " (set)" : ""));
                gen.Rules.Use("invalid", k);
                return (string)t[set ? "set" : "cpp"];   // set: the assignment's C++, $value = the assigned value
            }
        return null;
    }

    static bool IsNullable(ITypeSymbol t) => t is INamedTypeSymbol { OriginalDefinition.SpecialType: SpecialType.System_Nullable_T };
    static ITypeSymbol Underlying(ITypeSymbol t) => ((INamedTypeSymbol)t).TypeArguments[0];

    readonly Stack<string> condAccessStack = new();
    string ConditionalAccess(IConditionalAccessOperation ca)
    {
        // x?.Y on an absent member (translate.toml [fortrise]: FortRise's mod list ...): it is null on the Switch, so the whole chain is
        if (AbsentMember(ca.Operation) != null || (ca.Operation is IConditionalAccessOperation absIn && AbsentMember(absIn.WhenNotNull) != null))
            return ca.Type == null || ca.Type.SpecialType == SpecialType.System_Void ? "((void)0)" : Literal(null, ca.Type);
        // F?.Invoke(x) on a delegate field made by reflection over a static property setter: the setter's call (never null on the PC)
        if (Strip(ca.Operation) is IFieldReferenceOperation { Field: var rsf } && ReflectedSetterField(rsf) is IPropertySymbol rsp &&
            Strip(ca.WhenNotNull) is IInvocationOperation { TargetMethod.Name: "Invoke", Arguments.Length: 1 } rsi)
            return SetterInvoke(rsp, rsf, rsi.Arguments[0].Value, rsi);
        // mi?.Invoke(target, args) with mi a reflected method (FortRise's mod template: `typeof(Mod).GetMethod("SaveSettings", ...)?.Invoke(Instance,
        // null)`): resolved at translate time, so never null: the call
        if (MethodFrom(ca.Operation) is IMethodSymbol && Strip(ca.WhenNotNull) is IInvocationOperation { TargetMethod.Name: "Invoke" } rmi &&
            rmi.TargetMethod.ContainingType.ToDisplayString() is "System.Reflection.MethodInfo" or "System.Reflection.MethodBase")
        {
            reflectedRecv = ca.Operation;
            try { return Expr(rmi); } finally { reflectedRecv = null; }
        }
        // Imports.X?.Invoke(...) on a mod import field: the export's call (ModImportUnit)
        if (Strip(ca.Operation) is IFieldReferenceOperation { Field: { IsStatic: true } cmf } && ModImportUnit(cmf) is string cmu &&
            Strip(ca.WhenNotNull) is IInvocationOperation { TargetMethod.Name: "Invoke" } cmi)
        {
            var call = ModImportCall(cmf, cmu, cmi);
            return ca.Type == null || ca.Type.SpecialType == SpecialType.System_Void ? call : IsNullable(ca.Type) ? $"{Cpp(ca.Type)}({call})" : call;
        }
        var v = $"ca_{tmp++}";
        bool nullableRecv = ca.Operation.Type != null && IsNullable(ca.Operation.Type);   // x?.M() on an int?: tested by .has, used as .value
        condAccessStack.Push(nullableRecv ? v + ".value" : v);
        var inner = Expr(ca.WhenNotNull);
        condAccessStack.Pop();
        var t = ca.Type;
        // (T[])a?.Clone(): the cast's array type (the array clone gives the array itself; null is the empty Arr)
        if (ca.Parent is IConversionOperation { Type: IArrayTypeSymbol cat } && Strip(ca.WhenNotNull) is IInvocationOperation { TargetMethod.Name: "Clone" }) t = cat;
        var test = nullableRecv ? v + ".has" : v;
        if (IsNullable(t))   // x?.ValueMember: T? (inner is T, or already T? when the member is nullable)
        {
            var opt = Cpp(t);
            var innerT = ca.WhenNotNull.Type;
            var wrap = innerT != null && IsNullable(innerT) ? inner : $"{opt}({inner})";
            return $"([&]() -> {opt} {{ auto {v} = {Expr(ca.Operation)}; return {test} ? {wrap} : {opt}{{}}; }}())";
        }
        // s?.Select(..).ToList() on a TF.EX method's snapshot (never null): the inner expression, whatever its type (a snapshot, a List)
        if (CalledSnap(ca.Operation, ReturnsSnap) != null) return $"([&]() {{ auto {v} = {Expr(ca.Operation)}; return {inner}; }}())";
        // a chain ending in a materialized snapshot (x?.M()?.Select(..).ToList()): null is the empty snapshot (the local holding it is one)
        if (ChainSnapElem(ca) is string cse) return $"([&]() -> gen::Snap<{cse}> {{ auto {v} = {Expr(ca.Operation)}; if (!{test}) return gen::Snap<{cse}>{{}}; return {inner}; }}())";
        return $"([&]() {{ auto {v} = {Expr(ca.Operation)}; return {test} ? {inner} : {Literal(null, t)}; }}())";
    }

    string TypeOf(ITypeSymbol t)
    {
        if (InBinary(t))
        {
            var tk = B.Need("t", SwitchName(t));
            // a game type the Switch's 1.4.1.6 doesn't have (WorkshopButton: PC only): null, so nothing is one (gen::As of null is null)
            if (B.ErrorOf(tk[6..]) is string te && te.StartsWith("no type"))
            { methodKeys.Remove(tk[6..]); R.Add("mechanical: a game type missing on the Switch -> null (nothing is one)", t.ToDisplayString()); return "nullptr"; }
            return $"gen::TypeOf({tk})";
        }
        if (OriginOf(t) == Origin.Bcl && t is INamedTypeSymbol { IsGenericType: false } && (t.IsReferenceType || t.SpecialType != SpecialType.None))
            return $"gen::TypeOf({B.Need("t", SwitchName(t))})";   // string, a boxed float / long ...: corlib's class in the game
        R.Add("unsupported: type test against a non-game type", t.ToDisplayString());
        return "nullptr";
    }

    /* ---------------------------------------------------------------- fields and properties ---------------------------------------------------------------- */
    string Field(IFieldReferenceOperation f)
    {
        var fs = f.Field;
        if (IsStrongBox(fs.ContainingType) && fs.Name == "Value" && f.Instance != null) return $"({Expr(f.Instance)})->Value";
        // a tuple's element (x.Item1, or a named one): gen::Tup2/Tup3's ItemN
        if (fs.ContainingType is { IsTupleType: true } && f.Instance != null) return $"{Paren(f.Instance)}.{(fs.CorrespondingTupleField ?? fs).Name}";
        var o = OriginOf(fs.ContainingType);
        if (fs.IsConst) return Literal(fs.ConstantValue, fs.Type);
        if (SideMember(f) is string sideF) return sideF;
        if (AbsentMember(f) != null) return Literal(null, fs.Type);
        switch (fs.ContainingType.ToDisplayString() + "." + fs.Name)
        {
            case "System.DateTime.MinValue": case "System.TimeSpan.Zero": return "0LL";
            case "System.DateTime.MaxValue": case "System.TimeSpan.MaxValue": return "0x7fffffffffffffffLL";
            case "string.Empty": return StringLiteral("");
        }
        switch (o)
        {
            case Origin.Tfex:
                if (gen.ServiceRule(fs) != null && Service(fs, _ => "", f, fs.IsStatic ? null : f.Instance) is string sf1) return sf1;   // a hosted unit's state ($self: the receiver, FortRise's archerData.ExtraHairData)
                if (!fs.IsStatic && IsPodS(fs.ContainingType)) return $"{Paren(f.Instance)}.{StructsGen.FieldName(fs)}";
                // a MonoMod patch class's redeclaration of the game's own static (patch_MatchSettings.GoalMultiplier): the game's static
                if (fs.IsStatic && fs.ContainingType.Name.StartsWith("patch_") && fs.ContainingType.BaseType is INamedTypeSymbol sgb && OriginOf(sgb) == Origin.Game &&
                    sgb.GetMembers(fs.Name).OfType<IFieldSymbol>().FirstOrDefault() is IFieldSymbol sgf && sgf.IsStatic)
                {
                    R.Add("mechanical: a MonoMod patch class's static that the game type has -> the game's static", fs.ContainingType.Name + "." + fs.Name);
                    var sacc = $"(gen::Cctor({B.Need("c", SwitchName(sgb))}), gen::Static<{(sgf.Type.IsReferenceType ? "void*" : Cpp(sgf.Type))}>({B.Need("f", SwitchName(sgb), sgf.Name)}))";
                    return GameArray(sgf.Type, sacc) ?? sacc;
                }
                if (fs.IsStatic && SymbolEqualityComparer.Default.Equals(fs.ContainingType, type))
                    return isClass ? $"g_s->{Id(fs.Name)}" : fieldFns.Contains(fs) || LazyStatic(fs) ? Id(fs.Name) + "()" : Id(fs.Name);
                if (!fs.IsStatic && isClass && SymbolEqualityComparer.Default.Equals(fs.ContainingType, type))
                    return ModelCollFieldCpp(fs) != null ? $"(&S({Expr(f.Instance)})->{Id(fs.Name)})" : $"S({Expr(f.Instance)})->{Id(fs.Name)}";   // a model list: by address, as models' own
                // a FortRise MonoMod patch class's redeclaration of the game's own field (patch_MatchVariants.canRandoms: MonoMod merges it into
                // the game type's private field): the game's field
                if (!fs.IsStatic && fs.ContainingType.BaseType is INamedTypeSymbol gb && OriginOf(gb) == Origin.Game &&
                    gb.GetMembers(fs.Name).OfType<IFieldSymbol>().FirstOrDefault() is IFieldSymbol gfld && !gfld.IsStatic)
                {
                    R.Add("mechanical: a MonoMod patch class's field that the game type has -> the game's field", fs.ContainingType.Name + "." + fs.Name);
                    var gi = f.Instance == null || f.Instance is IInstanceReferenceOperation ? "self" : Expr(f.Instance);
                    return GameArray(gfld.Type, $"gen::Fld<void*>({gi}, {B.Need("f", SwitchName(gb), gfld.Name)})")
                        ?? $"gen::Fld<{Cpp(gfld.Type)}>({gi}, {B.Need("f", SwitchName(gb), gfld.Name)})";
                }
                if (HostedInstanceField(fs))   // a hosted unit's instance field: its static storage (EmitField)
                {
                    if (SymbolEqualityComparer.Default.Equals(fs.ContainingType, type)) return $"{Id(fs.Name)}()";
                    var hu0 = OtherUnit(fs.ContainingType);
                    includes.Add($"tfexgen_{hu0["name"]}.hpp");
                    return $"tfex::gen::{hu0["name"]}::{Id(fs.Name)}()";
                }
                // a static field of another unit's hosted FortRise.Mod subclass (LoaderAI's module tables, read by its patch classes): that
                // unit's namespace-level static, as a static class's (LazyStatic: made at first use)
                if (fs.IsStatic && HostedModType(fs.ContainingType) && OtherUnit(fs.ContainingType) is TomlTable hmu)
                {
                    includes.Add($"tfexgen_{hmu["name"]}.hpp");
                    R.Add("mechanical: a static field of another unit's hosted Mod subclass -> that unit's static", fs.ContainingType.Name + "." + fs.Name);
                    return $"tfex::gen::{hmu["name"]}::{Id(fs.Name)}{(LazyStatic(fs) || ArrayFieldFn(fs) ? "()" : "")}";
                }
                if (OtherUnit(fs.ContainingType) is TomlTable hu && hu.TryGetValue("hosted", out var hv) && hv is true)   // its state is hand-written
                    return Unsupported(f, "field of a hosted unit's type without a [service] rule: " + fs.ContainingType.ToDisplayString() + "." + fs.Name);
                if (IsClassType(fs.ContainingType) && (OtherUnit(fs.ContainingType) is TomlTable cu0 && !cu0.ContainsKey("auto") && !cu0.ContainsKey("members") ? cu0 : AutoU(fs.ContainingType, fs)) is TomlTable cu)
                {
                    includes.Add($"tfexgen_{cu["name"]}.hpp");
                    var un = $"tfex::gen::{cu["name"]}";
                    return fs.IsStatic ? $"({un}::EnsureClass(), {un}::g_s)->{Id(fs.Name)}" : $"static_cast<{un}::Self*>({Expr(f.Instance)})->{Id(fs.Name)}";
                }
                if (Service(fs, new List<string>(), f) is string sf) return sf;
                // a static field of a static class, or of a class translated as functions (a Harmony patch class: TFGamePatch's
                // IsRunningUnsynchronizedUpdate, read by LevelPatch): the other unit's namespace-level static
                if (fs.IsStatic && (fs.ContainingType.IsStatic || !IsClassType(fs.ContainingType)) && (AutoU(fs.ContainingType, fs) ?? OtherUnit(fs.ContainingType)) is TomlTable ou)
                {
                    includes.Add($"tfexgen_{ou["name"]}.hpp");
                    return $"tfex::gen::{ou["name"]}::{Id(fs.Name)}{(LazyStatic(fs) || ArrayFieldFn(fs) ? "()" : "")}";
                }
                return Unsupported(f, "field of another TF.EX type " + fs.ContainingType.Name);
            // a field of a game struct held in a game object's field (archer.Portraits.Win): one load at the summed offset (a struct
            // field's offset is from the struct's start: PortraitInfo.Win +16 inside ArcherData.Portraits +216)
            case Origin.Game when !fs.IsStatic && fs.ContainingType.TypeKind == TypeKind.Struct && GameStructPath(f) is var (gobj, goff):
                return GameArray(fs.Type, $"gen::Fld<void*>({gobj}, {goff})") ?? $"gen::Fld<{Cpp(fs.Type)}>({gobj}, {goff})";
            case Origin.Game:
            case Origin.Fna when InBinary(fs.ContainingType):
                // a [service] rule on a PC game field the Switch's type lacks (XGamepadInput.XGamepad: the Switch's controller input itself)
                if (!fs.IsStatic && gen.ServiceRule(fs) != null && Service(fs, _ => "", f, f.Instance) is string gfs) return gfs;
                // ... and on a static (FortRise's GameModeRegistry tables, read from TF.EX: its translated registry)
                if (fs.IsStatic && gen.ServiceRule(fs) != null && Service(fs, new List<string>(), f) is string gsfs) return gsfs;
                if (fs.IsStatic)
                    return GameArray(fs.Type, CctorThen(SwitchName(fs.ContainingType), $"gen::Static<void*>({B.Need("f", SwitchName(fs.ContainingType), fs.Name)})"))
                        ?? CctorThen(SwitchName(fs.ContainingType), $"gen::Static<{Cpp(fs.Type)}>({B.Need("f", SwitchName(fs.ContainingType), fs.Name)})");
                return GameArray(fs.Type, $"gen::Fld<void*>({Expr(f.Instance)}, {B.Need("f", SwitchName(fs.ContainingType), fs.Name)})")
                    ?? $"gen::Fld<{Cpp(fs.Type)}>({Expr(f.Instance)}, {B.Need("f", SwitchName(fs.ContainingType), fs.Name)})";
            case Origin.Fna:
                if (!fs.IsStatic && FnaValueTypes.ContainsKey(fs.ContainingType.ToDisplayString())) return $"{Expr(f.Instance)}.{fs.Name}";
                break;
        }
        return Unsupported(f, "field " + fs.ToDisplayString());
    }

    /* (object, offset) of a game-struct field reached through game-object fields: obj.A.B... (A a struct field of a class, B of A ...) */
    (string obj, string off)? GameStructPath(IFieldReferenceOperation f)
    {
        var inner = Strip(f.Instance);
        if (inner is not IFieldReferenceOperation outer || outer.Field.IsStatic || outer.Field.Type.TypeKind != TypeKind.Struct || OriginOf(outer.Field.Type) != Origin.Game) return null;
        var me = B.Need("f", SwitchName(f.Field.ContainingType), f.Field.Name);
        if (outer.Field.ContainingType.TypeKind == TypeKind.Struct && OriginOf(outer.Field.ContainingType) == Origin.Game)
            return GameStructPath(outer) is var (o2, off2) ? (o2, $"{off2} + {me}") : null;
        if (!InBinary(outer.Field.ContainingType)) return null;
        R.Add("mechanical: a game struct's field through a game object's field -> one load at the summed offset", Where(f));
        return (Expr(outer.Instance), $"{B.Need("f", SwitchName(outer.Field.ContainingType), outer.Field.Name)} + {me}");
    }

    string PropertyGet(IPropertyReferenceOperation p)
    {
        var ps = p.Property;
        var owner = ps.ContainingType;
        var full = owner.ToDisplayString() + "." + ps.Name;
        // Task.CompletedTask: a Task field's value (IsTaskType: a byte, no state; the Switch's tasks run inline, S-async)
        if (full == "System.Threading.Tasks.Task.CompletedTask") { R.Add("mechanical: Task.CompletedTask -> 0 (a Task holds no state: tasks run inline, S-async)", Where(p)); return "0"; }
        // x.GetType().Name: the runtime type's short name, from its type record (+0x20). MemberInfo.Name is abstract (RuntimeType overrides
        // it; the base body throws NotImplementedException), and its vtable slot doesn't bind (TF.State's CoroutinePatch: an iterator's
        // class name, "<SetupSequence>d__N")
        if (ps.Name == "Name" && owner.ToDisplayString() is "System.Type" or "System.Reflection.MemberInfo" &&
            Strip(p.Instance) is IInvocationOperation { TargetMethod.Name: "GetType", Arguments.Length: 0, Instance: { } gti })
        {
            R.Add("mechanical: x.GetType().Name -> the runtime type record's name (gen::TypeNameStr)", Where(p));
            return $"gen::TypeNameStr({Expr(gti)})";
        }
        if (DictSingleValue(p) is string dsv) return dsv;
        if (IsGrouping(owner) && ps.Name == "Key" && p.Instance != null) return $"({Expr(p.Instance)})->Key";
        if (IsPcInputOwner(owner)) { R.Add("TFEX-DIFF S-input: the PC keyboard / mouse -> nothing pressed", Where(p)); return Literal(null, ps.Type); }
        if (p.Instance != null && IsAttrMgr(Strip(p.Instance).Type) && ps.IsIndexer) return $"({Expr(p.Instance)})->At({Expr(p.Arguments[0].Value)})";
        if (owner.IsAnonymousType) return Unsupported(p, "anonymous type member");
        // x.ToList().Count / x.ToList()[i]: the snapshot (gen::Snap) itself
        if (p.Instance != null && LinqMaterialized(p.Instance) != null && Strip(p.Instance).Type is not IArrayTypeSymbol)
        {
            if (ps.Name == "Count" && !ps.IsIndexer) return $"({Expr(p.Instance)}).Count()";
            if (ps.IsIndexer && p.Arguments.Length == 1) return $"({Expr(p.Instance)})[{Expr(p.Arguments[0].Value)}]";
        }
        // S-async: a CancellationToken is a plain value (UnitEmitter.Cpp); nothing is cancelled mid-way on the Switch
        if (full is "System.Threading.CancellationToken.IsCancellationRequested" or "System.Threading.CancellationToken.CanBeCanceled") return "false";
        if (full is "System.Threading.CancellationTokenSource.Token" or "System.Threading.CancellationToken.None") return "0";
        if (full == "System.Threading.CancellationTokenSource.IsCancellationRequested") return "false";
        if (SideMember(p) is string sideP) return sideP;
        if (AbsentMember(p) != null || (p.Instance != null && AbsentMember(p.Instance) != null)) return Literal(null, ps.Type);
        if (p.Instance != null && IsModelMap(p.Instance))
        {
            if (ps.IsIndexer) return $"gen::StrMapGet({Expr(Strip(p.Instance))}, {Expr(p.Arguments[0].Value)})";
            if (ps.Name == "Count") return $"({Expr(Strip(p.Instance))})->Count()";
        }
        if (p.Instance != null && ValKind(Strip(p.Instance).Type) is VKind.Set or VKind.List &&
            !(Strip(p.Instance) is ILocalReferenceOperation { Local: var vsl } && snapLocals.ContainsKey(vsl)))   // (a snapshot local: below)
        {
            var vl = Expr(Strip(p.Instance));
            if (ps.Name == "Count") return $"({vl})->Count()";
            if (ps.IsIndexer) return $"({vl})->Get({Expr(p.Arguments[0].Value)})";
            return Unsupported(p, "value collection member " + ps.Name);
        }
        if (p.Instance != null && IsValMap(Strip(p.Instance).Type))
        {
            var mp = Expr(Strip(p.Instance));
            switch (ps.Name)
            {
                case "Count": return $"({mp})->Count()";
                case "Keys": return $"gen::MapKeys({mp})";
                case "Values": return $"gen::MapValues({mp})";
                case "this[]": return $"({mp})->Get({Expr(p.Arguments[0].Value)})";
            }
            return Unsupported(p, "value map member " + ps.Name);
        }
        if (p.Instance != null && !ps.IsStatic && IsPodS(owner) && IsAutoProperty(ps)) return $"{Paren(p.Instance)}.{ps.Name}";
        if (p.Instance != null && IListOfOp(Strip(p.Instance)) is string il)   // Count / this[] through a collection interface
        {
            if (ps.Name == "Count") return $"gen::ICount({Expr(Strip(p.Instance))}, {il})";
            if (ps.IsIndexer) return $"gen::IItem<{Cpp(ps.Type)}>({Expr(Strip(p.Instance))}, {il}, {Expr(p.Arguments[0].Value)})";
        }
        // PC -> Switch: GameData.VersusTowers[mapId]. On the PC the list index is the tower's level ID (TF.EX's MapId); the Switch's list
        // has another order (tfex/NetplayManager.cpp SelectStandard matches LevelData.ID.X): the tower whose ID is mapId, or null
        if (ps.IsIndexer && p.Instance is IFieldReferenceOperation { Field: { Name: "VersusTowers", ContainingType.Name: "GameData" } } vt && vt.Type is INamedTypeSymbol vlt)
        {
            R.Add("PC -> Switch: GameData.VersusTowers[mapId] -> the tower whose LevelData.ID is mapId", Where(p));
            var count = BclCall(vlt, "get_Count", "l_", new List<string>(), "int32_t");
            var item = BclCall(vlt, "get_Item", "l_", new List<string> { "i_" }, "void*", new[] { "int32" });
            return $"([&]() -> void* {{ void* l_ = {Expr(p.Instance)}; int32_t id_ = {Expr(p.Arguments[0].Value)}; for (int32_t i_ = 0; l_ && i_ < {count}; i_++) {{ void* t_ = {item}; " +
                   $"if (gen::Fld<int32_t>(t_, {B.Need("f", "TowerFall.LevelData", "ID")}) == id_) return t_; }} return nullptr; }}())";
        }
        if (owner.ToDisplayString() is "System.DateTime" or "System.TimeSpan") return TimeProperty(p);
        if (full == "Monocle.Screen.Center" && p.Instance != null)   // 1.4.1.6's Screen has only Width/Height: PC's `Size * 0.5f`
        {
            var w = owner.GetMembers("Width").OfType<IPropertySymbol>().First().GetMethod; var h = owner.GetMembers("Height").OfType<IPropertySymbol>().First().GetMethod;
            R.Add("PC -> Switch: Screen.Center -> (Width, Height) * 0.5", Where(p));
            return $"([&]() {{ void* s_ = {Expr(p.Instance)}; return gen::Vector2{{(float){Call(w, "s_", new List<string>(), p)}, (float){Call(h, "s_", new List<string>(), p)}}} * 0.5f; }}())";
        }
        if (owner.ToDisplayString().StartsWith("HarmonyLib.Traverse") && ps.Name == "Value" && p.Instance is IInvocationOperation tf) return TraverseValue(tf, p);
        if (p.Instance?.Type is INamedTypeSymbol kvt && kvt.OriginalDefinition.ToDisplayString() == "System.Collections.Generic.KeyValuePair<TKey, TValue>")
            return $"({Expr(p.Instance)}).{ps.Name}";
        if (p.Instance?.Type != null && IsNullable(p.Instance.Type))
        {
            if (ps.Name == "HasValue") return $"({Expr(p.Instance)}).has";
            if (ps.Name == "Value") return $"({Expr(p.Instance)}).value";
        }
        if (p.Instance?.Type is IArrayTypeSymbol && ps.Name == "Length")   // a snapshot local (a ToArray of models) counts its own
            return Strip(p.Instance) is ILocalReferenceOperation { Local: var sl } && snapLocals.ContainsKey(sl) ? $"{Expr(p.Instance)}.count" : $"{Expr(p.Instance)}.len";
        if (ps.IsStatic && owner.ToDisplayString() == "Microsoft.Xna.Framework.Color" && p.Arguments.Length == 0) return "gen::Color_" + ps.Name;
        if (p.Instance != null && Strip(p.Instance) is ILocalReferenceOperation snl && snapLocals.ContainsKey(snl.Local))   // a snapshot local
        {
            if (ps.Name == "Count") return $"{Id(snl.Local.Name)}.Count()";
            if (ps.IsIndexer) return $"{Id(snl.Local.Name)}.items[{Expr(p.Arguments[0].Value)}]";
        }
        if (ps.Name == "Count" && p.Instance != null && IsModelList(p.Instance))
            return $"({Expr(Strip(p.Instance))})->Count()";
        if (ps.IsIndexer && p.Instance != null && IsModelList(p.Instance))   // a model list's element: a class as a pointer, a string managed
            return $"gen::ModelElem(({Expr(Strip(p.Instance))})->items[{Expr(p.Arguments[0].Value)}])";
        switch (full)
        {
            case "string.Length": return $"gen::Len({Expr(p.Instance)})";
            case "string.this[]": return $"gen::CharAt({Expr(p.Instance)}, {Expr(p.Arguments[0].Value)})";
            case "System.Environment.TickCount64": return "gen::TickCount64()";
            case "System.Environment.TickCount": return "(int32_t)gen::TickCount64()";
            case "Microsoft.Xna.Framework.Color.White": return "gen::Color_White";
            case "Microsoft.Xna.Framework.Color.Black": return "gen::Color_Black";
            case "Microsoft.Xna.Framework.Color.Transparent": return "gen::Color_Transparent";
            case "Microsoft.Xna.Framework.Vector2.Zero": return "gen::Vector2_Zero";
            case "Microsoft.Xna.Framework.Matrix.Identity": return "gen::Matrix_Identity";
            case "Microsoft.Xna.Framework.Vector2.One": return "gen::Vector2_One";
            case "Microsoft.Xna.Framework.Vector2.UnitX": return "gen::Vector2_UnitX";
            case "Microsoft.Xna.Framework.Vector2.UnitY": return "gen::Vector2_UnitY";
            case "Microsoft.Xna.Framework.Color.PackedValue": return $"{Expr(p.Instance)}.packed";
            case "Microsoft.Xna.Framework.Color.R": case "Microsoft.Xna.Framework.Color.G": case "Microsoft.Xna.Framework.Color.B":
            case "Microsoft.Xna.Framework.Color.A": return $"{Expr(p.Instance)}.{ps.Name}()";
        }
        var o = OriginOf(owner);
        if (gen.Models.IsModel(owner) && !ps.IsStatic) return ModelGet(p);
        // a [service] rule on a game / BCL property (System.Diagnostics.Stopwatch.ElapsedMilliseconds): as for methods ($self = the receiver)
        if (o != Origin.Tfex && gen.ServiceRule(ps) != null &&
            Service(ps, i => i < p.Arguments.Length ? Expr(p.Arguments.OrderBy(a => a.Parameter.Ordinal).ElementAt(i).Value) : "", p, p.Instance) is string bsp) return bsp;
        // ($self = the receiver here too: StatusImpl.IsOk on a local wrapper, the ggrs-ffi port's FFI handle rules)
        if (o == Origin.Tfex && Service(ps, p.Arguments.OrderBy(a => a.Parameter.Ordinal).Select(a => Expr(a.Value)).ToList() is var pa ? (i => i < pa.Count ? pa[i] : "") : null, p, p.Instance) is string sp) return sp;
        if (o == Origin.Tfex && ps.GetMethod != null) methodCallees.Add(ps.GetMethod.OriginalDefinition);
        if (o == Origin.Tfex && !ps.IsStatic && ps.GetMethod != null && IsClassType(owner))
        {
            var recvP = Recv(p.Instance);
            if (SymbolEqualityComparer.Default.Equals(owner, type)) return $"{Id(ps.GetMethod.Name)}({recvP})";
            var pu2 = OtherUnit(owner) is TomlTable pu3 && !pu3.ContainsKey("auto") && !pu3.ContainsKey("members") ? pu3 : AutoU(owner, ps);
            includes.Add($"tfexgen_{pu2["name"]}.hpp");
            return $"tfex::gen::{pu2["name"]}::{Id(ps.GetMethod.Name)}({recvP})";
        }
        if (o == Origin.Tfex && ps.IsStatic && ps.GetMethod != null)
        {
            if (SymbolEqualityComparer.Default.Equals(owner, type)) return $"{Id(ps.GetMethod.Name)}()";
            if ((OtherUnit(owner) is TomlTable pu0 && !pu0.ContainsKey("auto") && !pu0.ContainsKey("members") ? pu0 : AutoU(owner, ps)) is TomlTable pu) { includes.Add($"tfexgen_{pu["name"]}.hpp"); return $"tfex::gen::{pu["name"]}::{Id(ps.GetMethod.Name)}()"; }
        }
        if (InBinary(owner) || (o == Origin.Bcl && ps.GetMethod != null))
        {
            var get = ps.GetMethod;
            if (get == null) return Unsupported(p, "property without getter " + full);
            var args = p.Arguments.OrderBy(a => a.Parameter.Ordinal).Select(a => Expr(a.Value)).ToList();
            var recv = ps.IsStatic ? null : Expr(p.Instance);
            // an auto-property whose getter BRUTE inlined: its backing field (the getter's `ldr` offset)
            // a virtual / abstract getter (PlayerInput.MenuAlt2Check): through its vtable slot (Call), never a body or a backing field
            bool virt = (get.IsVirtual || get.IsAbstract || get.IsOverride) && !owner.IsSealed && !get.IsSealed;
            if (InBinary(owner) && !ps.IsStatic && args.Count == 0 && !virt)
            {
                var key = B.Need("m", SwitchName(get.ContainingType), get.Name, IlSig(get));
                if (!Bound(key))
                    return $"gen::Fld<{Cpp(ps.Type)}>({recv}, {B.Need("f", SwitchName(owner), ps.Name)})";
            }
            return Call(get, recv, args, p);
        }
        // a mod's interop interface's property ([[interop]]): the provider class's generated getter, the API object the receiver
        if (!ps.IsStatic && ps.GetMethod != null && owner.TypeKind == TypeKind.Interface && gen.InteropImpl(owner, ps) is IPropertySymbol xp && xp.GetMethod != null &&
            (OtherUnit(xp.ContainingType) is TomlTable xpu0 && !xpu0.ContainsKey("auto") && !xpu0.ContainsKey("members") ? xpu0 : AutoU(xp.ContainingType, xp)) is TomlTable xpu)
        {
            var recvXP = Recv(p.Instance);
            methodCallees.Add(xp.GetMethod.OriginalDefinition);
            R.Add("mechanical: a property through a mod's interop interface -> the provider mod's class ([[interop]])", $"{owner.Name}.{ps.Name} -> {xp.ContainingType.Name}");
            includes.Add($"tfexgen_{xpu["name"]}.hpp");
            return $"tfex::gen::{xpu["name"]}::{Id(xp.GetMethod.Name)}({recvXP})";
        }
        // a TF.EX interface's property with one implementation in TF.EX's code and no [service] rule (IRngService.Gameplay -> RngService's):
        // that class's generated getter, as for methods
        if (o == Origin.Tfex && !ps.IsStatic && ps.GetMethod != null && owner.TypeKind == TypeKind.Interface && gen.ImplsOf(owner) is { Count: 1 } onep &&
            gen.ImplMember(onep[0], ps) is IPropertySymbol implP && implP.GetMethod != null && SymbolEqualityComparer.Default.Equals(implP.ContainingType, onep[0]))
        {
            var recvIP = Recv(p.Instance);
            methodCallees.Add(implP.GetMethod.OriginalDefinition);
            R.Add("mechanical: a property through a TF.EX interface -> its single TF.EX implementation (no [service] rule)", $"{owner.Name}.{ps.Name} -> {onep[0].Name}");
            if (SymbolEqualityComparer.Default.Equals(onep[0], type)) return $"{Id(implP.GetMethod.Name)}({recvIP})";
            var ipu = OtherUnit(onep[0]) is TomlTable ipu0 && !ipu0.ContainsKey("auto") && !ipu0.ContainsKey("members") ? ipu0 : AutoU(onep[0], implP);
            includes.Add($"tfexgen_{ipu["name"]}.hpp");
            return $"tfex::gen::{ipu["name"]}::{Id(implP.GetMethod.Name)}({recvIP})";
        }
        // ... implemented by several code mods' classes (LoaderAI's IAgentLogic.Type: AIJimmy's AILogic and AINova's NovaLogic): dispatch
        // on the object's class (MultiDispatch)
        if (o == Origin.Tfex && !ps.IsStatic && ps.GetMethod != null && owner.TypeKind == TypeKind.Interface && gen.ImplsOf(owner) is { Count: > 1 } manyP &&
            MultiDispatch(manyP, ps, Recv(p.Instance), new List<string>(), ps.Type) is string mdp)
            return mdp;
        // FNA's Rectangle edges (X, X + Width, Y, Y + Height)
        if (owner.ToDisplayString() == "Microsoft.Xna.Framework.Rectangle" && !ps.IsStatic && ps.Name is "Left" or "Right" or "Top" or "Bottom")
            return $"{Paren(p.Instance)}.{ps.Name}()";
        return Unsupported(p, "property " + full);
    }

    /* AccessTools.Method(typeof(T), "N"): T's method N when T is a game type and N names one method (or one with no parameters) */
    IMethodSymbol ReflectedMethod(IInvocationOperation inv)
    {
        // typeof(T).GetMethod("N"[, flags]): the same (the Switch build has no working reflection: GetMethod crashed CrumbleWall's state)
        if (inv.TargetMethod.Name == "GetMethod" && inv.TargetMethod.ContainingType.ToDisplayString() == "System.Type" &&
            KnownType(inv.Instance) is INamedTypeSymbol gt && (InBinary(gt) || OriginOf(gt) == Origin.Tfex) && inv.Arguments.Length >= 1 &&
            inv.Arguments.OrderBy(x => x.Parameter.Ordinal).First().Value.ConstantValue is { HasValue: true, Value: string gn })
        {
            for (var bt = gt; bt != null; bt = bt.BaseType)   // (FortRise's Mod.SaveSettings: a mod's own type's base)
            {
                var gms = bt.GetMembers(gn).OfType<IMethodSymbol>().ToList();
                if (gms.Count > 0) return gms.Count == 1 ? gms[0] : gms.FirstOrDefault(x => x.Parameters.Length == 0);
            }
            return null;
        }
        var tm = inv.TargetMethod.ToDisplayString();
        // AccessTools.[Declared]PropertySetter / PropertyGetter(typeof(T), "N"): the accessor (Teams+'s DataHooks sets ArcherData.Teams, a
        // private setter, through it)
        var acc = tm.StartsWith("HarmonyLib.AccessTools.PropertySetter") || tm.StartsWith("HarmonyLib.AccessTools.DeclaredPropertySetter") ? "set"
                : tm.StartsWith("HarmonyLib.AccessTools.PropertyGetter") || tm.StartsWith("HarmonyLib.AccessTools.DeclaredPropertyGetter") ? "get" : null;
        if (!(acc != null || tm.StartsWith("HarmonyLib.AccessTools.Method") || tm.StartsWith("HarmonyLib.AccessTools.DeclaredMethod")) || inv.Arguments.Length < 2) return null;
        var a = inv.Arguments.OrderBy(x => x.Parameter.Ordinal).ToList();
        if (Strip(a[0].Value) is not ITypeOfOperation { TypeOperand: INamedTypeSymbol rt } || a[1].Value.ConstantValue is not { HasValue: true, Value: string rn } || !InBinary(rt)) return null;
        if (acc != null) return rt.GetMembers(rn).OfType<IPropertySymbol>().FirstOrDefault() is IPropertySymbol rp ? (acc == "set" ? rp.SetMethod : rp.GetMethod) : null;
        var ms = rt.GetMembers(rn).OfType<IMethodSymbol>().ToList();
        return ms.Count == 1 ? ms[0] : ms.FirstOrDefault(x => x.Parameters.Length == 0);
    }
    /* a System.Type value known at translate time: typeof(T), or the variable of a foreach unrolled over a list of typeof(T)s
     * (UnitEmitter.ForEach: FortRise's mod template, `foreach (var t in Hookables) t.GetMethod("Load").Invoke(null, [harmony])`) */
    public readonly Dictionary<ILocalSymbol, INamedTypeSymbol> typeLocals = new(SymbolEqualityComparer.Default);
    IOperation reflectedRecv;   /* the receiver of a `mi?.Invoke(...)` being emitted as its direct call */
    INamedTypeSymbol KnownType(IOperation op) =>
        Strip(op) switch
        {
            ITypeOfOperation { TypeOperand: INamedTypeSymbol t } => t,
            ILocalReferenceOperation l when typeLocals.TryGetValue(l.Local, out var lt) => lt,
            _ => null,
        };
    /* the elements of an `object[]` argument written as an array creation or a collection expression ([a, b]) */
    static List<IOperation> ArgElements(IOperation op) =>
        Strip(op) switch
        {
            IArrayCreationOperation { Initializer: { } ai } => ai.ElementValues.ToList(),
            ICollectionExpressionOperation ce => ce.Elements.ToList(),
            _ => null,
        };
    /* the game method a MethodInfo value stands for: a reflection call inline, or a static field made by one */
    IMethodSymbol MethodFrom(IOperation op) =>
        Strip(op) switch
        {
            IInvocationOperation ri => ReflectedMethod(ri),
            IFieldReferenceOperation rf when FieldInit(rf.Field) is IInvocationOperation fi => ReflectedMethod(fi),
            // a local holding one (TF.State's PlayerExtensions.ToActions: `var m = typeof(Player).GetMethod(...)`): its initializer
            ILocalReferenceOperation rl when rl.Local.DeclaringSyntaxReferences.FirstOrDefault()?.GetSyntax() is Microsoft.CodeAnalysis.CSharp.Syntax.VariableDeclaratorSyntax { Initializer: { } li } ld &&
                                             ld.SyntaxTree == model.SyntaxTree && M(li.Value).GetOperation(li.Value) is IInvocationOperation lio => ReflectedMethod(lio),
            _ => null,
        };
    /* the game field an AccessTools.StaticFieldRefAccess<F>(AccessTools.Field(typeof(X), "n")) / FieldRefAccess<T, F>("n") names */
    IFieldSymbol RefField(IInvocationOperation fi)
    {
        var a = fi.Arguments.OrderBy(x => x.Parameter.Ordinal).ToList();
        if (fi.TargetMethod.Name == "StaticFieldRefAccess" && a.Count == 1 && Strip(a[0].Value) is IInvocationOperation { TargetMethod.Name: "Field" } ff)
        {
            var fa = ff.Arguments.OrderBy(x => x.Parameter.Ordinal).ToList();
            if (fa.Count == 2 && Strip(fa[0].Value) is ITypeOfOperation { TypeOperand: INamedTypeSymbol ft } && fa[1].Value.ConstantValue is { HasValue: true, Value: string fn })
                return ft.GetMembers(fn).OfType<IFieldSymbol>().FirstOrDefault();
        }
        if (fi.TargetMethod.Name == "FieldRefAccess" && fi.TargetMethod.TypeArguments.Length == 2 && a.Count == 1 && a[0].Value.ConstantValue is { HasValue: true, Value: string n2 } &&
            fi.TargetMethod.TypeArguments[0] is INamedTypeSymbol ot)
            for (var c = ot; c != null; c = c.BaseType) if (c.GetMembers(n2).OfType<IFieldSymbol>().FirstOrDefault() is IFieldSymbol r) return r;
        return null;
    }
    /* MonoMod's ModInterop: a [ModImportName("N")] class's static delegate fields (a mod's TfExAPIModImports.RegisterNetplayVersusMode),
     * filled by reflection with the [ModExportName("N")] static methods of the same names. The Switch has no reflection: the unit that
     * translates the export class (translate.toml [mod_exports] N = unit) is called directly (ModImportCall), and the registration
     * (typeof(X).ModInterop()) is nothing. */
    string ModImportUnit(IFieldSymbol f) =>
        f.ContainingType?.GetAttributes().FirstOrDefault(a => a.AttributeClass?.Name == "ModImportNameAttribute")?.ConstructorArguments.FirstOrDefault().Value is string n &&
        cfg.Table("mod_exports").TryGetValue(n, out var u) && u is string us ? us : null;
    string ModImportCall(IFieldSymbol f, string unitName, IInvocationOperation inv)
    {
        includes.Add($"tfexgen_{unitName}.hpp");
        R.Add("mechanical: a mod import's Invoke -> its export's function", $"{f.ContainingType.Name}.{f.Name} -> {unitName}");
        return $"tfex::gen::{unitName}::{Id(f.Name)}({string.Join(", ", inv.Arguments.OrderBy(a => a.Parameter.Ordinal).Select(a => Expr(a.Value)))})";
    }

    /* a static delegate field made by reflection over a static property's setter (TF.EX's LevelPatch: `static readonly Action<float>
     * SetEngineTimeMult = StaticSetter<float>(nameof(Engine.TimeMult))`, StaticSetter's body `AccessTools.PropertySetter(typeof(Engine),
     * property)?.CreateDelegate<Action<T>>()`): the property it sets, else null. The Switch has no reflection-made delegates: an invoke of
     * the field is the setter's call (SetterInvoke), and the field itself isn't emitted. Found inline in the initializer, or in the body of
     * the TF.EX helper it calls, with the helper's string parameter bound to the call's constant argument. */
    IPropertySymbol ReflectedSetterField(IFieldSymbol f)
    {
        if (!f.IsStatic || f.Type is not INamedTypeSymbol { TypeKind: TypeKind.Delegate } || FieldInit(f) is not IInvocationOperation init) return null;
        return ReflectedSetter(init);
    }
    IPropertySymbol ReflectedSetter(IInvocationOperation init)
    {
        static bool IsPropSetter(IInvocationOperation i) => i.TargetMethod.ContainingType?.ToDisplayString() == "HarmonyLib.AccessTools" &&
                                                            i.TargetMethod.Name is "PropertySetter" or "DeclaredPropertySetter";
        IPropertySymbol Resolve(IInvocationOperation ps, Func<IParameterSymbol, string> param)
        {
            var a = ps.Arguments.OrderBy(x => x.Parameter.Ordinal).ToList();
            if (a.Count != 2 || Strip(a[0].Value) is not ITypeOfOperation { TypeOperand: INamedTypeSymbol pt }) return null;
            var name = a[1].Value.ConstantValue is { HasValue: true, Value: string n } ? n
                     : Strip(a[1].Value) is IParameterReferenceOperation pr && param != null ? param(pr.Parameter) : null;
            if (name == null) return null;
            for (var c = pt; c != null; c = c.BaseType)
                if (c.GetMembers(name).OfType<IPropertySymbol>().FirstOrDefault(p => p.SetMethod != null) is IPropertySymbol r) return r;
            return null;
        }
        if (init.DescendantsAndSelf().OfType<IInvocationOperation>().FirstOrDefault(IsPropSetter) is IInvocationOperation inl) return Resolve(inl, null);
        // mi.CreateDelegate(typeof(D)) with mi a reflected setter held in a local / field (Soccer's `setter.CreateDelegate(...)`)
        if (init.TargetMethod.Name == "CreateDelegate" && init.Instance != null && MethodFrom(init.Instance) is IMethodSymbol { AssociatedSymbol: IPropertySymbol cdp } cdm &&
            SymbolEqualityComparer.Default.Equals(cdm, cdp.SetMethod)) return cdp;
        // a TF.EX helper with the reflection in its body (one PropertySetter call, its delegate made there)
        var hm = init.TargetMethod.OriginalDefinition;
        if (OriginOf(hm.ContainingType) != Origin.Tfex || hm.ReturnType.TypeKind != TypeKind.Delegate ||
            hm.DeclaringSyntaxReferences.FirstOrDefault()?.GetSyntax() is not Microsoft.CodeAnalysis.CSharp.Syntax.MethodDeclarationSyntax md) return null;
        SyntaxNode body = (SyntaxNode)md.Body ?? md.ExpressionBody;
        if (body == null || ws.Model(md.SyntaxTree).GetOperation(body) is not IOperation bop) return null;
        var setters = bop.DescendantsAndSelf().OfType<IInvocationOperation>().Where(IsPropSetter).ToList();
        if (setters.Count != 1 || !bop.DescendantsAndSelf().OfType<IInvocationOperation>().Any(i => i.TargetMethod.Name == "CreateDelegate")) return null;
        var callArgs = init.Arguments.ToDictionary(x => x.Parameter.Ordinal, x => x.Value);
        return Resolve(setters[0], p => callArgs.TryGetValue(p.Ordinal, out var v) && v.ConstantValue is { HasValue: true, Value: string s } ? s : null);
    }
    /* an invoke of such a field: the property's setter, called */
    string SetterInvoke(IPropertySymbol p, IFieldSymbol f, IOperation arg, IOperation at)
    {
        R.Add("mechanical: a delegate made by reflection over a static property setter (AccessTools.PropertySetter + CreateDelegate), invoked -> the setter's call",
              $"{f.ContainingType.Name}.{f.Name}: {p.ContainingType.Name}.{p.Name}");
        return Call(p.SetMethod, null, new List<string> { Expr(arg) }, at);
    }
    /* TF.State's IEnumerable<T> parameter of a game value type T the game has a List<T> of: that List type (else null) */
    INamedTypeSymbol StateEnumListOf(IParameterSymbol p) => p?.ContainingSymbol is IMethodSymbol pm ? StateEnumListOf(p.Type, pm) : null;
    /* the same for a TF.State method's IEnumerable<T> result that isn't a Snap (OwnShuffle returns its List) */
    INamedTypeSymbol StateEnumListOf(ITypeSymbol t, IMethodSymbol pm)
    {
        if (t is not INamedTypeSymbol { OriginalDefinition.SpecialType: SpecialType.System_Collections_Generic_IEnumerable_T } et) return null;
        if (OriginOf(pm.ContainingType) != Origin.Tfex) return null;
        if (pm.ContainingType.DeclaringSyntaxReferences.FirstOrDefault()?.SyntaxTree.FilePath.StartsWith("state/") != true) return null;
        var el = et.TypeArguments[0] is ITypeParameterSymbol && subst.Count > 0 ? Sub(et.TypeArguments[0]) : et.TypeArguments[0];
        var list1 = ws.Compilation.GetTypeByMetadataName("System.Collections.Generic.List`1")!;
        // a TF.State model class (StateContext.LoadDesiredSfx's IEnumerable<SFX>): the game's List<object>, the models' own collections' form
        if (el is INamedTypeSymbol { TypeKind: TypeKind.Class } && OriginOf(el) == Origin.Tfex)
        {
            var lo = list1.Construct(ws.Compilation.GetSpecialType(SpecialType.System_Object));
            return GameLacks(lo) ? null : lo;
        }
        if (!(el.SpecialType is SpecialType.System_Int32 or SpecialType.System_Single or SpecialType.System_String || el.ToDisplayString() == "Microsoft.Xna.Framework.Vector2")) return null;
        var lt = list1.Construct(el);
        return GameLacks(lt) ? null : lt;
    }
    /* a field's initializer expression (static readonly X = ...) */
    IOperation FieldInit(IFieldSymbol f)
    {
        var syn = f.DeclaringSyntaxReferences.FirstOrDefault()?.GetSyntax() as Microsoft.CodeAnalysis.CSharp.Syntax.VariableDeclaratorSyntax;
        if (syn?.Initializer?.Value is { } v) return Strip(ws.Model(syn.SyntaxTree).GetOperation(v));
        return ReflectionAssignment(f);
    }
    /* a static field of ours assigned once in its class (outside catch blocks, which never run on the Switch) from a reflection lookup
     * (AccessTools.Field / Method / PropertySetter, Type.GetField / GetMethod, MethodInfo.CreateDelegate): that value, as an initializer
     * (Soccer's FlatPlay.Load: `moveAxis = AccessTools.Field(typeof(Player), "moveAxis")`, `setGround = (Action<Player, bool>)setter.CreateDelegate(...)`) */
    readonly Dictionary<IFieldSymbol, IOperation> reflAssign = new(SymbolEqualityComparer.Default);
    IOperation ReflectionAssignment(IFieldSymbol f)
    {
        if (!f.IsStatic || f.IsConst || OriginOf(f.ContainingType) != Origin.Tfex || InBinary(f.ContainingType)) return null;
        if (reflAssign.TryGetValue(f, out var cached)) return cached;
        IOperation found = null; int n = 0;
        foreach (var r in f.ContainingType.DeclaringSyntaxReferences)
        {
            var root = r.GetSyntax();
            var model = M(root);   // (the field's own compilation: a code mod's AINova.NovaLogic, not TF.EX's)
            foreach (var asg in root.DescendantNodes().OfType<Microsoft.CodeAnalysis.CSharp.Syntax.AssignmentExpressionSyntax>())
            {
                if (asg.Ancestors().Any(x => x is Microsoft.CodeAnalysis.CSharp.Syntax.CatchClauseSyntax)) continue;
                if (!SymbolEqualityComparer.Default.Equals(model.GetSymbolInfo(asg.Left).Symbol, f)) continue;
                n++;
                found = Strip(model.GetOperation(asg.Right));
                while (found is IConversionOperation fc) found = Strip(fc.Operand);   // ((Action<Player, bool>)mi.CreateDelegate(...): the cast's operand)
            }
        }
        bool reflection = n == 1 && found != null && found.DescendantsAndSelf().OfType<IInvocationOperation>().Any(i =>
            i.TargetMethod.ContainingType?.ToDisplayString() is "HarmonyLib.AccessTools" or "System.Type" or "System.Reflection.MethodInfo" or "System.Delegate" ||
            i.TargetMethod.Name == "CreateDelegate");
        return reflAssign[f] = reflection ? found : null;
    }
    /* a field whose only value is a reflection lookup resolved at translate time (its uses are direct code): no storage, no assignment */
    bool ResolvedReflectionField(IFieldSymbol f) =>
        ReflectionAssignment(f) is IOperation ra && (ReflectedSetterField(f) != null || ReflectedField(ra) != null || MethodFrom(ra) != null);

    /* a lobby model's dictionary member (CustomMod.Data: Dictionary<string, string> -> StrMap) */
    bool IsModelMap(IOperation op) =>
        (Strip(op) is IPropertyReferenceOperation { Property.IsStatic: false } mp && gen.Models.IsModel(mp.Property.ContainingType) &&
         gen.Models.MemberOf(mp.Property.ContainingType, mp.Property.Name)?.CppType is string mct && (mct.StartsWith("StrMap<") || mct.StartsWith("KeyMap<"))) ||
        // a Dictionary<string, string> parameter its callers pass a model's map (WiderSetModApiData.CanUseWiderSet(mod.Data, ...))
        (Strip(op) is IParameterReferenceOperation pr && ModelListParamCpp(pr.Parameter) is string mpc && mpc.Contains("StrMap<"));

    /* a lobby model's property: a serialized one is the struct's member, a computed one the model unit's get_X(self) */
    string ModelGet(IPropertyReferenceOperation p)
    {
        var ps = p.Property;
        var recv = Recv(p.Instance);
        var mem = gen.Models.MemberOf(ps.ContainingType, ps.Name);
        includes.Add("tfexgen_Models.hpp");
        if (mem == null)
        {
            if (ps.GetMethod == null) return Unsupported(p, "model property without getter");
            methodCallees.Add(ps.GetMethod.OriginalDefinition);
            if (SymbolEqualityComparer.Default.Equals(ps.ContainingType, type)) return $"{Id(ps.GetMethod.Name)}({recv})";
            var u = OtherUnit(ps.ContainingType) is TomlTable mu0 && !mu0.ContainsKey("auto") && !mu0.ContainsKey("members") ? mu0 : AutoU(ps.ContainingType, ps);
            if (u == null) return Unsupported(p, "model property " + ps.Name);
            includes.Add($"tfexgen_{u["name"]}.hpp");
            return $"tfex::gen::{u["name"]}::{Id(ps.GetMethod.Name)}({recv})";
        }
        var acc = $"({recv})->{ps.Name}";
        if (mem.CppType.StartsWith("Str<")) return $"gen::Managed({acc})";
        if (mem.CppType == "MStr") return $"({acc}).Get()";   // a managed string reference ([models.caps] "managed")
        if (mem.CppType.StartsWith("Opt<ws::")) return $"gen::RefOf({acc})";
        if (mem.CppType.StartsWith("List<") || (mem.CppType.StartsWith("StrMap<") || mem.CppType.StartsWith("KeyMap<"))) return "&" + acc;
        return acc;
    }

    /* model.List = <a managed list / LINQ chain / snapshot> (not another model's list): the elements copied (gen::SetModelList) */
    /* an array-typed value as gen::Arr (a null literal as an empty one, so .obj is null) */
    string ArrValue(IOperation v)
    {
        var s = Strip(v);
        if (s.ConstantValue is { HasValue: true, Value: null } || (s is IConversionOperation c && c.Operand.ConstantValue is { HasValue: true, Value: null })) return "gen::Arr<void*>{}";
        return Expr(v);
    }

    string ModelListFromSeq(ISimpleAssignmentOperation a)
    {
        if (a.Target is not IPropertyReferenceOperation p || !gen.Models.IsModel(p.Property.ContainingType) || p.Property.IsStatic) return null;
        return ModelListAssign(p.Property.ContainingType, p.Property.Name, $"({Recv(p.Instance)})->{p.Property.Name}", a.Value, a);
    }

    /* a model's list of numbers / strings (not of models) set from a collection or sequence: its elements copied; `x?.ToList()` leaves
     * the list null when x is (null if the value is another model list or new List<T>(...): the plain rule) */
    string ModelListAssign(ITypeSymbol model, string prop, string acc, IOperation value, IOperation at)
    {
        var mem = gen.Models.MemberOf(model, prop);
        if (mem == null || !mem.CppType.StartsWith("List<") || mem.CppType.StartsWith("List<ws::") || mem.CppType.StartsWith("List<List<")) return null;
        var v = Strip(value);
        while (v is IConversionOperation c) v = c.Operand;
        if (IsModelList(v)) return null;
        if (v is IObjectCreationOperation) return null;   // new List<T>(model list): the existing rule
        if (v is IConditionalAccessOperation ca && Strip(ca.WhenNotNull) is IInvocationOperation { TargetMethod.Name: "ToList" or "ToArray" } wl && IsEnumerable(wl.TargetMethod) &&
            Strip(ca.Operation) is ILocalReferenceOperation or IParameterReferenceOperation)
        {
            R.Add("rule: a model's list assigned from a collection -> its elements copied", Where(at));
            return $"([&]() {{ if (!({Expr(ca.Operation)})) {acc} = std::remove_reference_t<decltype({acc})>::Null(); else gen::SetModelList({acc}, {SeqOfCollection(Strip(ca.Operation))}); }}())";
        }
        // x.Where(..).ToList() / ToArray(): the elements are copied anyway, so straight from the sequence (not a materialized game List)
        if (v is IInvocationOperation { TargetMethod.Name: "ToList" or "ToArray" } tl && IsEnumerable(tl.TargetMethod) && tl.Arguments.Length == 1)
            v = Strip(tl.Arguments[0].Value);
        R.Add("rule: a model's list assigned from a collection -> its elements copied", Where(at));
        return $"gen::SetModelList({acc}, {SeqOfCollection(v)})";
    }
    string SeqOfCollection(IOperation v) =>
        v.Type is IArrayTypeSymbol ? $"gen::FromArr({Expr(v)})" : v.Type is INamedTypeSymbol { OriginalDefinition.MetadataName: "List`1" } lt && !(v is IInvocationOperation li && IsEnumerable(li.TargetMethod))
            ? $"gen::FromList<{Cpp(lt.TypeArguments[0])}>({Expr(v)}, {MethodBody(lt, "get_Count", null)}, {MethodBody(lt, "get_Item", new[] { "int32" })})" : SeqOf(v);

    string ModelSet(IPropertyReferenceOperation p, string value)
    {
        var ps = p.Property;
        var recv = Recv(p.Instance);
        var mem = gen.Models.MemberOf(ps.ContainingType, ps.Name);
        if (mem == null) return Unsupported(p, "computed model property set " + ps.Name);
        var acc = $"({recv})->{ps.Name}";
        if (mem.CppType.StartsWith("Str<")) return $"gen::SetStr({acc}, {value})";
        if (mem.CppType == "MStr") return $"({acc}).Set({value})";
        if (mem.CppType.StartsWith("Opt<ws::")) { R.Add("rule: model reference assignment copies the object", Where(p)); return $"gen::SetRef({acc}, {value})"; }
        if (mem.CppType.StartsWith("List<") || (mem.CppType.StartsWith("StrMap<") || mem.CppType.StartsWith("KeyMap<"))) { R.Add("rule: model collection assignment copies the collection", Where(p)); return $"({acc} = *({value}))"; }
        return $"{acc} = {value}";
    }

    /* a managed array from the game -> gen::Arr<T> (data, length through the bound Array.get_Length, the object for passing back) */
    /* [a, b, ...] (C# 12): an array (gen::NewArr) or a game List<T> (List`1's constructor + Add), for List<T> and the collection
     * interfaces; a model's collection is the model rule's; spreads (..x) are not supported */
    /* new Dictionary<,>() / HashSet<> / List<> of a value collection, a collection expression ([] / [a, b]) or an initializer: a static
     * field's own object, or one of the ring's in a method */
    /* the allocator of a value container a LINQ materialization (ToList, Select(...).ToList(), ToDictionary, an argument copy) makes: in TF.State's
     * code GC memory, kept as long as the model holding it (a GameState has more int lists than the ring's 8: player 0's arrows came out
     * as another list's, 2026-10-22); elsewhere the ring of temporaries */
    string NewValFn() => StateMemory ? "NewValGc" : "NewVal";
    /* TF.State's code and its glue (the serializer GlueStateMsgPack, GlueState): their containers end up in GameState models, which hold more
     * of them than the ring's 8 (deserializing a state, player 0's arrows came back as another list's: [19], 2026-10-22) */
    bool StateMemory => file.StartsWith("state/") || ((string)unit["name"]).StartsWith("GlueState");

    string NewValColl(ITypeSymbol t, IOperation at, IEnumerable<IOperation> items)
    {
        // a container that outlives the call (a field's: an initializer, or assigned to a field) gets its own storage; the ring of 8 is for
        // temporaries only (MatchmakingService's sets in fields were reset by later temporaries: Integration A)
        bool inStatic = false, instanceInit = false;
        for (var p = at.Parent; p != null; p = p.Parent)
        {
            if (p is IFieldInitializerOperation fio) { inStatic = true; instanceInit = fio.InitializedFields.Any(f => !f.IsStatic); }
            if (p is IPropertyInitializerOperation pio) { inStatic = true; instanceInit = pio.InitializedProperties.Any(q => !q.IsStatic); }
            // assigned to a field in a method (ResetPeer: _skinKnownPeers = new HashSet()): built in the ring, then copied into the field's
            // own storage (Assign: gen::KeepVal), made once; a NewValStatic per call would leak one container per call
            if (p is IBlockOperation || p is IAnonymousFunctionOperation) break;
        }
        var ct = ValCpp(t);
        R.Add("rule: a collection the game has no code for -> a C++ container in scanned GC memory (gen::ValMap/ValSet/VList)", Where(at));
        // (an instance field's initializer in TF.State's code, whose model objects are made every tick and are ordinary GC objects: made with
        // the object, collected with it, gen::NewValGc. Other units' objects can live in module memory, which the GC doesn't scan
        // (NetplayManager's state, eb6556e): there it stays NewValStatic)
        instanceInit = instanceInit && StateMemory;
        // TF.State's code outside static initializers: its containers are kept in model objects (BrambleSpreadState.Clone's lists), so the
        // ring of 8 temporaries would be reused under them; GC memory, collected with the model, as the instance initializers
        bool stateGc = instanceInit || (StateMemory && !inStatic);
        var sb = new StringBuilder($"([&]() {{ auto* v_ = gen::{(stateGc ? "NewValGc" : inStatic ? "NewValStatic" : "NewVal")}<{ct}>();");
        // new List<T>(source) / new HashSet<T>(source): the source's items (C#'s copy constructor); a capacity argument changes nothing
        if (at is IObjectCreationOperation { Arguments: [var src1] } && Strip(src1.Value).Type is ITypeSymbol st1 && st1.SpecialType != SpecialType.System_Int32 &&
            t is INamedTypeSymbol { TypeArguments.Length: 1 } vt1)
        {
            R.Add("mechanical: new List<T>(source) of a C++ container -> the source's items added", Where(at));
            sb.Append($" {SeqOf(Strip(src1.Value))}([&]({Cpp(vt1.TypeArguments[0])} x_) {{ v_->Add(x_); return true; }});");
        }
        // new Dictionary<K, V>(source): the source's entries (C#'s copy constructor; it was an empty map before 2026-10-29)
        if (at is IObjectCreationOperation { Arguments: [var src2] } && Strip(src2.Value).Type is INamedTypeSymbol { TypeArguments.Length: 2 } &&
            t is INamedTypeSymbol { TypeArguments.Length: 2 } vt2)
        {
            R.Add("mechanical: new Dictionary<K, V>(source) of a C++ container -> the source's entries set", Where(at));
            sb.Append($" gen::FromValMap({Expr(Strip(src2.Value))})([&](gen::KV<{Cpp(vt2.TypeArguments[0])}, {Cpp(vt2.TypeArguments[1])}> kv_) {{ v_->Set(kv_.Key, kv_.Value); return true; }});");
        }
        foreach (var it in items ?? Enumerable.Empty<IOperation>())
        {
            // an indexer initializer ({ [k] = v }): Set
            if (it is ISimpleAssignmentOperation { Target: IPropertyReferenceOperation { Property.IsIndexer: true } ix } ias)
            {
                sb.Append($" v_->Set({Expr(ix.Arguments[0].Value)}, {Expr(ias.Value)});");
                continue;
            }
            var item = it is IInvocationOperation ai && ai.TargetMethod.Name == "Add" ? string.Join(", ", ai.Arguments.OrderBy(a => a.Parameter.Ordinal).Select(a => Expr(a.Value))) : Expr(it);
            sb.Append($" v_->Add({item});");
        }
        return sb.Append(" return v_; }())").ToString();
    }

    string CollectionExpr(ICollectionExpressionOperation ce)
    {
        if (IsValColl(ce.Type) && ce.Elements.Any(e => e is ISpreadOperation))
        {   // [a, .. seq] into a HashSet / Dictionary-less container: the container, every element added in order
            if (ce.Type is not INamedTypeSymbol { TypeArguments.Length: 1 } vt) return Unsupported(ce, "CollectionExpression with a spread");
            var vsb = new StringBuilder($"([&]() {{ auto* v_ = gen::{NewValFn()}<{ValCpp(ce.Type)}>();");
            foreach (var el in ce.Elements)
                if (el is ISpreadOperation sp) vsb.Append($" {SeqOf(sp.Operand)}([&]({Cpp(vt.TypeArguments[0])} x_) {{ v_->Add(x_); return true; }});");
                else vsb.Append($" v_->Add({Expr(el)});");
            R.Add("rule: a collection the game has no code for -> a C++ container in scanned GC memory (gen::ValMap/ValSet/VList)", Where(ce));
            return vsb.Append(" return v_; }())").ToString();
        }
        if (IsValColl(ce.Type)) return NewValColl(ce.Type, ce, ce.Elements);
        // [a, .. seq, b] into a List<T> (or its interfaces): the List, Add per item, every element of a spread added in order
        if (ce.Elements.Any(e => e is ISpreadOperation) && ce.Type is INamedTypeSymbol { TypeArguments.Length: 1 } snt && !IsModelCollection(snt) &&
            snt.Name is "List" or "IList" or "ICollection" or "IEnumerable" or "IReadOnlyList" or "IReadOnlyCollection")
        {
            var slist = ws.Compilation.GetTypeByMetadataName("System.Collections.Generic.List`1")!.Construct(snt.TypeArguments[0]);
            var sctor = slist.InstanceConstructors.First(c => c.Parameters.Length == 0);
            var sadd = slist.GetMembers("Add").OfType<IMethodSymbol>().First(m => m.Parameters.Length == 1);
            var sv = $"l_{tmp++}";
            var ssb = new StringBuilder($"([&]() {{ void* {sv} = {Call(sctor, "nullptr", new List<string>(), ce)};");
            foreach (var el in ce.Elements)
                if (el is ISpreadOperation sp) ssb.Append($" {SeqOf(sp.Operand)}([&]({Cpp(snt.TypeArguments[0])} x_) {{ {Call(sadd, sv, new List<string> { "x_" }, ce)}; return true; }});");
                else ssb.Append($" {Call(sadd, sv, new List<string> { Expr(el) }, ce)};");
            R.Add("mechanical: collection expression with a spread -> new List<T> + Add", Where(ce));
            return ssb.Append($" return {sv}; }}())").ToString();
        }
        // [a, .. seq, b] into T[]: every element in order into a snapshot, then a managed array of it (as ToArray)
        if (ce.Elements.Any(e => e is ISpreadOperation) && ce.Type is IArrayTypeSymbol sat && !gen.Models.IsModel(sat.ElementType) && !IsNullable(sat.ElementType))
        {
            var sel = Cpp(sat.ElementType);
            var asb = new StringBuilder($"([&]() {{ gen::Snap<{sel}> s_;");
            foreach (var el in ce.Elements)
                if (el is ISpreadOperation sp) asb.Append($" {SeqOf(sp.Operand)}([&]({sel} x_) {{ s_.Add(x_); return true; }});");
                else asb.Append($" s_.Add({Expr(el)});");
            R.Add("mechanical: collection expression with a spread -> T[] (snapshot, then the array)", Where(ce));
            return asb.Append($" auto a_ = {NewArrOf(sat, "s_.count")}; for (int i_ = 0; a_.data && i_ < s_.count; i_++) a_.data[i_] = s_.items[i_]; return a_; }}())").ToString();
        }
        // [.. seq] into a collection of lobby models (MatchmakingService.GetEndGameStatus: IEnumerable<EndGameStatus>): a temporary model list
        if (ce.Elements.Length == 1 && ce.Elements[0] is ISpreadOperation msp && IsModelCollection(ce.Type) &&
            ce.Type is INamedTypeSymbol { TypeArguments.Length: 1 } mct && gen.Models.IsModel(mct.TypeArguments[0]))
        {
            includes.Add("tfexgen_Models.hpp");
            R.Add("mechanical: [.. seq] into a model collection -> a temporary model list", Where(ce));
            return $"gen::TempModelList<gen::List<tfex::gen::ws::{mct.TypeArguments[0].Name}, {gen.Models.ListCapacityOf(mct.TypeArguments[0].Name)}>>({SeqOf(msp.Operand)})";
        }
        if (ce.Elements.Any(e => e is ISpreadOperation)) return Unsupported(ce, "CollectionExpression with a spread");
        // [a, b] for an auto-property's ICollection<string> (PropCpp: a game List<string>): new List<string>() + Add
        if (IsStringCollectionGuess(ce.Type) && ce.Parent is IPropertyInitializerOperation or IConversionOperation { Parent: IPropertyInitializerOperation })
        {
            var glist = ws.Compilation.GetTypeByMetadataName("System.Collections.Generic.List`1")!.Construct(((INamedTypeSymbol)ce.Type).TypeArguments[0]);
            var gctor = glist.InstanceConstructors.First(c => c.Parameters.Length == 0);
            var gadd = glist.GetMembers("Add").OfType<IMethodSymbol>().First(m => m.Parameters.Length == 1);
            var gv = $"l_{tmp++}";
            var gsb = new StringBuilder($"([&]() {{ void* {gv} = {Call(gctor, "nullptr", new List<string>(), ce)};");
            foreach (var el in ce.Elements) gsb.Append($" {Call(gadd, gv, new List<string> { Expr(el) }, ce)};");
            R.Add("mechanical: collection expression for an ICollection<string> property -> new List<string> + Add", Where(ce));
            return gsb.Append($" return {gv}; }}())").ToString();
        }
        var items = ce.Elements.Select(Expr).ToList();
        if (ce.Type is IArrayTypeSymbol at)
        {
            if (gen.Models.IsModel(at.ElementType) || IsNullable(at.ElementType)) return Unsupported(ce, "CollectionExpression");
            var el = Cpp(at.ElementType);
            var sb0 = new StringBuilder($"([&]() {{ auto a_ = {NewArrOf(at, items.Count.ToString())};");
            for (int i = 0; i < items.Count; i++) sb0.Append($" if (a_.data) a_.data[{i}] = {items[i]};");
            return sb0.Append(" return a_; }())").ToString();
        }
        // `= []` for a Dictionary / HashSet / List of ours (FortRise's ArcherRegistry): a new empty one
        if (ce.Elements.Length == 0 && Sub(ce.Type) is INamedTypeSymbol { TypeKind: TypeKind.Class } et0 && et0.ContainingNamespace?.ToDisplayString() == "System.Collections.Generic" &&
            !IsValColl(et0) && !IsModelCollection(et0) && et0.InstanceConstructors.FirstOrDefault(c => c.Parameters.Length == 0) is IMethodSymbol ector)
        {
            R.Add("mechanical: an empty collection expression -> the collection's new empty instance", Where(ce));
            return Call(ector, "nullptr", new List<string>(), ce);
        }
        if (ce.Type is not INamedTypeSymbol nt || nt.TypeArguments.Length != 1 || nt.ContainingNamespace?.ToDisplayString() != "System.Collections.Generic" || IsModelCollection(nt))
            return Unsupported(ce, "CollectionExpression");
        var elt = nt.TypeArguments[0];
        if (gen.Models.IsModel(elt) || IsNullable(elt)) return Unsupported(ce, "CollectionExpression");
        var listDef = ws.Compilation.GetTypeByMetadataName("System.Collections.Generic.List`1");
        var list = listDef?.Construct(elt);
        if (list == null || !(nt.Name is "List" or "IList" or "ICollection" or "IEnumerable" or "IReadOnlyList" or "IReadOnlyCollection"))
            return Unsupported(ce, "CollectionExpression");
        var ctor = list.InstanceConstructors.FirstOrDefault(c => c.Parameters.Length == 0);
        var add = list.GetMembers("Add").OfType<IMethodSymbol>().FirstOrDefault(m => m.Parameters.Length == 1);
        if (ctor == null || add == null) return Unsupported(ce, "CollectionExpression");
        R.Add("mechanical: collection expression -> new List<T> + Add", Where(ce));
        var v = $"l_{tmp++}";
        var sb = new StringBuilder($"([&]() {{ void* {v} = {Call(ctor, "nullptr", new List<string>(), ce)};");
        foreach (var it in items) sb.Append($" {Call(add, v, new List<string> { it }, ce)};");
        return sb.Append($" return {v}; }}())").ToString();
    }

    /* new T[n] / new T[] { ... } / a params array: a managed array (BRUTE's NewArray, gen::NewArr), so game code can take it (.obj) */
    /* a new T[n]: the game's array of T; of a tuple or a TF.EX struct (no such array type in the Switch build: TF.State's Vector2f[],
     * TF.EX Scenario's (int X, int Y)[]) a game Object[] with T's element size, as LINQ's ToArray makes them: scanned (an element may
     * hold a reference), collected with what holds it (TF.State's models make them every tick) */
    bool RawArrElem(ITypeSymbol et) => et is INamedTypeSymbol { IsTupleType: true } || (IsPodS(et) && OriginOf(et) == Origin.Tfex) ||
        // bool? / int? ...: gen::Opt<T> elements (a has flag and the value), zeroed = null as C#'s new T?[n]
        (IsNullable(et) && ((INamedTypeSymbol)et).TypeArguments[0].SpecialType is >= SpecialType.System_Boolean and <= SpecialType.System_Double);
    string NewArrOf(IArrayTypeSymbol at, string n) =>
        $"gen::NewArr<{Cpp(at.ElementType)}>({B.Need("t", RawArrElem(at.ElementType) ? "System.Object[]" : SwitchName(at))}, {n})";

    string ArrayNew(IArrayCreationOperation ac)
    {
        if (ac.Type is IArrayTypeSymbol { Rank: 2 } g2 && IsGridElem(g2.ElementType) && ac.DimensionSizes.Length == 2 && ac.Initializer == null)
        {
            R.Add("mechanical: a rectangular array of numbers -> gen::Grid (a local owning its cells)", Where(ac));
            return $"{Cpp(g2)}({Expr(ac.DimensionSizes[0])}, {Expr(ac.DimensionSizes[1])})";
        }
        if (ac.Type is not IArrayTypeSymbol at || at.Rank != 1 || ac.DimensionSizes.Length != 1) return Unsupported(ac, "ArrayCreation");
        var et = at.ElementType;
        if (gen.Models.IsModel(et) || (IsNullable(et) && !RawArrElem(et)) || et is ITypeParameterSymbol || (et.IsValueType && OriginOf(et) == Origin.Tfex && et.TypeKind != TypeKind.Enum && !RawArrElem(et)))
            return Unsupported(ac, "ArrayCreation");
        var n = Expr(ac.DimensionSizes[0]);
        if (ac.Initializer == null || ac.Initializer.ElementValues.Length == 0) return NewArrOf(at, n);
        return ArrayFromItems(at, ac.Initializer, ac);
    }
    string ArrayFromItems(IArrayTypeSymbol at, IArrayInitializerOperation init, IOperation where)
    {
        var et = at.ElementType;
        if (gen.Models.IsModel(et) || (IsNullable(et) && !RawArrElem(et)) || et is ITypeParameterSymbol || (et.IsValueType && OriginOf(et) == Origin.Tfex && et.TypeKind != TypeKind.Enum && !RawArrElem(et)))
            return Unsupported(where, "ArrayCreation");
        var items = init.ElementValues.Select(Expr).ToList();
        var sb = new StringBuilder($"([&]() {{ auto a_ = {NewArrOf(at, items.Count.ToString())};");
        for (int i = 0; i < items.Count; i++) sb.Append($" if (a_.data) a_.data[{i}] = {items[i]};");
        return sb.Append(" return a_; }())").ToString();
    }

    string GameArray(ITypeSymbol t, string managed)
    {
        if (t is not IArrayTypeSymbol at) return null;
        // a game T[,] (LevelTiles.bitData): a view of its cells in place (gen::Grid<T>::Of, the runtime's 2-D array layout)
        if (at.Rank == 2 && UnitEmitter.IsGridElem(at.ElementType)) return $"gen::Grid<{Cpp(at.ElementType)}>::Of({managed})";
        var len = B.Need("m", "System.Array", "get_Length", "", 0, 1);
        return $"gen::GameArr<{Cpp(at.ElementType)}>({managed}, {len})";
    }

    string TimeProperty(IPropertyReferenceOperation p)
    {
        var n = p.Property.ContainingType.Name + "." + p.Property.Name;
        var i = p.Instance != null ? Expr(p.Instance) : "";
        switch (n)
        {
            case "DateTime.UtcNow": case "DateTime.Now": return "gen::UtcNowTicks()";
            case "TimeSpan.TotalSeconds": return $"gen::TotalSeconds({i})";
            case "TimeSpan.TotalMilliseconds": return $"gen::TotalMilliseconds({i})";
            case "TimeSpan.Ticks": case "DateTime.Ticks": return i;
            case "TimeSpan.Seconds": return $"(int32_t)(({i}) / gen::TicksPerSecond % 60)";
            case "TimeSpan.Milliseconds": return $"(int32_t)(({i}) / gen::TicksPerMillisecond % 1000)";
            case "TimeSpan.Minutes": return $"(int32_t)(({i}) / (gen::TicksPerSecond * 60) % 60)";
            case "TimeSpan.Hours": return $"(int32_t)(({i}) / (gen::TicksPerSecond * 3600) % 24)";
            case "TimeSpan.TotalMinutes": return $"((double)({i}) / (double)(gen::TicksPerSecond * 60))";
            case "TimeSpan.TotalHours": return $"((double)({i}) / (double)(gen::TicksPerSecond * 3600))";
        }
        return Unsupported(p, "time member " + n);
    }
    string TimeCall(IInvocationOperation inv)
    {
        var n = inv.TargetMethod.ContainingType.Name + "." + inv.TargetMethod.Name;
        var a = inv.Arguments.OrderBy(x => x.Parameter.Ordinal).Select(x => Expr(x.Value)).ToList();
        var i = inv.Instance != null ? Expr(inv.Instance) : "";
        switch (n)
        {
            case "DateTime.AddSeconds": return $"(({i}) + gen::TicksFromSeconds({a[0]}))";
            case "DateTime.AddMilliseconds": return $"(({i}) + gen::TicksFromMilliseconds({a[0]}))";
            case "DateTime.Add": case "TimeSpan.Add": return $"(({i}) + ({a[0]}))";
            case "DateTime.Subtract": case "TimeSpan.Subtract": return $"(({i}) - ({a[0]}))";
            case "DateTime.ToUniversalTime": return i;
            case "TimeSpan.FromSeconds": return $"gen::TicksFromSeconds({a[0]})";
            case "TimeSpan.FromMinutes": return $"gen::TicksFromSeconds(({a[0]}) * 60.0)";   /* (TF.State's AddChestsState: the chests' cache entry) */
            case "TimeSpan.FromMilliseconds": return $"gen::TicksFromMilliseconds({a[0]})";
            case "TimeSpan.FromTicks": return a[0];
            case "DateTime.TryParseExact" when inv.Arguments.Length == 5 && inv.Arguments.All(x => x.Parameter.Type.SpecialType == SpecialType.System_String ? true : true):
            {   // DateTime.TryParseExact(s, "yyyy-MM-dd_HH-mm-ss", invariant culture, DateTimeStyles.None, out d) (MatchReport): gen::DateTryParseExact
                var ta = inv.Arguments.OrderBy(x => x.Parameter.Ordinal).ToList();
                R.Add("mechanical: DateTime.TryParseExact(s, fixed format, invariant, None, out d) -> gen::DateTryParseExact (ticks)", Where(inv));
                return $"gen::DateTryParseExact({a[0]}, {a[1]}, &{LValue(ta[4].Value is IDeclarationExpressionOperation tde ? tde.Expression : ta[4].Value)})";
            }
        }
        return Unsupported(inv, "time member " + n);
    }

    bool Bound(string key)
    {
        var k = key.StartsWith("bind::") ? key[6..] : key;
        return B.Binding.ContainsKey(k) && B.Binding[k]?["error"] == null;
    }

    /* LValue without reporting: null when the target isn't plain storage */
    string LValueOrNull(IOperation t) => t is IPropertyReferenceOperation mp && !(
        (!mp.Property.IsStatic && gen.Models.IsModel(mp.Property.ContainingType) && gen.Models.MemberOf(mp.Property.ContainingType, mp.Property.Name) is { } mm &&
         !mm.CppType.StartsWith("Str<") && mm.CppType != "MStr" && !mm.CppType.StartsWith("List<") && !mm.CppType.StartsWith("Opt<") && !(mm.CppType.StartsWith("StrMap<") || mm.CppType.StartsWith("KeyMap<"))) ||
        (mp.Instance != null && !mp.Property.IsStatic && IsPodS(mp.Property.ContainingType) && IsAutoProperty(mp.Property))) ? null : "storage";

    string LValue(IOperation t) => t switch
    {
        IDeclarationExpressionOperation { Expression: ILocalReferenceOperation dl } => Id(dl.Local.Name),   // out var x (hoisted)
        IDiscardOperation dd => $"gen::Discard<{Cpp(dd.Type)}>()",   // out _: written to scratch storage nobody reads
        IFieldReferenceOperation f => Field(f),
        ILocalReferenceOperation l => Id(l.Local.Name),
        IParameterReferenceOperation p => Id(p.Parameter.Name),
        IArrayElementReferenceOperation => Expr(t),
        // a lobby model's scalar member (MatchLength++) or a TF.EX struct's auto-property: the struct member itself
        IPropertyReferenceOperation mp when !mp.Property.IsStatic && gen.Models.IsModel(mp.Property.ContainingType) &&
            gen.Models.MemberOf(mp.Property.ContainingType, mp.Property.Name) is { } mm && !mm.CppType.StartsWith("Str<") && mm.CppType != "MStr" && !mm.CppType.StartsWith("List<") && !mm.CppType.StartsWith("Opt<") && !(mm.CppType.StartsWith("StrMap<") || mm.CppType.StartsWith("KeyMap<"))
            => $"({Recv(mp.Instance)})->{mp.Property.Name}",
        IPropertyReferenceOperation sp when sp.Instance != null && !sp.Property.IsStatic && IsPodS(sp.Property.ContainingType) && IsAutoProperty(sp.Property)
            => $"{LValue(sp.Instance)}.{sp.Property.Name}",
        _ => Unsupported(t, "assignment target " + t.Kind),
    };

    /* the receiver of a member access: `this` (self), an object initializer's new object (implicitRecv), or an expression */
    string implicitRecv;
    string Recv(IOperation op) => op == null ? "self" : op is IInstanceReferenceOperation ir
        ? (ir.ReferenceKind == InstanceReferenceKind.ImplicitReceiver && implicitRecv != null ? implicitRecv : "self") : Expr(op);

    /* `new T(...) { A = x, B = y }` on a game class or a class of ours: made, then each member set on it */
    string WithInitializer(IObjectCreationOperation oc, string made)
    {
        if (oc.Initializer == null || oc.Initializer.Initializers.Length == 0) return made;
        var v = $"o_{tmp++}";
        var sb = new StringBuilder($"([&]() {{ void* {v} = {made}; ");
        var saved = implicitRecv; implicitRecv = v;
        try
        {
            foreach (var init in oc.Initializer.Initializers)
                sb.Append(init is ISimpleAssignmentOperation a ? Assign(a.Target, Expr(a.Value)) : Unsupported(init, "initializer " + init.Kind)).Append("; ");
        }
        finally { implicitRecv = saved; }
        return sb.Append($"return {v}; }}())").ToString();
    }

    string Assign(IOperation target, string value)
    {
        if (target is IFieldReferenceOperation { Field: var raf } && ResolvedReflectionField(raf))
        {
            R.Add("mechanical: the assignment of a reflection lookup resolved at translate time -> nothing (its uses are direct code)", $"{raf.ContainingType.Name}.{raf.Name}");
            return "((void)0)";
        }
        if (target is ILocalReferenceOperation rl && refSlots.TryGetValue(rl.Local, out var rs))   // (as a value, `return x = entry`: the value)
            return target.Parent?.Parent is IExpressionStatementOperation ? rs.set(value) : $"([&]() {{ auto rv_ = {value}; {rs.set("rv_")}; return rv_; }}())";
        // a value container field given a new one: the field keeps its own storage (allocated once), the new contents copied in
        if (target is IFieldReferenceOperation { Field: var kvf } && IsValColl(kvf.Type) && OriginOf(kvf.ContainingType) == Origin.Tfex && gen.ServiceRule(kvf) == null &&
            (target.Parent is ISimpleAssignmentOperation ||   // a new one, or a local's (ring storage: _skinKnownPeers = members)
             target.Parent is IArgumentOperation { Parent: IInvocationOperation { TargetMethod: { Name: "Exchange", ContainingType.Name: "Interlocked" } } }))   // (Interlocked.Exchange(ref _guestInputs, inputs), v0.28.0)
        {
            R.Add("mechanical: a new container assigned to a field -> copied into the field's own storage (gen::KeepVal)", Where(target));
            return $"gen::KeepVal({Field((IFieldReferenceOperation)target)}, {value})";
        }
        if (target is IInvalidOperation it && InvalidRule(it, set: true) is string inv) return inv.Replace("$value", value);
        if (SideMember(target, value) is string sideW) return sideW;
        if (AbsentMember(target) != null) return $"(void)({value})";
        if (target is IFieldReferenceOperation { Field: var mff } mfr && ModelFieldAssign(mff, mfr.Instance == null ? "self" : Recv(mfr.Instance), value) is string mfa)
        {
            // a field with a [service] rule that its own class writes directly (the model-field path wins): split state unless the rule
            // reads this same field (Integration A, 2026-10-18: MatchmakingService.pendingRollcallLobby)
            if (gen.ServiceRule(mff) is TomlTable wr && wr.ContainsKey("set"))
            {
                R.Add("warning: a [service]-ruled field written directly by its class (split state unless the rule reads the same field)", $"{mff.ContainingType.Name}.{mff.Name}");
                Console.Error.WriteLine($"translate: warning: {mff.ContainingType.Name}.{mff.Name} has a [service] rule but its class writes the field directly");
            }
            return mfa;
        }
        // a game object's array field (variant.playerValues = null / = arr): its managed object
        // a [service] rule's `set` on a TF.EX field (a hosted unit's state: "<Type>.<field>" = { cpp = "<get>", set = "<C++ with $value>" })
        // (also a game field FortRise adds, with a `set` rule: TowerFall.ArcherData.ExtraHairData as TF.EX's SkinOverlayService writes it)
        if (target is IFieldReferenceOperation { Field: var sfld } sfo && gen.ServiceRule(sfld) is TomlTable sfr &&
            (OriginOf(sfld.ContainingType) == Origin.Tfex || sfr.ContainsKey("set")))
        {
            if (sfr.TryGetValue("set", out var fset) && fset is string fst)
            {
                R.Add("service map: field set", sfld.ContainingType.ToDisplayString() + "." + sfld.Name);
                includes.Add("tfexgen_services.hpp");
                if (sfr.TryGetValue("include", out var finc)) includes.Add((string)finc);
                var fself = sfo.Instance == null ? "self" : Recv(sfo.Instance);   // (an object initializer's implicit receiver: the new object)
                return fst.Replace("$value", value).Replace("$self", fself);
            }
            return Unsupported(target, "[service] field without a set rule: " + sfld.Name);
        }
        if (target is IFieldReferenceOperation { Field: { IsStatic: false, Type: IArrayTypeSymbol } af } fa && InBinary(af.ContainingType))
            return $"gen::Fld<void*>({Expr(fa.Instance)}, {B.Need("f", SwitchName(af.ContainingType), af.Name)}) = ({value}).obj";
        // the same for a FortRise MonoMod patch class's redeclaration of the game's array field (patch_PlayerHair.offsets / images in its
        // Links setter): the game's field gets the new managed array
        if (target is IFieldReferenceOperation { Field: { IsStatic: false, Type: IArrayTypeSymbol } paf } pfa && paf.ContainingType.BaseType is INamedTypeSymbol pgb &&
            OriginOf(pgb) == Origin.Game && pgb.GetMembers(paf.Name).OfType<IFieldSymbol>().FirstOrDefault() is IFieldSymbol pgf && !pgf.IsStatic)
            return $"gen::Fld<void*>({(pfa.Instance == null || pfa.Instance is IInstanceReferenceOperation ? "self" : Expr(pfa.Instance))}, {B.Need("f", SwitchName(pgb), pgf.Name)}) = ({value}).obj";
        // a game static array field given a new array (InputService: TFGame.PlayerInputs = new PlayerInput[n]): the static's managed object
        // (read, such a field is an Arr wrapper: assigning that would only change the copy)
        if (target is IFieldReferenceOperation { Field: { IsStatic: true, Type: IArrayTypeSymbol } sarr } && OriginOf(sarr.ContainingType) == Origin.Game && InBinary(sarr.ContainingType))
            return $"({CctorThen(SwitchName(sarr.ContainingType), $"gen::Static<void*>({B.Need("f", SwitchName(sarr.ContainingType), sarr.Name)})")}) = ({value}).obj";
        if (target is IPropertyReferenceOperation { Property.IsIndexer: true, Instance: not null } ix && IsValColl(Strip(ix.Instance).Type))
            return $"({Expr(Strip(ix.Instance))})->Set({Expr(ix.Arguments[0].Value)}, {value})";
        if (target is IPropertyReferenceOperation p0 && gen.Models.IsModel(p0.Property.ContainingType) && !p0.Property.IsStatic) return ModelSet(p0, value);
        if (target is IPropertyReferenceOperation p)
        {
            var ps = p.Property;
            var o = OriginOf(ps.ContainingType);
            // a [service] rule's `set` (overrides.toml: "<Type>.<Property>" = { cpp = "<get>", set = "<C++ with $value>" }), on any property
            if (gen.ServiceRule(ps) is TomlTable svr && svr.TryGetValue("set", out var setText) && setText is string st)
            {
                R.Add("service map: property set", ps.ContainingType.ToDisplayString() + "." + ps.Name);
                includes.Add("tfexgen_services.hpp");
                if (svr.TryGetValue("include", out var inc)) foreach (var h in ((string)inc).Split(',')) includes.Add(h.Trim());
                // (an indexer's arguments as $0, $1 ...: Monocle.Atlas[key] = value)
                var sa = p.Arguments.OrderBy(a => a.Parameter.Ordinal).Select(a => Expr(a.Value)).ToList();
                for (int i = sa.Count - 1; i >= 0; i--) st = st.Replace("$" + i, sa[i]);
                return st.Replace("$value", value).Replace("$self", p.Instance != null ? Recv(p.Instance) : "nullptr");
            }
            if (p.Instance != null && !ps.IsStatic && IsPodS(ps.ContainingType) && IsAutoProperty(ps)) return $"{LValue(p.Instance)}.{ps.Name} = {value}";
            if ((InBinary(ps.ContainingType) || (o == Origin.Bcl && ps.ContainingType.IsReferenceType)) && ps.SetMethod != null)   // a game's / corlib's (List<T>[i] = x)
            {
                var args = p.Arguments.OrderBy(a => a.Parameter.Ordinal).Select(a => Expr(a.Value)).ToList();
                args.Add(value);
                return Call(ps.SetMethod, ps.IsStatic ? null : Expr(p.Instance), args, p);
            }
            if (OriginOf(ps.ContainingType) == Origin.Tfex && !ps.IsStatic && ps.SetMethod != null && IsClassType(ps.ContainingType))
            {
                var recvP = Recv(p.Instance);
                if (SymbolEqualityComparer.Default.Equals(ps.ContainingType, type)) return $"{Id(ps.SetMethod.Name)}({recvP}, {value})";
                var su = OtherUnit(ps.ContainingType) is TomlTable su0 && !su0.ContainsKey("auto") && !su0.ContainsKey("members") ? su0 : AutoU(ps.ContainingType, ps);
                includes.Add($"tfexgen_{su["name"]}.hpp");
                return $"tfex::gen::{su["name"]}::{Id(ps.SetMethod.Name)}({recvP}, {value})";
            }
            if (OriginOf(ps.ContainingType) == Origin.Tfex && ps.IsStatic && ps.SetMethod != null)
            {
                if (Service(ps, new List<string> { value }, target) is string ss) return ss;
                if (SymbolEqualityComparer.Default.Equals(ps.ContainingType, type)) return $"{Id(ps.SetMethod.Name)}({value})";
                if ((OtherUnit(ps.ContainingType) is TomlTable sp0 && !sp0.ContainsKey("auto") && !sp0.ContainsKey("members") ? sp0 : AutoU(ps.ContainingType, ps)) is TomlTable pu) { includes.Add($"tfexgen_{pu["name"]}.hpp"); return $"tfex::gen::{pu["name"]}::{Id(ps.SetMethod.Name)}({value})"; }
            }
            // a mod's interop interface's property ([[interop]]): the provider class's generated setter
            if (!ps.IsStatic && ps.ContainingType.TypeKind == TypeKind.Interface && gen.InteropImpl(ps.ContainingType, ps) is IPropertySymbol xs && xs.SetMethod != null &&
                (OtherUnit(xs.ContainingType) is TomlTable xsu0 && !xsu0.ContainsKey("auto") && !xsu0.ContainsKey("members") ? xsu0 : AutoU(xs.ContainingType, xs)) is TomlTable xsu)
            {
                R.Add("mechanical: a property set through a mod's interop interface -> the provider mod's class ([[interop]])", $"{ps.ContainingType.Name}.{ps.Name} -> {xs.ContainingType.Name}");
                includes.Add($"tfexgen_{xsu["name"]}.hpp");
                return $"tfex::gen::{xsu["name"]}::{Id(xs.SetMethod.Name)}({Recv(p.Instance)}, {value})";
            }
            // a get-only auto-property set in its class's constructor: C# writes the backing field (ours: <Name>_)
            if (ps.SetMethod == null && SymbolEqualityComparer.Default.Equals(ps.ContainingType, type) && isClass &&
                ps.ContainingType.GetMembers().OfType<IFieldSymbol>().Any(f => SymbolEqualityComparer.Default.Equals(f.AssociatedSymbol, ps)))
            {
                if (ps.IsStatic) return $"g_s->{Id(ps.Name)}_ = {value}";
                return $"S({(Recv(p.Instance))})->{Id(ps.Name)}_ = {value}";
            }
            return Unsupported(target, "property set " + ps.ToDisplayString());
        }
        return $"{LValue(target)} = {value}";
    }

    string CompoundAssign(ICompoundAssignmentOperation ca)
    {
        var o = BinOp(ca.OperatorKind);
        if (ca.Target is IPropertyReferenceOperation) return Assign(ca.Target, $"{Expr(ca.Target)} {o} ({Expr(ca.Value)})");
        if (ca.Target.Type?.SpecialType == SpecialType.System_String && ca.OperatorKind == BinaryOperatorKind.Add)
            return $"{LValue(ca.Target)} = gen::Concat({Expr(ca.Target)}, {StringOf(ca.Value)})";
        if (ca.OperatorKind == BinaryOperatorKind.Remainder && ca.Target.Type?.SpecialType is SpecialType.System_Single or SpecialType.System_Double)
            return $"{LValue(ca.Target)} = std::fmod({Expr(ca.Target)}, {Expr(ca.Value)})";   // x %= y on a float / double: C#'s IEEE remainder sign rule = fmod
        return $"{LValue(ca.Target)} {o}= {Operand(ca.Value)}";
    }

    /* ---------------------------------------------------------------- operators ---------------------------------------------------------------- */
    static string BinOp(BinaryOperatorKind k) => k switch
    {
        BinaryOperatorKind.Add => "+", BinaryOperatorKind.Subtract => "-", BinaryOperatorKind.Multiply => "*", BinaryOperatorKind.Divide => "/",
        BinaryOperatorKind.Remainder => "%", BinaryOperatorKind.LeftShift => "<<", BinaryOperatorKind.RightShift => ">>",
        BinaryOperatorKind.And => "&", BinaryOperatorKind.Or => "|", BinaryOperatorKind.ExclusiveOr => "^",
        BinaryOperatorKind.ConditionalAnd => "&&", BinaryOperatorKind.ConditionalOr => "||",
        BinaryOperatorKind.Equals => "==", BinaryOperatorKind.NotEquals => "!=", BinaryOperatorKind.LessThan => "<",
        BinaryOperatorKind.LessThanOrEqual => "<=", BinaryOperatorKind.GreaterThan => ">", BinaryOperatorKind.GreaterThanOrEqual => ">=",
        _ => "/*?*/",
    };

    string Binary(IBinaryOperation b)
    {
        // x == null / x != null on a reflection value resolved at translate time (a FieldInfo / MethodInfo / setter delegate found by name):
        // it exists, so a constant (Soccer's FlatPlay: `if (setGround == null || moveAxis == null || normal == null)`)
        if (b.OperatorKind is BinaryOperatorKind.Equals or BinaryOperatorKind.NotEquals)
        {
            var l = Strip(b.LeftOperand); var r = Strip(b.RightOperand);
            var other = l.ConstantValue is { HasValue: true, Value: null } ? r : r.ConstantValue is { HasValue: true, Value: null } ? l : null;
            bool resolved = other switch
            {
                IFieldReferenceOperation rf0 => ResolvedReflectionField(rf0.Field),
                ILocalReferenceOperation or IInvocationOperation => other.Type?.ToDisplayString() is "System.Reflection.MethodInfo" or "System.Reflection.FieldInfo" && (MethodFrom(other) != null || ReflectedField(other) != null),
                _ => false,
            };
            if (resolved)
            {
                R.Add("mechanical: a resolved reflection value compared with null -> a constant (it exists)", Where(b));
                return b.OperatorKind == BinaryOperatorKind.Equals ? "false" : "true";
            }
        }
        // x == null / x != null where x is a type parameter's value and the generic copy made it a value type (Sprite<int>.CurrentAnimID): never null
        if (subst.Count > 0 && b.OperatorKind is BinaryOperatorKind.Equals or BinaryOperatorKind.NotEquals)
        {
            var lo = Strip(b.LeftOperand); var ro = Strip(b.RightOperand);
            bool lnl = lo is ILiteralOperation { ConstantValue: { HasValue: true, Value: null } } || lo is IDefaultValueOperation { Type: null };
            bool rnl = ro is ILiteralOperation { ConstantValue: { HasValue: true, Value: null } } || ro is IDefaultValueOperation { Type: null };
            var other = lnl ? ro : rnl ? lo : null;
            if (other?.Type is ITypeParameterSymbol && Sub(other.Type) is { IsValueType: true } vt && vt.OriginalDefinition.SpecialType != SpecialType.System_Nullable_T)
                return b.OperatorKind == BinaryOperatorKind.NotEquals ? "true" : "false";
        }
        // typeof(T) == typeof(int) in a generic copy (or between any two types): known now
        if (b.OperatorKind is BinaryOperatorKind.Equals or BinaryOperatorKind.NotEquals && Strip(b.LeftOperand) is ITypeOfOperation tl && Strip(b.RightOperand) is ITypeOfOperation tr2)
        {
            bool same = SymbolEqualityComparer.Default.Equals(Sub(tl.TypeOperand), Sub(tr2.TypeOperand));
            R.Add("mechanical: typeof(A) == typeof(B) -> a constant (generic copies: substituted)", Where(b));
            return (same == (b.OperatorKind == BinaryOperatorKind.Equals)) ? "true" : "false";
        }
        var lt = b.LeftOperand.Type; var rt = b.RightOperand.Type;
        bool str = lt?.SpecialType == SpecialType.System_String || rt?.SpecialType == SpecialType.System_String;
        if (str && b.OperatorKind == BinaryOperatorKind.Add) return $"gen::Concat({StringOf(b.LeftOperand)}, {StringOf(b.RightOperand)})";
        if (str && b.OperatorKind is BinaryOperatorKind.Equals or BinaryOperatorKind.NotEquals && lt?.SpecialType == SpecialType.System_String && rt?.SpecialType == SpecialType.System_String)
            return (b.OperatorKind == BinaryOperatorKind.NotEquals ? "!" : "") + $"gen::StrEq({Expr(b.LeftOperand)}, {Expr(b.RightOperand)})";
        // (an array compared as object: `ArcherData.Teams == null` binds as object == object, the array under a conversion; Teams+'s DataHooks)
        var sla = Strip(b.LeftOperand).Type as IArrayTypeSymbol; var sra = Strip(b.RightOperand).Type as IArrayTypeSymbol;
        if (b.OperatorKind is BinaryOperatorKind.Equals or BinaryOperatorKind.NotEquals && (sla != null || sra != null))
        {
            var arr = sla != null ? Strip(b.LeftOperand) : Strip(b.RightOperand);
            var other = sla != null ? b.RightOperand : b.LeftOperand;
            if (other.ConstantValue is { HasValue: true, Value: null } || other is IConversionOperation { Operand.ConstantValue: { HasValue: true, Value: null } })
                return $"(({Expr(arr)}).data {(b.OperatorKind == BinaryOperatorKind.Equals ? "==" : "!=")} nullptr)";
        }
        bool ln = lt != null && IsNullable(lt), rn = rt != null && IsNullable(rt);
        if ((ln || rn) && !str)
        {
            bool lnull = b.LeftOperand.ConstantValue is { HasValue: true, Value: null } || (b.LeftOperand is IConversionOperation lc && lc.Operand.ConstantValue is { HasValue: true, Value: null });
            bool rnull = b.RightOperand.ConstantValue is { HasValue: true, Value: null } || (b.RightOperand is IConversionOperation rc && rc.Operand.ConstantValue is { HasValue: true, Value: null });
            if (b.OperatorKind is BinaryOperatorKind.Equals or BinaryOperatorKind.NotEquals && (lnull || rnull))
                return (b.OperatorKind == BinaryOperatorKind.Equals ? "!" : "") + $"({Expr(lnull ? b.RightOperand : b.LeftOperand)}).has";
            var a = $"l_{tmp++}"; var c = $"r_{tmp++}";
            string Val(string v, bool n) => n ? v + ".value" : v;
            string Has(string v, bool n) => n ? v + ".has" : "true";
            var op = BinOp(b.OperatorKind);
            if (b.OperatorKind is BinaryOperatorKind.Equals)
                return $"([&]() {{ auto {a} = {Expr(b.LeftOperand)}; auto {c} = {Expr(b.RightOperand)}; return {Has(a, ln)} == {Has(c, rn)} && (!{Has(a, ln)} || {Val(a, ln)} == {Val(c, rn)}); }}())";
            if (b.OperatorKind is BinaryOperatorKind.NotEquals)
                return $"([&]() {{ auto {a} = {Expr(b.LeftOperand)}; auto {c} = {Expr(b.RightOperand)}; return {Has(a, ln)} != {Has(c, rn)} || ({Has(a, ln)} && {Val(a, ln)} != {Val(c, rn)}); }}())";
            if (b.OperatorKind is BinaryOperatorKind.LessThan or BinaryOperatorKind.LessThanOrEqual or BinaryOperatorKind.GreaterThan or BinaryOperatorKind.GreaterThanOrEqual)
                return $"([&]() {{ auto {a} = {Expr(b.LeftOperand)}; auto {c} = {Expr(b.RightOperand)}; return {Has(a, ln)} && {Has(c, rn)} && {Val(a, ln)} {op} {Val(c, rn)}; }}())";
            var rtype = Cpp(b.Type);
            return $"([&]() -> {rtype} {{ auto {a} = {Expr(b.LeftOperand)}; auto {c} = {Expr(b.RightOperand)}; if (!({Has(a, ln)} && {Has(c, rn)})) return {rtype}{{}}; return {rtype}({Val(a, ln)} {op} {Val(c, rn)}); }}())";
        }
        // seat == GetLocalPlayerInputIndex(): several local players per device are planned (docs/research-couch-online.md section 5), so a
        // comparison with the local seat is a question to the services (svc::IsLocalSeat), not == one scalar
        if (b.OperatorKind is BinaryOperatorKind.Equals or BinaryOperatorKind.NotEquals && (IsModelMap(b.LeftOperand) || IsModelMap(b.RightOperand)) &&
            (Strip(b.LeftOperand).ConstantValue is { HasValue: true, Value: null } || Strip(b.RightOperand).ConstantValue is { HasValue: true, Value: null }))
        {   // mod.Data != null: the map's null flag (JSON null)
            var mm = IsModelMap(b.LeftOperand) ? b.LeftOperand : b.RightOperand;
            return $"{(b.OperatorKind == BinaryOperatorKind.Equals ? "" : "!")}({Expr(Strip(mm))})->null";
        }
        if (b.OperatorKind is BinaryOperatorKind.Equals or BinaryOperatorKind.NotEquals && (IsLocalSeatCall(b.LeftOperand) || IsLocalSeatCall(b.RightOperand)))
        {
            var call = IsLocalSeatCall(b.LeftOperand) ? b.LeftOperand : b.RightOperand;
            var other = ReferenceEquals(call, b.LeftOperand) ? b.RightOperand : b.LeftOperand;
            // the input index is a GGRS handle in a match (IsLocalInputIndex switches); a lobby seat is IsLocalSeat
            var fn = LocalSeatKind(call) == "input" ? "IsLocalInputIndex" : "IsLocalSeat";
            R.Add($"rule: x == the local seat/index -> svc::{fn}(x) (several local players per device)", Where(b));
            includes.Add("tfexgen_services.hpp");
            return (b.OperatorKind == BinaryOperatorKind.NotEquals ? "!" : "") + $"tfex::gen::svc::{fn}({Expr(other)})";
        }
        if (b.OperatorMethod != null && b.OperatorMethod.ContainingType.ToDisplayString() is "System.TimeSpan" &&
            b.OperatorKind == BinaryOperatorKind.Divide && b.Type?.SpecialType == SpecialType.System_Double)   // TimeSpan / TimeSpan: a ratio
            return $"((double)({Expr(b.LeftOperand)}) / (double)({Expr(b.RightOperand)}))";
        if (b.OperatorMethod != null && b.OperatorMethod.ContainingType.ToDisplayString() is "System.DateTime" or "System.TimeSpan" or "System.Version")
            return $"{Operand(b.LeftOperand)} {BinOp(b.OperatorKind)} {Operand(b.RightOperand)}";   // ticks / a packed Version: - + == < ...
        // System.Type == / != (FortRise's SpriteEntry<T>: `typeof(T) == typeof(int)`): the runtime type objects compared (one per type)
        if (b.OperatorMethod?.ContainingType.ToDisplayString() == "System.Type" && b.OperatorKind is BinaryOperatorKind.Equals or BinaryOperatorKind.NotEquals)
        {
            R.Add("mechanical: Type == Type -> the runtime type objects compared", Where(b));
            return $"({Expr(b.LeftOperand)} {(b.OperatorKind == BinaryOperatorKind.Equals ? "==" : "!=")} {Expr(b.RightOperand)})";
        }
        // an operator with a [service] rule (FortRise's ModuleMetadata == over the Switch's registry): the rule, $0 / $1 the operands
        if (b.OperatorMethod != null && gen.ServiceRule(b.OperatorMethod) != null &&
            Service(b.OperatorMethod, new List<string> { Expr(b.LeftOperand), Expr(b.RightOperand) }, b) is string sop)
            return sop;
        if (b.OperatorMethod != null && OriginOf(b.OperatorMethod.ContainingType) == Origin.Tfex)   // a TF.EX operator: its unit's op_X(a, b)
        {
            var om = b.OperatorMethod;
            var args2 = $"{Expr(b.LeftOperand)}, {Expr(b.RightOperand)}";
            methodCallees.Add(om.OriginalDefinition);
            if (SymbolEqualityComparer.Default.Equals(om.ContainingType, type)) return $"{Id(om.Name)}({args2})";
            var ou = OtherUnit(om.ContainingType) is TomlTable ou1 && !ou1.ContainsKey("auto") && !ou1.ContainsKey("members") ? ou1 : AutoU(om.ContainingType, om);
            if (ou == null) return Unsupported(b, "user-defined operator " + om.ToDisplayString());
            includes.Add($"tfexgen_{ou["name"]}.hpp");
            return $"tfex::gen::{ou["name"]}::{Id(om.Name)}({args2})";
        }
        // a.GetType() == / != b.GetType() (TF.EX's InputService.IsSameDevice): the objects' runtime types (one per type: compared by pointer)
        if (b.OperatorMethod?.ContainingType.ToDisplayString() == "System.Type" && b.OperatorKind is BinaryOperatorKind.Equals or BinaryOperatorKind.NotEquals &&
            Strip(b.LeftOperand) is IInvocationOperation { TargetMethod.Name: "GetType", Instance: { } gl } && Strip(b.RightOperand) is IInvocationOperation { TargetMethod.Name: "GetType", Instance: { } gr })
        {
            R.Add("mechanical: a.GetType() ==/!= b.GetType() -> their runtime types", Where(b));
            return $"(gen::RuntimeTypeOf({Expr(gl)}) {(b.OperatorKind == BinaryOperatorKind.Equals ? "==" : "!=")} gen::RuntimeTypeOf({Expr(gr)}))";
        }
        if (b.OperatorMethod != null && OriginOf(b.OperatorMethod.ContainingType) != Origin.Fna)
            return Unsupported(b, "user-defined operator " + b.OperatorMethod.ToDisplayString());
        if (b.OperatorKind == BinaryOperatorKind.Remainder && (b.Type?.SpecialType is SpecialType.System_Single or SpecialType.System_Double))
            return $"std::fmod({Expr(b.LeftOperand)}, {Expr(b.RightOperand)})";
        return $"{Operand(b.LeftOperand)} {BinOp(b.OperatorKind)} {Operand(b.RightOperand)}";
    }

    static bool IsLocalSeatCall(IOperation op) => LocalSeatKind(op) != null;
    static string LocalSeatKind(IOperation op)
    {
        while (op is IConversionOperation c) op = c.Operand;
        if (op is not IInvocationOperation li) return null;
        if (li.TargetMethod.Name == "GetLocalPlayerInputIndex" && li.TargetMethod.ContainingType.Name == "IInputService") return "input";
        if (li.TargetMethod.Name == "GetLocalSeat" && li.TargetMethod.ContainingType.Name == "IMatchmakingService") return "seat";
        return null;
    }

    /* C#'s operation tree has no parentheses: an operand that is itself an operator expression gets them */
    string Operand(IOperation o)
    {
        var e = Expr(o);
        var inner = o;
        while (inner is IConversionOperation c && c.IsImplicit) inner = c.Operand;
        return inner is IBinaryOperation or IConditionalOperation or IAssignmentOperation or ICoalesceOperation ? "(" + e + ")" : e;
    }

    /* an enum value's name (C#'s Enum.ToString of a defined value; an undefined one prints as its number). A game enum's values are the
     * Switch's, by name (bind.py); a TF.EX enum's its own. [Flags] combinations aren't named (none in TF.EX's interpolations). */
    string EnumName(INamedTypeSymbol et, string value)
    {
        var names = new List<(string name, long v)>();
        if (InBinary(et))
        {
            var sw = B.EnumValues(SwitchName(et));
            if (sw != null) names.AddRange(sw.Select(kv => (kv.Key, kv.Value)));
        }
        else
            foreach (var f in et.GetMembers().OfType<IFieldSymbol>().Where(f => f.HasConstantValue))
                names.Add((f.Name, Convert.ToInt64(f.ConstantValue)));
        R.Add("mechanical: enum to string -> its names", et.ToDisplayString());
        var sb = new StringBuilder($"([&]() -> void* {{ int64_t e_ = (int64_t)({value});");
        foreach (var g in names.GroupBy(n => n.v)) sb.Append($" if (e_ == {g.Key}LL) return {StringLiteral(g.First().name)};");   // C#: the first-declared name of a value
        return sb.Append(" return gen::FromInt(e_); }())").ToString();
    }

    /* a value used as a string operand: strings as they are, numbers through ToString (invariant) */
    string StringOf(IOperation op)
    {
        while (op is IConversionOperation c && c.IsImplicit && c.Operand.Type != null) op = c.Operand;
        var t = op.Type == null ? null : Sub(op.Type);   // (a generic copy's T: its type argument)
        if (t?.ToDisplayString() == "System.ReadOnlySpan<char>" && op is ILocalReferenceOperation or IParameterReferenceOperation)
            return Expr(op);   // a span variable over a string is its string here (Cpp; patch_Sounds.AddSFX's name / id)
        if (t?.SpecialType == SpecialType.System_String || op.ConstantValue is { HasValue: true, Value: string }) return Expr(op);
        if (t?.SpecialType is SpecialType.System_Int32 or SpecialType.System_Int64 or SpecialType.System_Int16 or SpecialType.System_Byte)
            return $"gen::FromInt({Expr(op)})";
        if (t?.SpecialType is SpecialType.System_Single or SpecialType.System_Double) return $"gen::FromDouble((double)({Expr(op)}))";
        if (t?.SpecialType is SpecialType.System_UInt32 or SpecialType.System_UInt16 or SpecialType.System_SByte)
            return $"gen::FromInt((int64_t)({Expr(op)}))";
        if (t?.SpecialType == SpecialType.System_Char) return $"gen::Repeat({Expr(op)}, 1)";
        if (t?.SpecialType == SpecialType.System_Boolean) return $"([&]() {{ bool b_ = {Expr(op)}; return gen::ManagedUtf8(b_ ? \"True\" : \"False\", b_ ? 4 : 5); }}())";   // C#'s bool.ToString()
        if (t is INamedTypeSymbol { TypeKind: TypeKind.Enum } et) return EnumName(et, Expr(op));
        // a bool? / int? read as a string: C# interpolates null as "", a value as its own ToString
        if (t != null && IsNullable(t) && ((INamedTypeSymbol)t).TypeArguments[0].SpecialType is var nst &&
            nst is SpecialType.System_Boolean or SpecialType.System_Int32 or SpecialType.System_Int64)
        {
            var val = nst == SpecialType.System_Boolean ? "gen::ManagedUtf8(n_.value ? \"True\" : \"False\", n_.value ? 4 : 5)" : "gen::FromInt(n_.value)";
            return $"([&]() {{ auto n_ = {Expr(op)}; return n_.has ? {val} : {StringLiteral("")}; }}())";
        }
        if (t?.ToDisplayString() is "System.Version") { R.Add("mechanical: System.Version -> a packed int64 (gen::VersionString)", Where(op)); return $"gen::VersionString({Expr(op)})"; }
        // s.AsSpan(start[, length]) read as a string (string.Concat(a, s.AsSpan(n)): .NET's span overloads): the substring
        if (Strip(op) is IInvocationOperation { TargetMethod: { Name: "AsSpan", ContainingType.Name: "MemoryExtensions" } asm } asp &&
            asp.Arguments.Length is 2 or 3 && asp.Arguments.All(a0 => a0.Value.Type?.SpecialType is SpecialType.System_String or SpecialType.System_Int32))
        {
            var spa = asp.Arguments.OrderBy(a0 => a0.Parameter.Ordinal).Select(a0 => Expr(a0.Value)).ToList();
            R.Add("mechanical: string.AsSpan(start[, length]) as a string -> its substring (gen::SubstringOf)", Where(op));
            return $"gen::SubstringOf({string.Join(", ", spa)})";
        }
        if (t?.ToDisplayString() == "System.Guid" && Strip(op) is IInvocationOperation { TargetMethod.Name: "NewGuid" })
        { R.Add("mechanical: Guid.NewGuid().ToString() -> gen::NewGuidString (netws::MakeUuid)", Where(op)); return "gen::NewGuidString()"; }
        // an object a no-op / null service rule stands for (IAutoUpdater.GetLatestVersion ...): C# interpolates null as ""
        if (t != null && t.IsReferenceType && (op is IInvocationOperation si ? gen.ServiceRule(si.TargetMethod) : op is IPropertyReferenceOperation sp ? gen.ServiceRule(sp.Property) : null) is Tomlyn.Model.TomlTable sr &&
            (sr.ContainsKey("noop") || (sr.TryGetValue("value", out var sv) && sv is string svs && svs == "nullptr")))
        {
            R.Add("rule: string of a null service value -> \"\"", Where(op));
            return StringLiteral("");
        }
        return Unsupported(op, "string conversion of " + t?.ToDisplayString());
    }

    string Unary(IUnaryOperation u) => u.OperatorKind switch
    {
        UnaryOperatorKind.Not => "!" + Paren(u.Operand),
        UnaryOperatorKind.Minus => "-" + Paren(u.Operand),
        UnaryOperatorKind.Plus => Expr(u.Operand),
        UnaryOperatorKind.BitwiseNegation => "~" + Paren(u.Operand),
        _ => Unsupported(u, "unary " + u.OperatorKind),
    };
    string Paren(IOperation o) { var e = Expr(o); return e.All(ch => char.IsLetterOrDigit(ch) || ch == '_' || ch == '.') ? e : "(" + e + ")"; }

    /* FortRise.Option<T> (a value and a flag, FortRise/Core/Utils/Option.cs): a plain struct {Value, HasValue}; its few members by hand */
    static bool IsFrOption(ITypeSymbol t) => t is INamedTypeSymbol { Name: "Option", Arity: 1 } n && n.ContainingNamespace?.ToDisplayString() == "FortRise";

    string Conversion(IConversionOperation c)
    {
        if (subst.Count > 0)   // in a generic copy: (T)(object)x where T is x's type, and x -> object -> T round trips: the value itself
        {
            var target = Sub(c.Type);
            var inner = c.Operand;
            while (inner is IConversionOperation ic && (ic.Type?.SpecialType == SpecialType.System_Object || ic.Type is ITypeParameterSymbol)) inner = ic.Operand;
            if (!ReferenceEquals(inner, c.Operand) && SymbolEqualityComparer.Default.Equals(Sub(inner.Type), target)) return Expr(inner);
            if (c.Operand.Type is ITypeParameterSymbol && SymbolEqualityComparer.Default.Equals(Sub(c.Operand.Type), target)) return Expr(c.Operand);
        }
        // string -> ReadOnlySpan<char> (FortRise's TextUtils.DrawOutlinedIconText(text), patch_Sounds' keys): the string itself (a span is no
        // type on the Switch: Cpp gives a ReadOnlySpan<char> the managed string)
        if (c.Type?.ToDisplayString() == "System.ReadOnlySpan<char>" && c.Operand.Type?.SpecialType == SpecialType.System_String)
            return Expr(c.Operand);
        // T[] -> ReadOnlySpan<T> (TF.State's BitConverter.ToInt32(SHA256.HashData(bytes))): the array itself, for a [[rewrite]] glue taking T[]
        if (c.Type is INamedTypeSymbol { Name: "ReadOnlySpan" } && c.Operand.Type is IArrayTypeSymbol && c.Parent is IArgumentOperation { Parent: IInvocationOperation si1 } &&
            HasRewrite(si1))
            return Expr(c.Operand);
        // a returned T[] as an interface held as a game object pointer (GameContext.GetSessionSeats: int[] as IReadOnlyList<int>, v0.28.0): the array
        // object (gen::Arr's obj); its readers go through the array's own get_Length / indexer (gen::IList_)
        if (c.IsImplicit && c.Parent is IReturnOperation && c.Operand.Type is IArrayTypeSymbol && c.Type is INamedTypeSymbol { TypeKind: TypeKind.Interface } && Cpp(c.Type) == "void*" &&
            Cpp(c.Operand.Type).StartsWith("gen::Arr<"))
        {
            R.Add("mechanical: an array as an interface (IReadOnlyList<T>) -> the array object", Where(c));
            return $"({Expr(c.Operand)}).obj";
        }
        if (c.OperatorMethod is { Name: "op_Implicit" } && IsFrOption(c.Type))   // T -> Option<T>: Some(value)
        {
            R.Add("mechanical: FortRise Option<T>: a {Value, HasValue} struct", "implicit T -> Option<T>");
            return $"{Cpp(c.Type)}{{{Expr(c.Operand)}, true}}";
        }
        // `dyn.Get("x") as T` / `as V?` on TF.EX's own state (no such member on the game class): the side table, typed by the conversion
        if (c.Operand is IInvocationOperation { TargetMethod: { Name: "Get", TypeArguments.Length: 0 } gm } gi && IsDynamicData(gm.ContainingType) &&
            gi.Arguments.Length == 1 && gi.Arguments[0].Value.ConstantValue.Value is string sn && c.Type != null &&
            (gi.Instance is ILocalReferenceOperation or IParameterReferenceOperation ? WrappedType(gi.Instance) : IsDynamicDataFor(gi.Instance, out var dtg) ? dtg.Type : null) is ITypeSymbol ot &&
            MemberByName(ot, sn) == null)
        {
            var recv = gi.Instance is ILocalReferenceOperation or IParameterReferenceOperation ? Expr(gi.Instance) : IsDynamicDataFor(gi.Instance, out var dtr) ? Expr(dtr) : "nullptr";
            R.Add("rule: DynamicData state TF.EX adds to a game object -> the side table (gen::SideGet/SideSet)", $"{ot.Name}.{sn}");
            if (IsNullable(c.Type)) return $"gen::SideGetOpt<{Cpp(Underlying(c.Type))}>(\"{sn}\", {recv})";
            if (c.Type.IsReferenceType) return $"gen::As(gen::SideGet<void*>(\"{sn}\", {recv}), {TypeOfAny(c.Type)})";
            return $"gen::SideGet<{Cpp(c.Type)}>(\"{sn}\", {recv})";
        }
        var from = c.Operand.Type; var to = c.Type;
        var e = Expr(c.Operand);
        // Monocle's Entity -> bool (the PC's IL: entity != null)
        if (c.OperatorMethod is { ContainingType: var oct } && oct.ToDisplayString() == "Monocle.Entity" && c.Type?.SpecialType == SpecialType.System_Boolean)
            return $"(({e}) != nullptr)";
        if (c.OperatorMethod != null && InBinary(c.OperatorMethod.ContainingType))
        {   // a game type's conversion operator (TowerFall.Variant -> bool): its body in the Switch's binary, a static call
            R.Add("mechanical: a game type's conversion operator -> its body (op_Implicit / op_Explicit)", Where(c));
            return Call(c.OperatorMethod, null, new List<string> { Expr(c.Operand) }, c);
        }
        if (c.OperatorMethod != null) return Unsupported(c, "user-defined conversion " + c.OperatorMethod.ToDisplayString());
        if (to == null || from == null) return e;
        if (to.IsReferenceType && c.IsTryCast && !gen.Models.IsModel(to) && to.SpecialType != SpecialType.System_String && (InBinary(to) || to is ITypeParameterSymbol || SymbolEqualityComparer.Default.Equals(to, type)))
            return AsType(e, to);      // `x as T`: null when x isn't a T
        if (to.IsReferenceType) return e;                 // other reference conversions (casts between game types: C# would throw on a bad one)
        if (IsNullable(to))
        {
            if (c.Operand.ConstantValue is { HasValue: true, Value: null } || c.Operand.Type == null) return $"{Cpp(to)}{{}}";
            if (IsNullable(from)) return Cpp(from) == Cpp(to) ? e : $"{Cpp(to)}::From({e})";
            return $"{Cpp(to)}(({Cpp(Underlying(to))})({e}))";
        }
        if (IsNullable(from)) return $"(({Cpp(to)})(({e}).value))";   // (T)x on a T?: C# throws when null
        e = EnumConvert(e, from, to);
        var ct = Cpp(to);
        return Cpp(from) == ct ? e : $"({ct})({e})";
    }

    /* ---------------------------------------------------------------- strings ---------------------------------------------------------------- */
    string Interpolated(IInterpolatedStringOperation s)
    {
        string acc = null;
        foreach (var part in s.Parts)
        {
            string piece = part switch
            {
                IInterpolatedStringTextOperation t => StringLiteral((string)t.Text.ConstantValue.Value),
                IInterpolationOperation i when i.Alignment == null && i.FormatString == null => StringOf(i.Expression),
                // {n:00}: an integer with at least that many digits (TFCommands' "scenario {index:00}")
                IInterpolationOperation i when i.Alignment == null && i.FormatString?.ConstantValue.Value is string zf && zf.Length > 0 && zf.All(ch => ch == '0') &&
                    Strip(i.Expression).Type?.SpecialType is SpecialType.System_Int32 or SpecialType.System_Int64 or SpecialType.System_Int16 or SpecialType.System_Byte
                    => $"gen::FromIntPad((int64_t)({Expr(i.Expression)}), {zf.Length})",
                // {b:X2} / {n:x}: hexadecimal, at least that many digits (Teams+'s DataHooks.Hex: a colour's bytes)
                IInterpolationOperation i when i.Alignment == null && i.FormatString?.ConstantValue.Value is string xf && xf.Length >= 1 && xf[0] is 'X' or 'x' &&
                    (xf.Length == 1 || xf[1..].All(char.IsDigit)) &&
                    Strip(i.Expression).Type?.SpecialType is SpecialType.System_Int32 or SpecialType.System_Int64 or SpecialType.System_Int16 or SpecialType.System_Byte or SpecialType.System_UInt32 or SpecialType.System_UInt16
                    => $"gen::FromIntHex((uint64_t)({Expr(i.Expression)}), {(xf.Length > 1 ? int.Parse(xf[1..]) : 1)}, {(xf[0] == 'X' ? "true" : "false")})",
                _ => Unsupported(part, "interpolation with alignment/format"),
            };
            acc = acc == null ? piece : $"gen::Concat({acc}, {piece})";
        }
        return acc ?? StringLiteral("");
    }

    /* an interpolated-string handler's content (AppendLiteral / AppendFormatted calls, or a plain interpolation): the string */
    string HandlerString(IOperation content)
    {
        if (content is IInterpolatedStringOperation iso && iso.Parts.All(p => p is not IInterpolatedStringAppendOperation)) return Interpolated(iso);
        var parts = content is IInterpolatedStringOperation io ? io.Parts : ImmutableArray<IInterpolatedStringContentOperation>.Empty;
        string acc = null;
        foreach (var part in parts)
        {
            string piece = part is IInterpolatedStringAppendOperation { AppendCall: IInvocationOperation ac } && ac.Arguments.Length >= 1
                ? (ac.TargetMethod.Name == "AppendLiteral" ? Expr(ac.Arguments[0].Value)
                   : ac.Arguments.Length == 1 ? StringOf(ac.Arguments[0].Value) : Unsupported(part, "interpolation handler with alignment/format"))
                : Unsupported(part, "interpolation handler part");
            acc = acc == null ? piece : $"gen::Concat({acc}, {piece})";
        }
        return acc ?? StringLiteral("");
    }

    /* ---------------------------------------------------------------- creation ---------------------------------------------------------------- */
    string New(IObjectCreationOperation oc)
    {
        var t = Sub(oc.Type);
        if (IsStrongBox(t))
        {
            R.Add("mechanical: new StrongBox<T>(v) -> gen::NewBox (a ConditionalWeakTable's value)", Where(oc));
            var bt = Cpp(((INamedTypeSymbol)t).TypeArguments[0]);
            return $"gen::NewBox<{bt}>({(oc.Arguments.Length == 1 ? Expr(oc.Arguments[0].Value) : bt + "{}")})";
        }
        // new string([.. seq]) / new string([a, .. seq]) (a ReadOnlySpan<char> collection expression): its characters in order
        if (t.SpecialType == SpecialType.System_String && oc.Arguments.Length == 1 && Strip(oc.Arguments[0].Value) is ICollectionExpressionOperation sce)
        {
            var csb = new StringBuilder("([&]() { gen::Snap<char16_t> s_;");
            foreach (var el in sce.Elements)
                if (el is ISpreadOperation sp) csb.Append($" {SeqOf(sp.Operand)}([&](char16_t x_) {{ s_.Add(x_); return true; }});");
                else csb.Append($" s_.Add({Expr(el)});");
            R.Add("mechanical: new string(collection expression) -> its characters (gen::StrFromChars)", Where(oc));
            return csb.Append(" return gen::StrFromChars(s_.items, s_.Count()); }())").ToString();
        }
        // new StringBuilder(capacity) when the Switch's binary has no such constructor (LoaderAI's ControlNames.Safe): the parameterless one
        // (a capacity is only a size hint: the same text either way), its argument still evaluated
        if (oc.Constructor is IMethodSymbol sbc && t.ToDisplayString() == "System.Text.StringBuilder" && sbc.Parameters.Length == 1 && sbc.Parameters[0].Name == "capacity" &&
            KnownUnbound(B.Need("m", SwitchName(t), ".ctor", IlSig(sbc))) &&
            ((INamedTypeSymbol)t).InstanceConstructors.FirstOrDefault(x => x.Parameters.Length == 0) is IMethodSymbol sbNoCap)
        {
            R.Add("mechanical: new StringBuilder(capacity) the Switch lacks -> new StringBuilder() (the capacity a size hint)", Where(oc));
            return $"((void)({Expr(oc.Arguments[0].Value)}), {Call(sbNoCap, "nullptr", new List<string>(), oc)})";
        }
        var args = oc.Arguments.OrderBy(a => a.Parameter.Ordinal).Select(a => Expr(a.Value)).ToList();
        // an array of ours for a game / corlib constructor's collection parameter (AINova's new List<int>(new int[11])): the managed array,
        // as a game call's
        if (oc.Constructor != null && OriginOf(t) != Origin.Tfex)
            for (int ai = 0; ai < oc.Arguments.Length && ai < args.Count; ai++)
            {
                var av = oc.Arguments.OrderBy(a => a.Parameter.Ordinal).ElementAt(ai);
                IOperation bare = av.Value; while (bare is IConversionOperation bcv) bare = bcv.Operand;
                if (bare.Type is IArrayTypeSymbol aat && av.Parameter.Type is not IArrayTypeSymbol) args[ai] = $"gen::ManagedOf({args[ai]}, {B.Need("t", SwitchName(aat))})";
            }
        if (RewriteNew(oc, args) is string rw) return WithInitializer(oc, rw);   // (FortRise's `new patch_ArcherData { ... }`: the members set on the glue's object)
        if (IsFrOption(t))   // new Option<T>(v): Some(v); new Option<T>(): none (C#'s parameterless struct constructor: no initializers run)
        {
            R.Add("mechanical: FortRise Option<T>: a {Value, HasValue} struct", "new Option<T>");
            return args.Count == 1 ? $"{Cpp(t)}{{{args[0]}, true}}" : $"{Cpp(t)}{{}}";
        }
        // a [service] rule on a constructor ("<Type>..ctor" = { cpp = "...$0..." }, exact: no wildcard): the runtime's stand-in for a BCL
        // class the game's code lacks (runtime/tfexgen_bcl.cpp: MemoryStream, ZipArchive, HttpClient ...); an object initializer's
        // property sets then go through the type's [service] `set` rules
        if (oc.Constructor != null && gen.Services.Any(sv => sv.pattern == oc.Constructor.ContainingType.ToDisplayString() + "..ctor") &&
            Service(oc.Constructor, args, oc) is string svNew)
            return WithInitializer(oc, svNew);
        // a positional record struct of ours (ChangelogPanel's `record struct Segment(string Text, Color Color, float X, float JustifyX = 0f)`):
        // its fields are its parameters, in order (defaults filled by Roslyn's arguments)
        if (IsPodS(t) && t is INamedTypeSymbol { IsRecord: true } rs && oc.Constructor is { Parameters.Length: > 0 } rc &&
            rc.Parameters.Select(cp => cp.Name).SequenceEqual(StructsGen.Fields(rs).Select(StructsGen.FieldName)))
        {
            R.Add("mechanical: new record struct(...) -> its fields in order", Where(oc));
            return $"{Cpp(t)}{{{string.Join(", ", args)}}}";
        }
        // a struct of ours with a primary constructor whose fields are each one of its parameters (TF.State's LocalSettingsGuard.Snapshot:
        // `public readonly bool Applied = applied;`): its fields, each given its parameter
        if (IsPodS(t) && t is INamedTypeSymbol ps0 && oc.Constructor is { Parameters.Length: > 0 } pc0 && !ps0.IsRecord)
        {
            var fs0 = StructsGen.Fields(ps0).ToList();
            var map = fs0.Select(f => f.DeclaringSyntaxReferences.FirstOrDefault()?.GetSyntax() is Microsoft.CodeAnalysis.CSharp.Syntax.VariableDeclaratorSyntax
                { Initializer.Value: Microsoft.CodeAnalysis.CSharp.Syntax.IdentifierNameSyntax idn } ? pc0.Parameters.FirstOrDefault(cp => cp.Name == idn.Identifier.Text) : null).ToList();
            if (fs0.Count > 0 && map.All(x => x != null))
            {
                R.Add("mechanical: new struct(...) of a primary constructor -> each field its parameter", Where(oc));
                return $"{Cpp(t)}{{{string.Join(", ", map.Select(x => args[x.Ordinal]))}}}";
            }
        }
        // a struct of ours whose constructor only assigns its fields from its parameters (TF.State's DeterministicRandom.State:
        // `public State(ulong s0, ...) { S0 = s0; ... }`): zeroed, then each field its parameter
        if (IsPodS(t) && t is INamedTypeSymbol bs && oc.Constructor is { Parameters.Length: > 0, IsImplicitlyDeclared: false } bc &&
            bc.DeclaringSyntaxReferences.FirstOrDefault()?.GetSyntax() is Microsoft.CodeAnalysis.CSharp.Syntax.ConstructorDeclarationSyntax { Initializer: null, Body: { } cbody })
        {
            var fset = StructsGen.Fields(bs).ToList();
            // each statement `Field = <parameter>` (that argument), or `Field = <expression>` (Teams+'s Group: `Members = new List<int>()`;
            // translated inside a lambda that takes the parameters by their names)
            var assigns = new List<(string field, string value)>();
            bool exprs = false;
            foreach (var st in cbody.Statements)
            {
                if (st is Microsoft.CodeAnalysis.CSharp.Syntax.ExpressionStatementSyntax { Expression: Microsoft.CodeAnalysis.CSharp.Syntax.AssignmentExpressionSyntax asg } &&
                    asg.IsKind(Microsoft.CodeAnalysis.CSharp.SyntaxKind.SimpleAssignmentExpression) &&
                    (asg.Left is Microsoft.CodeAnalysis.CSharp.Syntax.IdentifierNameSyntax lid ? lid.Identifier.Text :
                     asg.Left is Microsoft.CodeAnalysis.CSharp.Syntax.MemberAccessExpressionSyntax { Expression: Microsoft.CodeAnalysis.CSharp.Syntax.ThisExpressionSyntax } lma ? lma.Name.Identifier.Text : null) is string ln &&
                    fset.FirstOrDefault(f => StructsGen.FieldName(f) == ln) is IFieldSymbol bf)
                {
                    if (asg.Right is Microsoft.CodeAnalysis.CSharp.Syntax.IdentifierNameSyntax rid && bc.Parameters.FirstOrDefault(cp => cp.Name == rid.Identifier.Text) is IParameterSymbol bp)
                        assigns.Add((StructsGen.FieldName(bf), "\u0001" + bp.Ordinal));
                    else if (M(asg.Right).GetOperation(asg.Right) is IOperation rop) { assigns.Add((StructsGen.FieldName(bf), Expr(rop))); exprs = true; }
                    else { assigns = null; break; }
                }
                else { assigns = null; break; }
            }
            if (assigns != null)
            {
                R.Add("mechanical: new struct(...) whose constructor assigns its fields -> each field its value", Where(oc));
                var v = $"o_{tmp++}";
                var sbs = exprs ? new StringBuilder($"([&]({string.Join(", ", bc.Parameters.Select(p => "auto " + Id(p.Name)))}) {{ {Cpp(t)} {v}{{}}; ")
                                : new StringBuilder($"([&]() {{ {Cpp(t)} {v}{{}}; ");
                foreach (var (fld, val) in assigns)
                {
                    var pi = val.StartsWith('\u0001') ? int.Parse(val[1..]) : -1;   // a parameter: by name inside the lambda, else its argument
                    sbs.Append($"{v}.{fld} = {(pi < 0 ? val : exprs ? Id(bc.Parameters[pi].Name) : args[pi])}; ");
                }
                return sbs.Append($"return {v}; }}({(exprs ? string.Join(", ", args) : "")}))").ToString();
            }
        }
        if (t.SpecialType == SpecialType.System_String && oc.Arguments.Length == 1 && oc.Arguments[0].Value.Type is IArrayTypeSymbol { ElementType.SpecialType: SpecialType.System_Char })
        { R.Add("mechanical: new string(char[]) -> gen::StrFromArr", Where(oc)); return $"gen::StrFromArr({args[0]})"; }
        if (IsAttrMgr(t))   // new AttributeManager<T>(factory, size): size defaults (the factory is TF.EX's default(T): gen::AttrMgr)
        {
            R.Add("mechanical: new AttributeManager<T>(factory, n) -> gen::AttrMgr<T> with n default items", Where(oc));
            var et = Cpp(((INamedTypeSymbol)t).TypeArguments[0]);
            return $"([&]() {{ auto* a_ = gen::NewValStatic<gen::AttrMgr<{et}>>(); a_->Init({(args.Count > 1 ? args[1] : "0")}); return a_; }}())";
        }
        // new InputState { MoveX = .., JumpCheck = .. }: the Switch's bytes, zeroed (C#'s default), each field at its offset
        if (GameValueTypes.TryGetValue(t.ToDisplayString(), out var gvSize) && oc.Arguments.Length == 0)
        {
            var gsb = new StringBuilder($"([&]() {{ gen::GameVal<{gvSize}> v_{{}};");
            foreach (var init in oc.Initializer?.Initializers ?? Enumerable.Empty<IOperation>())
                if (init is ISimpleAssignmentOperation { Target: IFieldReferenceOperation gf } ga)
                    gsb.Append($" gen::Fld<{Cpp(gf.Field.Type)}>(v_, {B.Need("f", SwitchName(gf.Field.ContainingType), gf.Field.Name)}) = {Expr(ga.Value)};");
                else gsb.Append(" " + Unsupported(init, "game struct initializer " + init.Kind) + ";");
            R.Add("mechanical: new of a game struct held by value -> its bytes, field by field", Where(oc));
            return gsb.Append(" return v_; }())").ToString();
        }
        if (t.ToDisplayString() == "Monocle.Coroutine" && oc.Arguments.Length == 1 && Strip(oc.Arguments[0].Value) is IInvocationOperation ci &&
            IsCoroutineIterator(ci.TargetMethod) && OriginOf(ci.TargetMethod.ContainingType) == Origin.Tfex)
        {
            var cm = ci.TargetMethod;
            var cargs = ci.Arguments.OrderBy(a => a.Parameter.Ordinal).Select(a => Expr(a.Value)).ToList();
            if (!cm.IsStatic) cargs.Insert(0, Recv(ci.Instance));
            string fn;
            if (SymbolEqualityComparer.Default.Equals(cm.ContainingType, type)) fn = $"{Id(cm.Name)}_co";
            else if ((OtherUnit(cm.ContainingType) is TomlTable cu0 && !cu0.ContainsKey("auto") && !cu0.ContainsKey("members") ? cu0 : AutoU(cm.ContainingType, cm)) is TomlTable cu) { includes.Add($"tfexgen_{cu["name"]}.hpp"); fn = $"tfex::gen::{cu["name"]}::{Id(cm.Name)}_co"; }
            else return Unsupported(oc, "Coroutine over a TF.EX iterator of a type no unit translates");
            R.Add("rule: new Coroutine(a TF.EX iterator) -> its C++ coroutine stepped by a 1-frame Tween (gen::CoGen, GlueRewrites.CoroutineOf)", Where(oc));
            methodCallees.Add(cm.OriginalDefinition);
            return GlueCall("TfexGlue.Rewrites.CoroutineOf", new List<string> { $"gen::CoStart({fn}({string.Join(", ", cargs)}))" }, oc);
        }
        if (IsValColl(oc.Type)) return NewValColl(oc.Type, oc, oc.Initializer?.Initializers);
        // new List<T>(a model's collection): the model list itself; the assignment that takes it copies it (ModelSet: `= *(x)`)
        if (oc.Arguments.Length == 1 && t is INamedTypeSymbol { Name: "List" } && Strip(oc.Arguments[0].Value) is var src0 &&
            IsModelList(src0))
        {
            R.Add("rule: new List<T>(a model collection) -> that collection (copied where assigned)", Where(oc));
            return args[0];
        }
        var full = t.ToDisplayString();
        if (FnaValueTypes.TryGetValue(full, out var cpp))
        {
            if (full == "Microsoft.Xna.Framework.Color")
            {
                if (args.Count == 0) return "gen::Color{0}";
                if (oc.Constructor.Parameters.Length == 4 || oc.Constructor.Parameters.Length == 3)
                    return $"gen::Color::Make({string.Join(", ", args)}{(args.Count == 3 ? (oc.Constructor.Parameters[0].Type.SpecialType == SpecialType.System_Single ? ", 1.0f" : ", 255") : "")})";
                return Unsupported(oc, "Color constructor " + oc.Constructor.ToDisplayString());
            }
            if (oc.Initializer != null) return Unsupported(oc, "object initializer on a value type");
            return args.Count == 0 ? cpp + "{}" : $"{cpp}{{{string.Join(", ", args)}}}";
        }
        if (IsPodS(t) && args.Count == 0)   // new NetworkStats() / new PingStats { rtt = -1 }: zeroed, then the initializers
        {
            var ct0 = Cpp(t);
            // a struct with an explicit parameterless constructor (FortRise's `readonly struct HairInfo()`): its property initializers
            // run (Color = Color.White ...); the implicit one (new Option<T>()) leaves everything zeroed, as C#
            var defaults = new List<(string member, IOperation value)>();
            if (oc.Constructor is { IsImplicitlyDeclared: false, Parameters.Length: 0 } && t is INamedTypeSymbol dt)
                foreach (var pp in dt.GetMembers().OfType<IPropertySymbol>().Where(IsAutoProperty))
                    foreach (var r in pp.DeclaringSyntaxReferences)
                        if (r.GetSyntax() is Microsoft.CodeAnalysis.CSharp.Syntax.PropertyDeclarationSyntax { Initializer: { } pin } &&
                            M(pin.Value).GetOperation(pin.Value) is IOperation pv)
                            defaults.Add((pp.Name, pv));
            if ((oc.Initializer == null || oc.Initializer.Initializers.Length == 0) && defaults.Count == 0) return ct0 + "{}";
            var v = $"o_{tmp++}";
            var sb0 = new StringBuilder($"([&]() {{ {ct0} {v}{{}}; ");
            if (defaults.Count > 0) R.Add("mechanical: a struct's explicit () constructor -> its property initializers", Where(oc));
            foreach (var (dm, dv) in defaults) sb0.Append($"{v}.{dm} = {Expr(dv)}; ");
            foreach (var init in oc.Initializer?.Initializers ?? Enumerable.Empty<IOperation>())
            {
                var member = init is ISimpleAssignmentOperation sa ? sa.Target switch
                {
                    IFieldReferenceOperation fr => StructsGen.FieldName(fr.Field),
                    IPropertyReferenceOperation pr when IsAutoProperty(pr.Property) => pr.Property.Name,
                    _ => null,
                } : null;
                sb0.Append(member != null ? $"{v}.{member} = {Expr(((ISimpleAssignmentOperation)init).Value)}; " : Unsupported(init, "struct initializer " + init.Kind) + "; ");
            }
            return sb0.Append($"return {v}; }}())").ToString();
        }
        if (t.SpecialType == SpecialType.System_String && args.Count == 2 && oc.Constructor.Parameters[0].Type.SpecialType == SpecialType.System_Char)
            return $"gen::Repeat({args[0]}, {args[1]})";
        if (InBinary(t) || (OriginOf(t) == Origin.Bcl && t.IsReferenceType && t is INamedTypeSymbol { IsGenericType: false } && oc.Constructor != null))
            return WithInitializer(oc, Call(oc.Constructor, "nullptr", args, oc));   // corlib classes (System.Random): Mono's constructor in the game   // BRUTE constructors take `this`; null allocates the declaring type
        if (isClass && SymbolEqualityComparer.Default.Equals(t, type)) return WithInitializer(oc, $"New{CtorSuffix(oc.Constructor)}({string.Join(", ", args)})");
        if (t is INamedTypeSymbol ct && OriginOf(ct) == Origin.Tfex && IsClassType(ct) && (OtherUnit(ct) is TomlTable nu0 && !nu0.ContainsKey("auto") && !nu0.ContainsKey("members") ? nu0 : AutoU(ct, oc.Constructor)) is TomlTable nu)
        {
            includes.Add($"tfexgen_{nu["name"]}.hpp");
            return WithInitializer(oc, $"tfex::gen::{nu["name"]}::New{CtorSuffix(oc.Constructor)}({string.Join(", ", args)})");
        }
        if (gen.Models.IsModel(t))
        {
            includes.Add("tfexgen_Models.hpp");
            if (args.Count > 0) return Unsupported(oc, "model constructor with arguments");
            var v = $"o_{tmp++}";
            var sb = new StringBuilder($"([&]() {{ auto* {v} = gen::NewModel<tfex::gen::ws::{t.Name}>(); ");
            foreach (var init in oc.Initializer?.Initializers ?? Enumerable.Empty<IOperation>())
            {
                if (init is ISimpleAssignmentOperation a && a.Target is IPropertyReferenceOperation tp)
                {
                    var mem = gen.Models.MemberOf(t, tp.Property.Name);
                    var acc = $"{v}->{tp.Property.Name}";
                    if (ModelListAssign(t, tp.Property.Name, acc, a.Value, init) is string mla) { sb.Append(mla).Append("; "); continue; }
                    // a StrMap member given new Dictionary<string, string> { { k, v } ... }: the C++ map, each pair set
                    if (mem != null && (mem.CppType.StartsWith("StrMap<") || mem.CppType.StartsWith("KeyMap<")) && Strip(a.Value) is IObjectCreationOperation dco && dco.Arguments.Length == 0)
                    {
                        sb.Append($"{acc}.count = 0; {acc}.null = false; ");
                        foreach (var di in dco.Initializer?.Initializers ?? Enumerable.Empty<IOperation>())
                            if (di is IInvocationOperation { TargetMethod.Name: "Add" } dadd && dadd.Arguments.Length == 2)
                            {
                                var da = dadd.Arguments.OrderBy(x => x.Parameter.Ordinal).ToList();
                                sb.Append($"gen::StrMapSet(&{acc}, {Expr(da[0].Value)}, {Expr(da[1].Value)}); ");
                            }
                            else sb.Append(Unsupported(di, "model dictionary initializer " + di.Kind) + "; ");
                        R.Add("rule: new Dictionary<string, string> { ... } for a model's map -> the StrMap, pair by pair", Where(init));
                        continue;
                    }
                    // `Stats = stats?.ToList()` (TF.EX's MatchEnded: a model list parameter, maybe null): the elements copied, or a null list
                    if (mem != null && mem.CppType.StartsWith("List<") && Strip(a.Value) is IConditionalAccessOperation { WhenNotNull: IInvocationOperation { TargetMethod.Name: "ToList" } } mca &&
                        IsModelCollection(Strip(mca.Operation).Type))
                    {
                        R.Add("mechanical: a model list member = x?.ToList() -> gen::SetModelColl (the copy, or null)", Where(init));
                        sb.Append($"gen::SetModelColl({acc}, {Expr(mca.Operation)}); ");
                        continue;
                    }
                    var val = Expr(a.Value);
                    if (mem != null && mem.CppType.StartsWith("KeyMap<") && IsValMap(Strip(a.Value).Type))
                    {   // `RawStats = BuildRawStats(...)` (MatchStatsReport.Save): a value map of ours copied into the model's map
                        R.Add("mechanical: a model map member = a value map of ours -> gen::SetKeyMapFrom (entry by entry)", Where(init));
                        sb.Append($"gen::SetKeyMapFrom({acc}, {val}); ");
                        continue;
                    }
                    sb.Append(mem == null ? Unsupported(init, "computed model property in an initializer") :
                              mem.CppType.StartsWith("Str<") ? $"gen::SetStr({acc}, {val}); " : mem.CppType == "MStr" ? $"({acc}).Set({val}); " :
                              mem.CppType.StartsWith("Opt<ws::") ? $"gen::SetRef({acc}, {val}); " :
                              mem.CppType.StartsWith("List<") || (mem.CppType.StartsWith("StrMap<") || mem.CppType.StartsWith("KeyMap<")) ? $"{acc} = *({val}); " : $"{acc} = {val}; ");
                }
                else sb.Append(Unsupported(init, "model initializer " + init.Kind) + "; ");
            }
            R.Add("rule: model object made by TF.EX code (a ring of 8 per type)", Where(oc));
            return sb.Append($"return {v}; }}())").ToString();
        }
        if (t is INamedTypeSymbol gt && gt.IsGenericType && OriginOf(gt) == Origin.Bcl)   // new List<Notification>(): the game's instantiation over the nearest game type
        {
            var (k, count, kp, cp) = order.Of4(oc.Constructor);
            var ctor = B.Need("m", SwitchName(gt), ".ctor", IlSig(oc.Constructor), k, count, kp, cp);
            var ps = new[] { "void*" }.Concat(oc.Constructor.Parameters.Select(p => GameParamCpp(p.Type)));
            var made = $"gen::Fn<void* (*)({string.Join(", ", ps)})>({ctor})({string.Join(", ", new[] { "nullptr" }.Concat(args))})";
            if (oc.Initializer == null || oc.Initializer.Initializers.Length == 0) return made;
            // a collection initializer (new List<string> { "a", "b" }): C#'s Add per item
            var v = $"l_{tmp++}";
            var sb2 = new StringBuilder($"([&]() {{ void* {v} = {made};");
            foreach (var init in oc.Initializer.Initializers)
            {
                if (init is not IInvocationOperation { TargetMethod.Name: "Add" } addInv) return Unsupported(oc, "initializer " + init.Kind + " on " + full);
                var aa = addInv.Arguments.OrderBy(a => a.Parameter.Ordinal).Select(a => Expr(a.Value)).ToList();
                sb2.Append($" {Call(addInv.TargetMethod, v, aa, oc)};");
            }
            R.Add("mechanical: collection initializer -> Add per item", Where(oc));
            return sb2.Append($" return {v}; }}())").ToString();
        }
        return Unsupported(oc, "new " + full);
    }

    /* ---------------------------------------------------------------- calls ---------------------------------------------------------------- */
    static readonly Dictionary<string, string> MathMap = new()
    {
        ["System.MathF.Pow"] = "powf", ["System.MathF.Floor"] = "floorf", ["System.MathF.Ceiling"] = "ceilf", ["System.MathF.Sqrt"] = "sqrtf",
        ["System.MathF.Abs"] = "fabsf", ["System.MathF.Sin"] = "sinf", ["System.MathF.Cos"] = "cosf", ["System.MathF.Round"] = "rintf",
        ["System.MathF.Min"] = "fminf", ["System.MathF.Max"] = "fmaxf",
        ["System.Math.Ceiling"] = "std::ceil", ["System.Math.Floor"] = "std::floor", ["System.Math.Sqrt"] = "std::sqrt", ["System.Math.Pow"] = "std::pow",
        ["System.Math.Round"] = "std::rint", ["System.Math.Abs"] = "gen_abs", ["System.Math.Max"] = "gen_max", ["System.Math.Min"] = "gen_min",
        ["System.Math.Sign"] = "gen_sign",
    };

    /* Task.Run(() => service.X(...)) / Task.Run(async () => { ...; await service.X(...); }): TF.EX keeps the game thread free while its
     * services talk to the server; the Switch's services never block (their callbacks come on the game thread), so the body runs inline.
     * Only when nothing follows an await (its continuation would run later in C#, at once here): otherwise unsupported. */
    int inTaskRun;
    static bool IsTaskType(ITypeSymbol t) => t?.ToDisplayString() == "System.Threading.Tasks.Task";
    string TaskRun(IInvocationOperation inv)
    {
        if (inv.Arguments.Length < 1) return null;
        var arg = inv.Arguments[0].Value;
        while (arg is IConversionOperation c) arg = c.Operand;
        if (arg is IDelegateCreationOperation { Target: IMethodReferenceOperation mr } && mr.Method.Parameters.Length == 0)
        {
            R.Add("rule: Task.Run -> inline (S-async: the Switch's services don't block)", Where(inv));
            var callee = mr.Method;
            string call;
            if (gen.ServiceRule(callee) != null) call = Service(callee, i => "", inv, mr.Instance);
            else if (SymbolEqualityComparer.Default.Equals(callee.ContainingType, type))   // Task.Run(SendLeaveLobby): this class's own method
            { methodCallees.Add(callee.OriginalDefinition); call = $"{MName(callee)}({(callee.IsStatic ? "" : Recv(mr.Instance))})"; }
            else if (!callee.IsStatic && callee.ContainingType.TypeKind == TypeKind.Interface && gen.ImplsOf(callee.ContainingType) is { Count: 1 } one &&
                     gen.ImplMember(one[0], callee) is IMethodSymbol impl &&
                     (OtherUnit(one[0]) is TomlTable iu0 && !iu0.ContainsKey("auto") && !iu0.ContainsKey("members") ? iu0 : AutoU(one[0], impl)) is TomlTable iu)
            {   // Task.Run(matchmakingService.AddLocalPlayer): through the interface to its single TF.EX implementation, as a call would
                methodCallees.Add(impl.OriginalDefinition); includes.Add($"tfexgen_{iu["name"]}.hpp");
                call = $"tfex::gen::{iu["name"]}::{Id(impl.Name)}({Recv(mr.Instance)})";
            }
            else call = Call(callee, callee.IsStatic ? null : Recv(mr.Instance), new List<string>(), inv);
            return $"(void)({call})";
        }
        if (arg is not IDelegateCreationOperation { Target: IAnonymousFunctionOperation af }) return null;
        var ops = af.Body.Operations.Where(o => o is not IReturnOperation { ReturnedValue: null }).ToList();
        // code after an await: inline too when every await is a statement whose result is dropped (`await svc.RematchChoice();`): the
        // continuation runs once the request is issued instead of once it is sent (S-async); an awaited result still blocks
        var awaits = af.Body.Descendants().OfType<IAwaitOperation>().ToList();
        bool after = false;
        for (int i = 0; i < ops.Count - 1; i++) if (ops[i].DescendantsAndSelf().Any(o => o is IAwaitOperation)) after = true;
        if (ops.Count > 0 && ops[^1].DescendantsAndSelf().OfType<IAwaitOperation>().Count() > 1) after = true;
        if (after && !awaits.All(a => a.Parent is IExpressionStatementOperation)) return Unsupported(inv, "Task.Run with code after an await");
        R.Add(after ? "rule: Task.Run with statement awaits -> inline, continuation right after the request (S-async)" : "rule: Task.Run -> inline (S-async: the Switch's services don't block)", Where(inv));
        var sb = new StringBuilder("([&]() {\n");
        inTaskRun++;
        var saveCatch = catchLabels.ToList(); catchLabels.Clear();   // (inside the C++ lambda: its statement's fault check comes after it)
        try
        {
            foreach (var o in af.Body.Operations)
                if (o is IReturnOperation { ReturnedValue: { } rv }) sb.Append($"        (void)({Expr(rv)});\n");   // () => svc.X(): the task, dropped (inline)
                else Stmt(o, sb, 2);
        }
        finally { inTaskRun--; catchLabels.AddRange(saveCatch); }
        return sb.Append("    }())").ToString();
    }

    /* MessagePackSerializer as TF.EX uses it (Serialize then ConvertToJson; ConvertFromJson then Deserialize): the bytes carry UTF-8 JSON,
     * the models' generated JWrite / JRead do the work (runtime/tfexgen_msgpack.hpp) */
    string MessagePack(IInvocationOperation inv)
    {
        var m = inv.TargetMethod;
        var a = inv.Arguments.OrderBy(x => x.Parameter.Ordinal).ToList();
        if (a.Count == 0) return null;
        var a0 = Strip(a[0].Value);   // byte[] -> ReadOnlyMemory<byte>: the array itself
        var bytes = B.Need("t", "System.Byte[]");
        string r = m.Name switch
        {
            "Serialize" when m.IsGenericMethod && gen.Models.IsModel(m.TypeArguments[0]) => $"gen::JsonBytes({Expr(a0)}, {bytes})",
            "Deserialize" when m.IsGenericMethod && gen.Models.IsModel(m.TypeArguments[0]) => $"gen::FromJsonBytes<{Cpp(m.TypeArguments[0]).TrimEnd('*')}>({Expr(a0)})",
            "ConvertToJson" => $"gen::JsonString({Expr(a0)})",
            "ConvertFromJson" => $"gen::JsonUtf8({Expr(a0)}, {bytes})",
            _ => null,
        };
        // a [MessagePackObject] class that isn't a lobby model (TF.EX's ArcherSkinBundle: binary, its bytes hashed and streamed): the
        // glue's formatter for it (translate.toml [[rewrite]] on the constructed method; glue/Skins.cs)
        if (r == null && m.IsGenericMethod && m.Name is "Serialize" or "Deserialize" &&
            Rules("rewrite").FirstOrDefault(x => x.ContainsKey("call") && (string)x["call"] == m.ToDisplayString()) is Tomlyn.Model.TomlTable mpRule)
        {
            R.Add("hand rule: call rewritten to glue (translate.toml [[rewrite]])", $"{m.ToDisplayString()} -> {mpRule["to"]}: {mpRule["why"]}");
            return GlueCall((string)mpRule["to"], new List<string> { Expr(a0) }, inv);
        }
        if (r == null) return null;
        includes.Add("tfexgen_msgpack.hpp");
        R.Add("rule: MessagePackSerializer -> JSON through the models' JWrite / JRead (TF.EX converts to JSON at once)", m.Name);
        return r;
    }

    /* System.String's overloaded members the binder can't pick (several bodies of one name): gen:: helpers, ordinal/invariant as TF.EX's
     * uses (UI text) */
    string StringCall(IInvocationOperation inv)
    {
        var m = inv.TargetMethod;
        var a = inv.Arguments.OrderBy(x => x.Parameter.Ordinal).ToList();
        string Self() => Expr(inv.Instance);
        bool IgnoreCase(IArgumentOperation x) => x.Value.ConstantValue.HasValue && x.Value.Type?.Name == "StringComparison" &&
                                                 Convert.ToInt32(x.Value.ConstantValue.Value) is 1 or 3 or 5;   // CurrentCultureIgnoreCase, InvariantCultureIgnoreCase, OrdinalIgnoreCase
        switch (m.Name)
        {
            case "Equals" when m.IsStatic && a.Count >= 2 && a[0].Value.Type?.SpecialType == SpecialType.System_String:
                R.Add("mechanical: string member -> gen:: helper", "String.Equals");
                return $"gen::{(a.Count == 3 && IgnoreCase(a[2]) ? "StrEqI" : "StrEq")}({Expr(a[0].Value)}, {Expr(a[1].Value)})";
            case "Equals" when !m.IsStatic && a.Count >= 1 && a[0].Value.Type?.SpecialType == SpecialType.System_String:
                R.Add("mechanical: string member -> gen:: helper", "String.Equals");
                return $"gen::{(a.Count == 2 && IgnoreCase(a[1]) ? "StrEqI" : "StrEq")}({Self()}, {Expr(a[0].Value)})";
            case "ToUpper" or "ToUpperInvariant" or "ToLower" or "ToLowerInvariant" when !m.IsStatic:
                R.Add("mechanical: string member -> gen:: helper", "String." + m.Name);
                return $"gen::{(m.Name.StartsWith("ToUpper") ? "Upper" : "Lower")}({Self()})";
            case "Split" when !m.IsStatic && a.Count >= 1 && a[0].Value.Type?.SpecialType == SpecialType.System_Char && (a.Count == 1 || a[1].Value.ConstantValue is { HasValue: true, Value: 0 }):
                R.Add("mechanical: string member -> gen:: helper", "String.Split(char)");
                return $"gen::Split({Self()}, {Expr(a[0].Value)}, bind::rt_StringArray)";
            case "Split" when !m.IsStatic && a.Count >= 1 && a[0].Value.Type?.SpecialType == SpecialType.System_Char && a.Count == 2 && a[1].Value.ConstantValue is { HasValue: true, Value: 1 }:
                R.Add("mechanical: string member -> gen:: helper", "String.Split(char, RemoveEmptyEntries)");
                return $"gen::SplitAny({Self()}, (const char16_t[]){{{Expr(a[0].Value)}}}, 1, true, bind::rt_StringArray)";
            case "Split" when !m.IsStatic && a.Count == 1 && Strip(a[0].Value) is ICollectionExpressionOperation sce && sce.Elements.All(e => e.Type?.SpecialType == SpecialType.System_Char):
                R.Add("mechanical: string member -> gen:: helper", "String.Split(params char)");
                return $"gen::SplitAny({Self()}, (const char16_t[]){{{string.Join(", ", sce.Elements.Select(Expr))}}}, {sce.Elements.Length}, false, bind::rt_StringArray)";
            case "Split" when !m.IsStatic && a.Count >= 2 && a[0].Value.Type?.SpecialType == SpecialType.System_String && a[1].Value.Type?.SpecialType == SpecialType.System_Int32 &&
                              (a.Count == 2 || a[2].Value.ConstantValue is { HasValue: true, Value: 0 }):
                R.Add("mechanical: string member -> gen:: helper", "String.Split(string, count)");
                return $"gen::SplitStr({Self()}, {Expr(a[0].Value)}, {Expr(a[1].Value)}, bind::rt_StringArray)";
            case "Trim" or "TrimStart" or "TrimEnd" when !m.IsStatic:
            {
                var chars = new List<string>();
                foreach (var x in a)
                {
                    var v = Strip(x.Value);
                    if (v is IArrayCreationOperation { Initializer: var ci } && ci != null) chars.AddRange(ci.ElementValues.Select(Expr));
                    else if (v is ICollectionExpressionOperation ce && ce.Elements.All(e => e.Type?.SpecialType == SpecialType.System_Char)) chars.AddRange(ce.Elements.Select(Expr));   // params ReadOnlySpan<char> (C# 13)
                    else if (v.Type?.SpecialType == SpecialType.System_Char) chars.Add(Expr(v));
                    else return null;
                }
                R.Add("mechanical: string member -> gen:: helper", "String." + m.Name);
                var arr = chars.Count == 0 ? "u'\\0'" : string.Join(", ", chars);
                return $"gen::Trim({Self()}, (const char16_t[]){{{arr}}}, {chars.Count}, {(m.Name != "TrimEnd" ? "true" : "false")}, {(m.Name != "TrimStart" ? "true" : "false")})";
            }
            case "StartsWith" or "EndsWith" when !m.IsStatic && a.Count == 1 && a[0].Value.Type?.SpecialType == SpecialType.System_String:
                R.Add("mechanical: string member -> gen:: helper", "String." + m.Name);
                return $"gen::{m.Name}({Self()}, {Expr(a[0].Value)})";
            case "Contains" when !m.IsStatic && a.Count == 1 && a[0].Value.Type?.SpecialType == SpecialType.System_Char:
            {   // (char): .NET Core's, not in the Switch's Mono (TF.EX's ArcherDataExtensions: name.Contains('/'))
                R.Add("mechanical: string member -> gen:: helper", "String.Contains(char)");
                return $"([&]() {{ void* s_ = {Self()}; char16_t c_ = {Expr(a[0].Value)}; for (int32_t i_ = 0; i_ < gen::Len(s_); i_++) if (gen::CharAt(s_, i_) == c_) return true; return false; }}())";
            }
            case "StartsWith" or "EndsWith" when !m.IsStatic && a.Count == 1 && a[0].Value.Type?.SpecialType == SpecialType.System_Char:
            {   // (char): .NET Core's, not in the Switch's Mono (FortRise's EntryExtensions.ResolveID: id.StartsWith('@'))
                R.Add("mechanical: string member -> gen:: helper", "String." + m.Name + "(char)");
                var self0 = Self();
                return m.Name == "StartsWith" ? $"([&]() {{ void* s_ = {self0}; return gen::Len(s_) > 0 && gen::CharAt(s_, 0) == {Expr(a[0].Value)}; }}())"
                                              : $"([&]() {{ void* s_ = {self0}; return gen::Len(s_) > 0 && gen::CharAt(s_, gen::Len(s_) - 1) == {Expr(a[0].Value)}; }}())";
            }
            case "Join" when m.IsStatic && a.Count == 2 && a[0].Value.Type?.SpecialType is SpecialType.System_String or SpecialType.System_Char:
            {   // (char separator: .NET Core's Join(char, ...), not in the Switch's Mono: the separator as a one-character string)
                var src = a[1].Value;
                while (src is IConversionOperation c && c.IsImplicit) src = c.Operand;
                var seq = src.Type is IArrayTypeSymbol ? $"gen::FromArr({Expr(src)})" : SeqOf(src);
                if (seq == null) return null;
                R.Add("mechanical: string member -> gen:: helper", "String.Join");
                // Join<T>(sep, values) of numbers (TF.EX's SkinOverlayService: string.Join(",", sprite.BowXOffsets)): each value's ToString()
                var jet = src.Type is IArrayTypeSymbol jat ? jat.ElementType : src.Type is INamedTypeSymbol { TypeArguments.Length: 1 } jnt ? jnt.TypeArguments[0] : null;
                if (jet != null && jet.SpecialType is SpecialType.System_Int32 or SpecialType.System_Int64 or SpecialType.System_Int16 or SpecialType.System_Byte or SpecialType.System_UInt32)
                    seq = $"gen::Select({seq}, [](auto x_) -> void* {{ return gen::FromInt((int64_t)x_); }})";
                var sepE = a[0].Value.Type?.SpecialType == SpecialType.System_Char ? StringOf(a[0].Value) : Expr(a[0].Value);
                return $"gen::Join({sepE}, {seq})";
            }
        }
        return null;
    }

    /* logger.LogInformation("Sent {kind} report ({size} KB)", kind, size): Microsoft.Extensions.Logging's message template, its
     * placeholders filled with the arguments in order (strings, numbers, bools, chars, enums: StringOf; another type keeps its
     * {placeholder}), through the logger's [service] rule with the formatted text as its message ($1) */
    string LogTemplate(IInvocationOperation inv)
    {
        var m = inv.TargetMethod;
        if (m.ContainingType.ToDisplayString() != "Microsoft.Extensions.Logging.LoggerExtensions" || !m.Name.StartsWith("Log")) return null;
        var ps = inv.Arguments.FirstOrDefault(a => a.ArgumentKind == ArgumentKind.ParamArray);
        var msg = inv.Arguments.FirstOrDefault(a => a.Parameter?.Name == "message")?.Value;
        if (ps == null || msg?.ConstantValue.Value is not string tmpl || Strip(ps.Value) is not IArrayCreationOperation { Initializer: { } pin } || pin.ElementValues.Length == 0)
            return null;
        var vals = pin.ElementValues.Select(v => { while (v is IConversionOperation c && c.IsImplicit) v = c.Operand; return v; }).ToList();
        var parts = new List<string>();
        int at = 0, k = 0;
        foreach (System.Text.RegularExpressions.Match ph in System.Text.RegularExpressions.Regex.Matches(tmpl, @"\{[^{}]+\}"))
        {
            if (ph.Index > at) parts.Add(StringLiteral(tmpl[at..ph.Index]));
            var v = k < vals.Count ? vals[k] : null;
            k++;
            bool stringable = v?.Type is { } vt && (vt.SpecialType is SpecialType.System_String or SpecialType.System_Boolean or SpecialType.System_Char or
                SpecialType.System_Int16 or SpecialType.System_Int32 or SpecialType.System_Int64 or SpecialType.System_UInt16 or SpecialType.System_UInt32 or
                SpecialType.System_Byte or SpecialType.System_SByte or SpecialType.System_Single or SpecialType.System_Double || vt.TypeKind == TypeKind.Enum);
            parts.Add(stringable ? $"gen::OrNullText({StringOf(v)})" : StringLiteral(ph.Value));
            at = ph.Index + ph.Length;
        }
        if (at < tmpl.Length) parts.Add(StringLiteral(tmpl[at..]));
        if (parts.Count == 0) return null;
        var text = parts[0];
        for (int i = 1; i < parts.Count; i++) text = $"gen::Concat({text}, {parts[i]})";
        R.Add("mechanical: a logger's message template -> its placeholders filled in order", Where(inv));
        var argOps = inv.Arguments.OrderBy(a => a.Parameter.Ordinal).ToList();
        int msgAt = argOps.FindIndex(a => a.Parameter?.Name == "message");
        return Service(m, i => i == msgAt ? text : i < argOps.Count && i != msgAt ? Expr(argOps[i].Value) : "", inv, inv.Instance);
    }

    string Invoke(IInvocationOperation inv)
    {
        var m = inv.TargetMethod;
        var owner = m.ContainingType;
        var full = owner.ToDisplayString() + "." + m.Name;
        if (m.MethodKind == MethodKind.LocalFunction && m.IsGenericMethod)
            return $"{LocalInstName(m.OriginalDefinition, m.TypeArguments.Select(Sub))}({string.Join(", ", inv.Arguments.OrderBy(a => a.Parameter.Ordinal).Select(a => Expr(a.Value)))})";
        if (m.MethodKind == MethodKind.LocalFunction)
            return $"{Id(m.Name)}({string.Join(", ", inv.Arguments.OrderBy(a => a.Parameter.Ordinal).Select(a => Expr(a.Value)))})";
        if (owner.ToDisplayString() is "System.DateTime" or "System.TimeSpan") return TimeCall(inv);
        // Unsafe.IsNullRef(ref x): x a ref local over CollectionsMarshal.GetValueRefOrNullRef -> the key was missing; any other local (a
        // plain one: FortRise's ModSubtextures.GetTexture passes a value local, never a null ref) -> false
        if (full == "System.Runtime.CompilerServices.Unsafe.IsNullRef" && inv.Arguments.Length == 1)
        {
            if (Strip(inv.Arguments[0].Value) is ILocalReferenceOperation nl && refFound.TryGetValue(nl.Local, out var fv)) return $"(!{fv})";
            R.Add("mechanical: Unsafe.IsNullRef of a value local -> false (only a ref can be a null ref)", Where(inv));
            return "false";
        }
        if (full == "MonoMod.ModInterop.ModInteropManager.ModInterop")
        {
            R.Add("mechanical: ModInterop registration (typeof(X).ModInterop()) -> nothing: an import calls its export directly", Where(inv));
            return "((void)0)";
        }
        if (owner.TypeKind == TypeKind.Delegate && m.Name == "Invoke" && Strip(inv.Instance) is IFieldReferenceOperation { Field: { IsStatic: true } imf } &&
            ModImportUnit(imf) is string imu)
            return ModImportCall(imf, imu, inv);
        // Path.Combine(a, b, c, ...) (C# 13 binds it to Combine(params ReadOnlySpan<string>), which Mono hasn't): folded into Combine(a, b)
        if (full == "System.IO.Path.Combine" && inv.Arguments.Length == 1 && Strip(inv.Arguments[0].Value) is ICollectionExpressionOperation pce &&
            owner.GetMembers("Combine").OfType<IMethodSymbol>().FirstOrDefault(x => x.Parameters.Length == 2 && x.Parameters.All(q => q.Type.SpecialType == SpecialType.System_String)) is IMethodSymbol c2 &&
            pce.Elements.Length >= 1)
        {
            R.Add("mechanical: Path.Combine(a, b, c, ...) -> Combine(Combine(a, b), c) ...", Where(inv));
            var acc0 = Expr(pce.Elements[0]);
            foreach (var el in pce.Elements.Skip(1)) acc0 = Call(c2, null, new List<string> { acc0, Expr(el) }, inv);
            return acc0;
        }
        // sb.Append($"...") / sb.AppendLine($"..."): C# passes an interpolated-string handler (.NET 6), which Mono's StringBuilder doesn't
        // have: the string built as any interpolation, then the string overload (ScenarioMapWriter's map XML)
        if (owner.ToDisplayString() == "System.Text.StringBuilder" && m.Name is "Append" or "AppendLine" && inv.Arguments.Length == 1 &&
            Strip(inv.Arguments[0].Value) is IInterpolatedStringHandlerCreationOperation hc &&
            owner.GetMembers(m.Name).OfType<IMethodSymbol>().FirstOrDefault(x => x.Parameters.Length == 1 && x.Parameters[0].Type.SpecialType == SpecialType.System_String) is IMethodSymbol sbStr)
        {
            R.Add("mechanical: StringBuilder.Append($\"...\") (an interpolated-string handler) -> the string, then Append(string)", Where(inv));
            return Call(sbStr, Expr(inv.Instance), new List<string> { HandlerString(hc.Content) }, inv);
        }
        if (owner.ToDisplayString() == "System.Version")   // a packed int64 (UnitEmitter.Cpp): TryParse, ToString
        {
            R.Add("mechanical: System.Version -> a packed int64 (gen::Version*)", m.Name);
            if (m.Name == "TryParse" && m.IsStatic && inv.Arguments.Length == 2 && inv.Arguments[0].Value.Type?.SpecialType == SpecialType.System_String)
                return $"gen::VersionTryParse({Expr(inv.Arguments[0].Value)}, {LValue(inv.Arguments[1].Value)})";
            if (m.Name == "ToString" && !m.IsStatic && inv.Arguments.Length == 0) return $"gen::VersionString({Expr(inv.Instance)})";
            return Unsupported(inv, "System.Version." + m.Name);
        }
        if (IsPcInputOwner(owner))   // Keyboard.GetState(), state.IsKeyDown(k), state.GetPressedKeys(): nothing pressed (S-input)
        {
            R.Add("TFEX-DIFF S-input: the PC keyboard / mouse -> nothing pressed", Where(inv));
            return m.ReturnsVoid ? "((void)0)" : m.ReturnType is IArrayTypeSymbol rat ? $"gen::Arr<{Cpp(rat.ElementType)}>{{}}" : Literal(null, m.ReturnType);
        }
        if (inv.Instance != null && IsAttrMgr(owner))
        {
            var am = Expr(inv.Instance);
            var aa = inv.Arguments.OrderBy(x => x.Parameter.Ordinal).ToList();
            R.Add("mechanical: AttributeManager<T> member -> gen::AttrMgr<T>", Where(inv));
            var amEl = ((INamedTypeSymbol)owner).TypeArguments[0];
            switch (m.Name)
            {
                case "Get": return $"(&({am})->items)";
                case "Count": return $"({am})->items.Count()";
                case "Clear": return $"({am})->items.Clear()";
                case "Update":
                {
                    var src = Strip(aa[0].Value);
                    var seq = IsValColl(src.Type) && !(src is IInvocationOperation { TargetMethod: var sm } && IsEnumerable(sm)) ? $"(*({Expr(src)}))" : SeqOf(src);
                    return $"([&]() {{ auto* a_ = {am}; a_->items.Clear(); {seq}([&]({Cpp(amEl)} x_) {{ a_->items.Add(x_); return true; }}); }}())";
                }
                case "Reset": return $"([&]() {{ auto* a_ = {am}; int32_t n_ = a_->items.Count(); a_->items.Clear(); a_->Init(n_); }}())";
            }
            return Unsupported(inv, "AttributeManager member " + m.Name);
        }
        // reflection on a known game method: AccessTools.Method(typeof(T), "N") is resolved where it's invoked (a direct call), so the
        // MethodInfo itself is nothing; mi.Invoke(target, args) with mi a field made that way -> the call
        if ((full is "HarmonyLib.AccessTools.Method" or "HarmonyLib.AccessTools.DeclaredMethod" or "System.Type.GetMethod" or "HarmonyLib.AccessTools.PropertySetter" or
             "HarmonyLib.AccessTools.DeclaredPropertySetter" or "HarmonyLib.AccessTools.PropertyGetter" or "HarmonyLib.AccessTools.DeclaredPropertyGetter") && ReflectedMethod(inv) is not null)
        { R.Add("mechanical: a reflected game method (AccessTools.Method / typeof(T).GetMethod) -> resolved where it's used (a direct call or delegate)", Where(inv)); return "nullptr"; }
        // Delegate.CreateDelegate(typeof(D), target, mi) / AccessTools.MethodDelegate<D>(mi), mi a reflected game method: the delegate itself
        if (full == "System.Delegate.CreateDelegate" && inv.Arguments.Length == 3)
        {
            var ca = inv.Arguments.OrderBy(a => a.Parameter.Ordinal).ToList();
            if (Strip(ca[0].Value) is ITypeOfOperation { TypeOperand: INamedTypeSymbol { TypeKind: TypeKind.Delegate } cd } && MethodFrom(ca[2].Value) is IMethodSymbol cm)
            {
                // a static method closed over its first argument (CreateDelegate(typeof(Action), self, PlayerExtensions.ShieldDelegate)): a
                // thunk whose target is that argument
                if (cm.IsStatic && ca[1].Value.ConstantValue is not { HasValue: true, Value: null } && cd.DelegateInvokeMethod is { } cdi &&
                    cm.Parameters.Length == cdi.Parameters.Length + 1)
                    return ClosedStaticDelegate(inv, cd, cm, Expr(ca[1].Value));
                return ReflectedDelegate(inv, cd, cm, cm.IsStatic ? "nullptr" : Expr(ca[1].Value));
            }
        }
        if (full == "HarmonyLib.AccessTools.MethodDelegate" && inv.TargetMethod.TypeArguments.FirstOrDefault() is INamedTypeSymbol { TypeKind: TypeKind.Delegate } md &&
            inv.Arguments.Length >= 1 && MethodFrom(inv.Arguments.OrderBy(a => a.Parameter.Ordinal).First().Value) is IMethodSymbol mm)
            return ReflectedDelegate(inv, md, mm, null);
        if (m.Name == "Invoke" && owner.ToDisplayString() is "System.Reflection.MethodInfo" or "System.Reflection.MethodBase" &&
            MethodFrom(inv.Instance is IConditionalAccessInstanceOperation && reflectedRecv != null ? reflectedRecv : inv.Instance) is IMethodSymbol rm)   // (a field made by reflection, or the reflection call itself)
        {
            var ia = inv.Arguments.OrderBy(a => a.Parameter.Ordinal).ToList();
            var rargs = (ArgElements(ia[1].Value) ?? new List<IOperation>()).Select(e => Expr(Strip(e))).ToList();
            if (rargs.Count != rm.Parameters.Length) return Unsupported(inv, "reflected call with other arguments");
            R.Add("mechanical: a reflected method's Invoke -> a direct call", Where(inv));
            string call;
            if (gen.ServiceRule(rm) is TomlTable rr && rr.TryGetValue("cpp", out var rc) && rc is string rcs)
            {   // a FortRise member with a rule (Mod.SaveSettings: the mod's settings saved)
                gen.Rules.Use("service", rm.ContainingType.ToDisplayString() + "." + rm.Name);
                if (rr.TryGetValue("include", out var rinc)) foreach (var h in ((string)rinc).Split(',')) includes.Add(h.Trim());
                for (int i = rargs.Count - 1; i >= 0; i--) rcs = rcs.Replace("$" + i, rargs[i]);
                call = rcs.Replace("$self", rm.IsStatic ? "nullptr" : Expr(ia[0].Value));
            }
            else if (OriginOf(rm.ContainingType) == Origin.Tfex && rm.IsStatic && !InBinary(rm.ContainingType))
            {   // a method of ours (a mod's IHookable.Load / IRegisterable.Register): its unit's
                var ru = SymbolEqualityComparer.Default.Equals(rm.ContainingType, type) ? null : OtherUnit(rm.ContainingType) is TomlTable ru0 && !ru0.ContainsKey("auto") && !ru0.ContainsKey("members") ? ru0 : AutoU(rm.ContainingType, rm);
                methodCallees.Add(rm.OriginalDefinition);
                if (ru != null) includes.Add($"tfexgen_{ru["name"]}.hpp");
                call = $"{(ru != null ? $"tfex::gen::{ru["name"]}::" : "")}{MName(rm)}({string.Join(", ", rargs)})";
            }
            else call = Call(rm, rm.IsStatic ? null : Expr(ia[0].Value), rargs, inv);
            return rm.ReturnsVoid ? $"({call}, nullptr)" : call;
        }
        if (full == "System.Threading.CancellationToken.ThrowIfCancellationRequested") return "((void)0)";   // S-async
        // TF.EX's async method (translated synchronously: Task -> void) waited for: `Upload(...).Wait(timeout)` is the call, then true
        // (S-async: it has finished when the call returns)
        if (full == "System.Threading.Tasks.Task.Wait" && Strip(inv.Instance) is IInvocationOperation wi && OriginOf(wi.TargetMethod.ContainingType) == Origin.Tfex)
        {
            R.Add("rule: Task.Wait on a TF.EX async call -> the call (S-async: done when it returns)", Where(inv));
            return $"(({Expr(inv.Instance)}), true)";
        }
        // x.CompareTo(y) on a number (Teams+'s sort by X: a.X.CompareTo(b.X)): -1 / 0 / 1 (float's CompareTo isn't a callable body on the
        // Switch); NaN sorts first, as .NET's
        if (owner.SpecialType is >= SpecialType.System_SByte and <= SpecialType.System_Double && m.Name == "CompareTo" && !m.IsStatic && inv.Instance != null &&
            inv.Arguments.Length == 1 && SymbolEqualityComparer.Default.Equals(m.Parameters[0].Type, owner))
        {
            R.Add("mechanical: number.CompareTo(number) -> -1 / 0 / 1", Where(inv));
            return $"([](auto x_, auto y_) {{ return x_ < y_ ? -1 : x_ > y_ ? 1 : x_ == y_ ? 0 : (x_ != x_ ? (y_ != y_ ? 0 : -1) : 1); }}({Expr(inv.Instance)}, {Expr(inv.Arguments[0].Value)}))";
        }
        if (owner.SpecialType is SpecialType.System_Double or SpecialType.System_Single && inv.Arguments.Length == 1 &&
            m.Name is "IsNaN" or "IsInfinity" or "IsPositiveInfinity" or "IsNegativeInfinity" or "IsFinite")
        {   // double / float classification: not in the Switch's binary (AOT stripped); the compiler's builtins, same IEEE answer
            var a0 = Expr(inv.Arguments[0].Value);
            R.Add("mechanical: double/float.IsNaN / IsInfinity -> compiler builtins", Where(inv));
            return m.Name switch
            {
                "IsNaN" => $"__builtin_isnan({a0})", "IsInfinity" => $"__builtin_isinf({a0})", "IsFinite" => $"__builtin_isfinite({a0})",
                "IsPositiveInfinity" => $"([&](auto v_) {{ return __builtin_isinf(v_) && v_ > 0; }}({a0}))",
                _ => $"([&](auto v_) {{ return __builtin_isinf(v_) && v_ < 0; }}({a0}))",
            };
        }
        // Enum.HasFlag(flag): the bits themselves (the game's Enum.HasFlag takes boxed enums; an enum is its integer here)
        if (owner.SpecialType == SpecialType.System_Enum && !m.IsStatic && m.Name == "HasFlag" && inv.Instance != null && inv.Arguments.Length == 1)
        {
            R.Add("mechanical: Enum.HasFlag -> (x & flag) == flag", Where(inv));
            var fl = Expr(Strip(inv.Arguments[0].Value));
            return $"((((int64_t)({Expr(inv.Instance)})) & ((int64_t)({fl}))) == ((int64_t)({fl})))";
        }
        // char.IsAscii* (.NET 7; not in the Switch's Mono): the ASCII ranges themselves
        if (owner.SpecialType == SpecialType.System_Char && m.IsStatic && inv.Arguments.Length == 1 && m.Name is "IsAsciiLetterOrDigit" or "IsAsciiLetter" or "IsAsciiDigit")
        {
            R.Add("mechanical: char.IsAscii* -> its ASCII ranges (not in the Switch's binary)", Where(inv));
            var test = m.Name switch
            {
                "IsAsciiDigit" => "(c_ >= u'0' && c_ <= u'9')",
                "IsAsciiLetter" => "((c_ >= u'a' && c_ <= u'z') || (c_ >= u'A' && c_ <= u'Z'))",
                _ => "((c_ >= u'a' && c_ <= u'z') || (c_ >= u'A' && c_ <= u'Z') || (c_ >= u'0' && c_ <= u'9'))",
            };
            return $"([](char16_t c_) {{ return {test}; }}({Expr(inv.Arguments[0].Value)}))";
        }
        if (owner.SpecialType == SpecialType.System_Int32 && m.Name == "Parse" && inv.Arguments.Length == 1 && inv.Arguments[0].Value.Type?.SpecialType == SpecialType.System_String)
        { R.Add("mechanical: int.Parse(string) -> gen::ParseInt (not in the Switch's binary)", Where(inv)); return $"gen::ParseInt({Expr(inv.Arguments[0].Value)})"; }
        if (owner.SpecialType == SpecialType.System_Int32 && m.Name == "TryParse" && inv.Arguments.Length == 2 && inv.Arguments[0].Value.Type?.SpecialType == SpecialType.System_String)
        {   // int.TryParse(s, out n) (FortRise's ArcherLoader): not in the Switch's binary
            R.Add("mechanical: int.TryParse(string, out int) -> gen::TryParseInt", Where(inv));
            var ta = inv.Arguments.OrderBy(a => a.Parameter.Ordinal).ToList();
            return $"gen::TryParseInt({Expr(ta[0].Value)}, &{LValue(ta[1].Value is IDeclarationExpressionOperation tde ? tde.Expression : ta[1].Value)})";
        }
        if (full == "System.Array.Resize" && inv.Arguments.Length == 2 && Sub(inv.Arguments[0].Value.Type) is IArrayTypeSymbol resizeT)
        {   // Array.Resize(ref a, n): a new array of a's type, the old elements copied (as .NET)
            R.Add("mechanical: Array.Resize(ref a, n) -> a new array, the elements copied (gen::ResizeArr)", Where(inv));
            var ra = inv.Arguments.OrderBy(a => a.Parameter.Ordinal).ToList();
            return $"{LValue(ra[0].Value)} = gen::ResizeArr({Expr(ra[0].Value)}, {Expr(ra[1].Value)}, {B.Need("t", SwitchName(resizeT))})";
        }
        if (m.Name == "Clone" && inv.Arguments.Length == 0 && inv.Instance != null && Sub(inv.Instance.Type) is IArrayTypeSymbol cloneT)
        {   // a.Clone() on an array: a new array of a's type with a's elements (as .NET's shallow copy); object-typed, as Clone returns it
            R.Add("mechanical: array.Clone() -> a new array, the elements copied (gen::ResizeArr)", Where(inv));
            var asArray = inv.Parent is IConversionOperation { Type: IArrayTypeSymbol } ||   // (T[])a.Clone(): the array itself
                          inv.Parent is IConditionalAccessOperation { Parent: IConversionOperation { Type: IArrayTypeSymbol } };   // (T[])a?.Clone()
            return $"([&]() {{ auto a_ = {Expr(inv.Instance)}; return gen::ResizeArr(a_, a_.len, {B.Need("t", SwitchName(cloneT))}){(asArray ? "" : ".obj")}; }}())";
        }
        if (full == "System.Array.Copy" && inv.Arguments.Length == 3 && inv.Arguments.All(a => a.Parameter.Ordinal == 2 || Sub(Strip(a.Value).Type) is IArrayTypeSymbol))
        {   // Array.Copy(src, dst, n) between arrays: the first n elements (both translated arrays: element by element)
            R.Add("mechanical: Array.Copy(src, dst, n) -> an element copy", Where(inv));
            var ca = inv.Arguments.OrderBy(a => a.Parameter.Ordinal).ToList();
            return $"([&](auto s_, auto d_, int32_t n_) {{ for (int32_t i_ = 0; i_ < n_ && i_ < s_.len && i_ < d_.len && s_.data && d_.data; i_++) d_.data[i_] = s_.data[i_]; }}({Expr(Strip(ca[0].Value))}, {Expr(Strip(ca[1].Value))}, {Expr(ca[2].Value)}))";
        }
        if (full == "System.Guid.ToString" && inv.Arguments.Length == 0 && Strip(inv.Instance) is IInvocationOperation { TargetMethod.Name: "NewGuid" or "CreateVersion7" } ng && ng.TargetMethod.ContainingType.ToDisplayString() == "System.Guid")
        { R.Add("mechanical: Guid.NewGuid().ToString() -> gen::NewGuidString (netws::MakeUuid)", Where(inv)); return "gen::NewGuidString()"; }
        if (gen.AbsentRule(m) is TomlTable origRule && origRule.TryGetValue("orig", out var origOf) && origOf is string origName)
        {   // FortRise's `orig_X` (its name for the vanilla body): on the Switch the original under the hook of X (HookGen's Orig_ export)
            R.Add("hand rule: FortRise's orig_ body -> the original under the Switch's hook ([fortrise] orig)", $"{m.ContainingType.Name}.{m.Name}");
            includes.Add("tfexgen_hooks.hpp");
            var oid = string.Concat((m.ContainingType.Name.StartsWith("patch_") ? m.ContainingType.Name[6..] : m.ContainingType.Name).Select(ch => char.IsLetterOrDigit(ch) ? ch : '_'));   // (FortRise's patch_X: X's hook)
            var oa = (m.IsStatic ? new List<string>() : new List<string> { Expr(inv.Instance) }).Concat(inv.Arguments.OrderBy(a => a.Parameter.Ordinal).Select(a => Expr(a.Value)));
            return $"tfex::gen::hooks::Orig_{oid}_{origName}({string.Join(", ", oa)})";
        }
        if (AbsentMember(inv) != null || (inv.Instance != null && AbsentMember(inv.Instance) != null) ||
            (m.IsExtensionMethod && inv.Arguments.Length > 0 && AbsentMember(inv.Arguments.OrderBy(a => a.Parameter.Ordinal).First().Value) != null))
            return m.ReturnsVoid ? "((void)0)" : Literal(null, m.ReturnType);   // nothing to call: C#'s result with no FortRise mod
        if (full == "System.Enum.IsDefined" && inv.Arguments.Length == 2 && inv.Arguments[0].Value is ITypeOfOperation to &&
            EnumPairs(to.TypeOperand as INamedTypeSymbol) is { } defined)   // a game enum: its literals on the Switch
        {
            R.Add("PC -> Switch: Enum.IsDefined over the Switch's literals", Where(inv));
            var v = inv.Arguments[1].Value is IConversionOperation bx ? bx.Operand : inv.Arguments[1].Value;
            return $"gen::EnumIn({Expr(v)}, {{{string.Join(", ", B.EnumValues(SwitchName(to.TypeOperand)).Values.Distinct().OrderBy(x => x))}}})";
        }
        // a [[rewrite]] for this Task.Run (NetplayManager.Init's Connect: a worker thread, translate.toml) wins over the inlining below
        if (full == "System.Threading.Tasks.Task.Run" && Rules("rewrite").Any(r => r.ContainsKey("call") && (string)r["call"] == m.OriginalDefinition.ToDisplayString() &&
                (!r.ContainsKey("in") || (string)r["in"] == type.ToDisplayString())) &&
            Rewrite(inv, inv.Arguments.OrderBy(a => a.Parameter.Ordinal).Select(a => Expr(a.Value)).ToList()) is string trw) return trw;
        // Array.FindIndex(array, predicate) (InputService.KeepOnlyLocalInputInMenus, v0.28.0): the generic method isn't in the Switch's build;
        // a loop over the array, the first index the predicate takes, else -1 (.NET's)
        if (full == "System.Array.FindIndex" && inv.Arguments.Length == 2 && inv.Arguments.OrderBy(a => a.Parameter.Ordinal).ToList() is var fia &&
            fia[0].Value.Type is IArrayTypeSymbol fat)
        {
            R.Add("mechanical: Array.FindIndex(array, predicate) -> a loop (the generic method isn't in the Switch's build)", Where(inv));
            var el = Cpp(fat.ElementType);
            var arrCpp = Cpp(fat);
            var arr = arrCpp.StartsWith("gen::Arr<") ? Expr(fia[0].Value) : $"gen::GameArr<{el}>({Expr(fia[0].Value)}, bind::m_Array_get_Length)";
            return $"([&]() -> int32_t {{ auto a_ = {arr}; auto p_ = {InlineLambda(fia[1].Value)}; for (int32_t i_ = 0; i_ < a_.len; i_++) if (p_(a_[i_])) return i_; return -1; }}())";
        }
        // Interlocked (TF.EX v0.28.0's GameContext: session seats and guest inputs read on the PC's poll thread): one game thread runs TF.EX's
        // netplay on the Switch (S-async), and an aligned reference store is atomic on the console's CPU anyway. CompareExchange(ref x,
        // null, null) is the atomic-read idiom -> x; Exchange(ref x, v) -> the swap, returning the old value
        if (owner.ToDisplayString() == "System.Threading.Interlocked" && m.Name is "CompareExchange" or "Exchange")
        {
            var ia = inv.Arguments.OrderBy(a => a.Parameter.Ordinal).Select(a => a.Value).ToList();
            if (m.Name == "CompareExchange" && ia.Count == 3 && Strip(ia[1]).ConstantValue is { HasValue: true, Value: null } && Strip(ia[2]).ConstantValue is { HasValue: true, Value: null })
            { R.Add("mechanical: Interlocked.CompareExchange(ref x, null, null) -> x (one game thread, S-async)", Where(inv)); return Expr(ia[0]); }
            if (m.Name == "Exchange" && ia.Count == 2)
            {
                R.Add("mechanical: Interlocked.Exchange(ref x, v) -> the swap (one game thread, S-async)", Where(inv));
                return $"([&]() {{ auto o_ = {Expr(ia[0])}; {Assign(ia[0], Expr(ia[1]))}; return o_; }}())";
            }
        }
        if (full == "System.Threading.Tasks.Task.Run" && TaskRun(inv) is string tr) return tr;
        // task.ContinueWith(_ => X(), ...).Unwrap() (MatchmakingService.QueueSkinBundle, v0.28.0: sends one after another): the tasks run
        // inline, in order (S-async), so the continuation's body runs now, as Task.Run's
        if (m.Name == "Unwrap" && (inv.Instance ?? (inv.Arguments.Length > 0 ? inv.Arguments[0].Value : null)) is IOperation uwr &&
            Strip(uwr) is IInvocationOperation { TargetMethod.Name: "ContinueWith" } cw && TaskRun(cw) is string cwr)
            return cwr;
        // Task.Run(...).GetAwaiter().GetResult(): the inlined Task.Run (S-async: the work is done when it returns)
        if (m.Name == "GetResult" && inv.Instance is IInvocationOperation { TargetMethod.Name: "GetAwaiter", Instance: IInvocationOperation tw } &&
            tw.TargetMethod.ToDisplayString().StartsWith("System.Threading.Tasks.Task.Run") && TaskRun(tw) is string twr)
            return twr;
        // Enum.TryParse<E>(s[, ignoreCase], out e): E's literals by name (a game enum's values are the Switch's), or a number;
        // a constant ignoreCase true compares the upper-cased string with the upper-cased names
        if (full == "System.Enum.TryParse" && m.TypeArguments.Length == 1 && m.TypeArguments[0] is INamedTypeSymbol { TypeKind: TypeKind.Enum } pe &&
            (inv.Arguments.Length == 2 || (inv.Arguments.Length == 3 && inv.Arguments.OrderBy(a => a.Parameter.Ordinal).ElementAt(1).Value.ConstantValue.Value is bool)))
        {
            var ta = inv.Arguments.OrderBy(a => a.Parameter.Ordinal).ToList();
            bool ci = ta.Count == 3 && ta[1].Value.ConstantValue.Value is true;
            var lits = pe.GetMembers().OfType<IFieldSymbol>().Where(f => f.HasConstantValue).ToList();
            var names = string.Join(", ", lits.Select(f => $"u\"{(ci ? f.Name.ToUpperInvariant() : f.Name)}\""));
            var vals = string.Join(", ", lits.Select(f => EnumLiteral(Convert.ToInt64(f.ConstantValue), pe)));
            var outv = ta[^1].Value is IDeclarationExpressionOperation dex ? dex.Expression : ta[^1].Value;
            R.Add("mechanical: Enum.TryParse<E> -> E's literals by name", Where(inv));
            return $"gen::EnumTryParse({(ci ? $"gen::Upper({Expr(ta[0].Value)})" : Expr(ta[0].Value))}, {{{names}}}, {{{vals}}}, &{LValue(outv)})";
        }
        if (owner.SpecialType == SpecialType.System_String && StringCall(inv) is string sc) return sc;
        if (IsEnumerable(m)) return Linq(inv);
        // a lobby model's list member (Lobby.Mods: an ICollection<CustomMod> held by value): the C++ list
        if (inv.Instance != null && m.Name is "Add" or "Clear" && IsModelList(inv.Instance) && !IsEnumerable(m))
        {
            var ml = Expr(Strip(inv.Instance));
            R.Add("mechanical: a model list's Add / Clear -> the C++ list", Where(inv));
            return m.Name == "Clear" ? $"({ml})->Clear()" : $"({ml})->Add(*({Expr(inv.Arguments[0].Value)}))";
        }
        if (inv.Instance != null && Strip(inv.Instance) is ILocalReferenceOperation { Local: var snl } && snapLocals.TryGetValue(snl, out var sne) &&
            m.Name == "Contains" && inv.Arguments.Length == 1)
        {   // a snapshot local (a LINQ ToList kept as gen::Snap): its elements
            var set = ((INamedTypeSymbol)snl.Type).TypeArguments.FirstOrDefault() ?? (snl.Type as IArrayTypeSymbol)?.ElementType;
            return $"gen::Contains<{sne}>({Id(snl.Name)}, {Expr(inv.Arguments[0].Value)}, {EqOf(set)})";
        }
        if (inv.Instance != null && m.Name == "Contains" && inv.Arguments.Length == 1 && !IsEnumerable(m) && IsModelList(Strip(inv.Instance)) &&
            Strip(inv.Instance).Type is INamedTypeSymbol { TypeArguments: [var mce] })
        {   // a model list's Contains(x): its elements
            R.Add("mechanical: a model list's Contains -> its elements", Where(inv));
            return $"gen::Contains<{Cpp(mce)}>({SeqOf(Strip(inv.Instance))}, {Expr(inv.Arguments[0].Value)}, {EqOf(mce)})";
        }
        if (inv.Instance != null && m.Name == "FindIndex" && inv.Arguments.Length == 1 && IsModelList(Strip(inv.Instance)))
        {   // a model list's FindIndex(pred): the index of the first match, or -1
            R.Add("mechanical: a model list's FindIndex -> internal iteration (gen::FindIndex)", Where(inv));
            var fl = Strip(inv.Arguments[0].Value); while (fl is IConversionOperation flc) fl = flc.Operand;
            if (fl is IDelegateCreationOperation { Target: IAnonymousFunctionOperation fla } && Strip(inv.Instance).Type is INamedTypeSymbol { TypeArguments: [INamedTypeSymbol { Name: "List" }] })
                modelLambdaParams.Add(fla.Symbol.Parameters[0]);   // a list of lists: the element is the model's inner list
            return $"gen::FindIndex({SeqOf(Strip(inv.Instance))}, {InlineLambda(inv.Arguments[0].Value)})";
        }
        // (not over a value list: a model's ICollection<double> held as gen::VList<double>* (RoundLogic.SpotlightDephts) is no game object; its own
        // Add below. Treated as one, a round end's spotlight crashed GetState, 2026-10-22)
        if (inv.Instance != null && m.Name is "Add" or "Clear" or "Remove" or "Contains" && !Cpp(Strip(inv.Instance).Type).StartsWith("gen::V") && IListOfOp(Strip(inv.Instance)) is string ilc)
        {   // ICollection<T> members over a game List<T> / T[] (interface dispatch, as Count)
            var el = ((INamedTypeSymbol)Strip(inv.Instance).Type).TypeArguments[0];
            var lt = ws.Compilation.GetTypeByMetadataName("System.Collections.Generic.List`1")!.Construct(el);
            var co = Expr(Strip(inv.Instance));
            var a0 = inv.Arguments.Length > 0 ? Expr(inv.Arguments[0].Value) : "";
            var ct = Cpp(el);
            R.Add("mechanical: ICollection<T> member -> the List<T> body (or the array)", Where(inv));
            switch (m.Name)
            {
                case "Add": return $"gen::IAdd<{ct}>({co}, {ilc}, {MethodBody(lt, "Add", new[] { "T" })}, {a0})";
                case "Clear": return $"gen::IClear({co}, {ilc}, {MethodBody(lt, "Clear", Array.Empty<string>())})";
                case "Remove": return $"gen::IRemove<{ct}>({co}, {ilc}, {MethodBody(lt, "Remove", new[] { "T" })}, {a0})";
                default:
                {
                    var eq = el.SpecialType == SpecialType.System_String ? "[](void* x, void* y) { return gen::StrEq(x, y); }" : "[](auto x, auto y) { return x == y; }";
                    return $"gen::IContains<{ct}>({co}, {ilc}, {MethodBody(lt, "Contains", new[] { "T" })}, {a0}, {eq})";
                }
            }
        }
        if (inv.Instance != null && IsModelMap(inv.Instance) && m.Name is "TryGetValue" or "ContainsKey")
        {
            var ma = inv.Arguments.OrderBy(a => a.Parameter.Ordinal).ToList();
            return m.Name == "ContainsKey" ? $"gen::StrMapHas({Expr(Strip(inv.Instance))}, {Expr(ma[0].Value)})"
                                           : $"gen::StrMapTry({Expr(Strip(inv.Instance))}, {Expr(ma[0].Value)}, &{LValue(ma[1].Value)})";
        }
        if (inv.Instance != null && IsValMap(Strip(inv.Instance).Type))
        {
            var mp = Expr(Strip(inv.Instance));
            var va = inv.Arguments.OrderBy(a => a.Parameter.Ordinal).ToList();
            if (m.Name == "ContainsKey") return $"({mp})->ContainsKey({Expr(va[0].Value)})";
            if (m.Name == "ContainsValue" && va.Count == 1) return $"({mp})->ContainsValue({Expr(va[0].Value)})";
            if (m.Name == "TryGetValue") return $"({mp})->TryGetValue({Expr(va[0].Value)}, &{LValue(va[1].Value)})";
            if (m.Name is "Add" && va.Count == 2) return $"(void)({mp})->Add({Expr(va[0].Value)}, {Expr(va[1].Value)})";
            if (m.Name is "TryAdd" && va.Count == 2) return $"({mp})->Add({Expr(va[0].Value)}, {Expr(va[1].Value)})";   // the map's Add: false on a duplicate, TryAdd's result
            if (m.Name is "Remove" && va.Count == 1) return $"({mp})->Remove({Expr(va[0].Value)})";
            if (m.Name is "TryRemove" && va.Count == 2 && va[1].Value is IDiscardOperation or IDeclarationExpressionOperation { Expression: IDiscardOperation }) return $"({mp})->Remove({Expr(va[0].Value)})";
            if (m.Name is "Remove" or "TryRemove" && va.Count == 2) return $"([&]() {{ auto k_ = {Expr(va[0].Value)}; return ({mp})->TryGetValue(k_, &{LValue(va[1].Value)}) && ({mp})->Remove(k_); }}())";   // Remove(key, out value)
            if (m.Name is "Clear") return $"({mp})->Clear()";
            if (m.Name is "GetAlternateLookup" && AltLookupOf(m.ReturnType) != null) return mp;   // keys by span: the same map (a span key is its string)
            if (m.Name is "GetOrCreateValue" && va.Count == 1 && ((INamedTypeSymbol)Strip(inv.Instance).Type).TypeArguments[1] is INamedTypeSymbol vt &&
                vt.InstanceConstructors.FirstOrDefault(c => c.Parameters.Length == 0) is IMethodSymbol vc &&
                (OriginOf(vt) == Origin.Tfex && IsClassType(vt) && (OtherUnit(vt) is TomlTable nu0 && !nu0.ContainsKey("auto") && !nu0.ContainsKey("members") ? nu0 : AutoU(vt, vc)) is TomlTable vu))
            {
                includes.Add($"tfexgen_{vu["name"]}.hpp");
                R.Add("mechanical: ConditionalWeakTable.GetOrCreateValue -> the value map's entry, made with new V() when missing", Where(inv));
                return $"([&]() {{ auto* m_ = {mp}; auto k_ = {Expr(va[0].Value)}; void* v_ = nullptr; if (!m_->TryGetValue(k_, &v_)) {{ v_ = tfex::gen::{vu["name"]}::New(); m_->Set(k_, v_); }} return v_; }}())";
            }
            return Unsupported(inv, "value map member " + m.Name);
        }
        if (inv.Instance != null && ValKind(Strip(inv.Instance).Type) is VKind.Set or VKind.List && !IsEnumerable(m))
        {
            var vl = Expr(Strip(inv.Instance));
            var va = inv.Arguments.OrderBy(a => a.Parameter.Ordinal).Select(a => Expr(a.Value)).ToList();
            bool isSet = ValKind(Strip(inv.Instance).Type) == VKind.Set;
            switch (m.Name)
            {
                case "Add" when va.Count == 1: return isSet ? $"({vl})->Add({va[0]})" : $"({vl})->Add({va[0]})";
                case "Contains" when va.Count == 1:
                {
                    var vet = ((INamedTypeSymbol)Strip(inv.Instance).Type).TypeArguments[0];
                    return isSet ? $"({vl})->Contains({va[0]})" : $"gen::Contains<{Cpp(vet)}>(gen::FromValMap({vl}), {va[0]}, {EqOf(vet)})";
                }
                case "SetEquals" when va.Count == 1 && isSet && SymbolEqualityComparer.Default.Equals(Strip(inv.Arguments[0].Value).Type, Strip(inv.Instance).Type):
                    return $"({vl})->SetEquals({va[0]})";
                case "Remove" when va.Count == 1 && isSet: return $"({vl})->Remove({va[0]})";
                case "IntersectWith" when va.Count == 1 && isSet: return $"({vl})->IntersectWith({SeqOf(inv.Arguments[0].Value)})";   // keeps the members the sequence has
                case "RemoveAt" when va.Count == 1 && !isSet: return $"({vl})->RemoveAt({va[0]})";
                case "Enqueue" when va.Count == 1 && !isSet: return $"({vl})->Add({va[0]})";
                case "Dequeue" when va.Count == 0 && !isSet: return $"({vl})->Dequeue()";
                case "TryDequeue" when va.Count == 1 && !isSet:   // ConcurrentQueue's TryDequeue(out x): the front item, false when empty
                    return $"({vl})->TryDequeue(&{LValue(inv.Arguments.OrderBy(a => a.Parameter.Ordinal).First().Value)})";
                case "Peek" when va.Count == 0 && !isSet: return $"({vl})->Get(0)";
                case "Sort" when va.Count == 1 && !isSet && inv.Arguments[0].Value.Type is INamedTypeSymbol { Name: "Comparison" }:   // List.Sort(Comparison<T>)
                    R.Add("mechanical: List<T>.Sort(comparison) on a value list -> its Sort with the comparison inline", Where(inv));
                    return $"({vl})->Sort({InlineLambda(inv.Arguments[0].Value)})";
                case "Clear": return $"({vl})->Clear()";
                case "FindIndex" when va.Count == 1 && !isSet && inv.Arguments[0].Value.Type is INamedTypeSymbol { Name: "Predicate" }:   // List.FindIndex(Predicate<T>)
                    R.Add("mechanical: List<T>.FindIndex(predicate) on a value list -> the first index it accepts, else -1", Where(inv));
                    return $"([&]() {{ auto* l_ = {vl}; auto p_ = {InlineLambda(inv.Arguments[0].Value)}; for (int32_t i_ = 0; i_ < l_->Count(); i_++) if (p_(l_->Get(i_))) return i_; return (int32_t)-1; }}())";
            }
            return Unsupported(inv, "value collection member " + m.Name);
        }
        // x.Equals(y) / Enum.Equals over enums and numbers: == (C#'s boxing Equals compares type and value; both sides have one type here)
        if (m.Name == "Equals" && inv.Arguments.Length == 1 && inv.Instance != null && Strip(inv.Instance).Type is { } et &&
            (et.TypeKind == TypeKind.Enum || et.SpecialType is >= SpecialType.System_Boolean and <= SpecialType.System_Double))
        {
            var ea = Strip(inv.Arguments[0].Value); while (ea is IConversionOperation ec) ea = ec.Operand;
            return $"({Expr(Strip(inv.Instance))} == {Expr(ea)})";
        }
        // n.ToString() on a number / char / bool: the string helpers (invariant, as C#'s ToString() of these)
        if (m.Name == "ToString" && inv.Arguments.Length == 0 && inv.Instance != null && Sub(Strip(inv.Instance).Type)?.SpecialType is
            SpecialType.System_Int32 or SpecialType.System_Int64 or SpecialType.System_Int16 or SpecialType.System_Byte or SpecialType.System_UInt32 or
            SpecialType.System_UInt16 or SpecialType.System_SByte or SpecialType.System_Single or SpecialType.System_Double or SpecialType.System_Char or SpecialType.System_Boolean)
            return StringOf(Strip(inv.Instance));
        // s.ToString() on a string (also a T bound to string in a generic method's copy: FortRise's SpriteEntry.CreateProperty<T>): s itself.
        // The game's Object.ToString called directly returns the type's name ("System.String": every translated mod sprite's Texture)
        if (m.Name == "ToString" && inv.Arguments.Length == 0 && inv.Instance != null && Sub(Strip(inv.Instance).Type)?.SpecialType == SpecialType.System_String)
            return Expr(Strip(inv.Instance));
        // o.ToString() through object (or a T bound to object): the virtual call, as C#'s (a boxed number's digits, not "System.Int32":
        // FortRise's SpriteEntry.CreateProperty<object> over a sprite's AdditionalData)
        if (m.Name == "ToString" && inv.Arguments.Length == 0 && inv.Instance != null && m.ContainingType.SpecialType == SpecialType.System_Object &&
            Sub(Strip(inv.Instance).Type)?.SpecialType == SpecialType.System_Object)
            return $"gen::ObjToString({Expr(Strip(inv.Instance))})";
        if (m.Name == "ToString" && inv.Arguments.Length == 0 && inv.Instance != null && Strip(inv.Instance).Type is INamedTypeSymbol { TypeKind: TypeKind.Enum } ets)
            return EnumName(ets, Expr(Strip(inv.Instance)));
        if (full == "char.IsUpper" && inv.Arguments.Length == 1) { var cu = Expr(inv.Arguments[0].Value); return $"([&]() {{ char16_t c_ = {cu}; return c_ >= u'A' && c_ <= u'Z'; }}())"; }   // ASCII (TF.EX's uses: changelog text)
        if (owner.ToDisplayString() == "MessagePack.MessagePackSerializer" && MessagePack(inv) is string msgp) return msgp;
        if (full == "System.Array.Empty" && m.IsGenericMethod && inv.Arguments.Length == 0)   // a generic BCL method: a new empty T[] (C# shares one; nothing writes to it)
        {
            var aet = m.TypeArguments[0];
            return $"gen::NewArr<{Cpp(aet)}>({B.Need("t", SwitchName(ws.Compilation.CreateArrayTypeSymbol(aet)))}, 0)";
        }
        if (full == "System.Console.WriteLine") return "((void)0)";   // TF.EX's debug output (S-tools: no console on the Switch)
        // array.Contains(v) that .NET 10 binds to MemoryExtensions.Contains(ReadOnlySpan<T>, T) (TF.EX's SkinLimits.AllowedExtraDataKeys)
        // (and Contains(v, StringComparer.X), .NET 10's IEqualityComparer overload: TF.EX's ModCollections.NetplaySafeMods)
        if (full == "System.MemoryExtensions.Contains" && inv.Arguments.Length is 2 or 3 && Strip(inv.Arguments.OrderBy(a => a.Parameter.Ordinal).First().Value) is IOperation spanSrc &&
            (spanSrc is IConversionOperation { Operand.Type: IArrayTypeSymbol } ? ((IConversionOperation)spanSrc).Operand : spanSrc).Type is IArrayTypeSymbol mat)
        {
            var ma = inv.Arguments.OrderBy(a => a.Parameter.Ordinal).ToList();
            var arrOp = Strip(ma[0].Value) is IConversionOperation mc ? mc.Operand : Strip(ma[0].Value);
            var eqm = EqOf(mat.ElementType);
            if (ma.Count == 3)
            {
                if (mat.ElementType.SpecialType != SpecialType.System_String || Strip(ma[2].Value) is not IPropertyReferenceOperation { Property: { Name: "OrdinalIgnoreCase" or "Ordinal", ContainingType.Name: "StringComparer" } } ccmp)
                    return Unsupported(inv, "MemoryExtensions.Contains with a comparer other than StringComparer.Ordinal / OrdinalIgnoreCase");
                if (ccmp.Property.Name == "OrdinalIgnoreCase") eqm = "[](void* a, void* b) { return gen::StrEqI(a, b); }";
            }
            R.Add("mechanical: MemoryExtensions.Contains over an array -> gen::ArrIndexOf", Where(inv));
            return $"(gen::ArrIndexOf({Expr(arrOp)}, {Expr(ma[1].Value)}, {eqm}) >= 0)";
        }
        // a.SequenceEqual(b) over two arrays that .NET 10 binds to MemoryExtensions.SequenceEqual(ReadOnlySpan<T>, ReadOnlySpan<T>) (TF.State's
        // ExplosionExtensions.HasLayout): LINQ's, element by element
        if (full == "System.MemoryExtensions.SequenceEqual" && inv.Arguments.Length == 2)
        {
            var sa = inv.Arguments.OrderBy(a => a.Parameter.Ordinal).Select(a => Strip(a.Value) is IConversionOperation sc && sc.Operand.Type is IArrayTypeSymbol ? sc.Operand : Strip(a.Value)).ToList();
            if (sa[0].Type is IArrayTypeSymbol sat && sa[1].Type is IArrayTypeSymbol)
            {
                R.Add("mechanical: MemoryExtensions.SequenceEqual over two arrays -> gen::SequenceEqual", Where(inv));
                return $"gen::SequenceEqual<{Cpp(sat.ElementType)}>(gen::FromArr({Expr(sa[0])}), gen::FromArr({Expr(sa[1])}), {EqOf(sat.ElementType)})";
            }
        }
        if (full == "System.Array.IndexOf" && inv.Arguments.Length == 2 && Strip(inv.Arguments[0].Value).Type is IArrayTypeSymbol iat)
        {
            var ia = inv.Arguments.OrderBy(a => a.Parameter.Ordinal).ToList();
            var eqf = EqOf(iat.ElementType);
            return $"gen::ArrIndexOf({Expr(Strip(ia[0].Value))}, {Expr(ia[1].Value)}, {eqf})";
        }
        if (full is "System.BitConverter.Int64BitsToDouble" or "System.BitConverter.DoubleToInt64Bits" && inv.Arguments.Length == 1)   // TF.State's DeterministicMath (Log2, Scale2)
            return $"__builtin_bit_cast({(m.Name == "Int64BitsToDouble" ? "double" : "int64_t")}, ({(m.Name == "Int64BitsToDouble" ? "int64_t" : "double")})({Expr(inv.Arguments[0].Value)}))";
        if (full is "System.BitConverter.Int32BitsToSingle" or "System.BitConverter.SingleToInt32Bits" && inv.Arguments.Length == 1)
            return $"__builtin_bit_cast({(m.Name == "Int32BitsToSingle" ? "float" : "int32_t")}, ({(m.Name == "Int32BitsToSingle" ? "int32_t" : "float")})({Expr(inv.Arguments[0].Value)}))";
        if (full == "Microsoft.Xna.Framework.Rectangle.Contains" && inv.Arguments.Length is 1 or 2 && inv.Arguments.All(a => a.Value.Type?.Name is "Rectangle" or "Int32"))
            return $"{Paren(inv.Instance)}.Contains({string.Join(", ", inv.Arguments.OrderBy(a => a.Parameter.Ordinal).Select(a => Expr(a.Value)))})";
        if (full == "Microsoft.Xna.Framework.Rectangle.Intersects" && inv.Arguments.Length == 1)
            return $"{Paren(inv.Instance)}.Intersects({Expr(inv.Arguments[0].Value)})";
        if (inv.Instance?.Type != null && IsNullable(inv.Instance.Type) && m.Name == "GetValueOrDefault")
            return inv.Arguments.Length == 0 ? $"({Expr(inv.Instance)}).value" : $"([&]() {{ auto n_ = {Expr(inv.Instance)}; return n_.has ? n_.value : {Expr(inv.Arguments[0].Value)}; }}())";
        if (IsDynamicData(owner)) return DynamicDataCall(inv);
        if (owner.ToDisplayString().StartsWith("HarmonyLib.Traverse")) return TraverseCall(inv);
        if (ReflectionFieldCall(inv) is string reflField) return reflField;
        // a game List<T> of a reference type, RemoveAll(lambda): in place (gen::GameListRemoveAll, .NET's algorithm on the list's fields) instead of a
        // Predicate delegate and its closure allocated per call (TF.State's loads call it per tag list per entity type: 40 a call of LoadDummies)
        if (m.Name == "RemoveAll" && owner.OriginalDefinition.ToDisplayString() == "System.Collections.Generic.List<T>" && inv.Arguments.Length == 1 &&
            owner is INamedTypeSymbol { TypeArguments: [{ IsReferenceType: true }] } && LinqMaterialized(inv.Instance) == null && Cpp(inv.Instance.Type) == "void*" &&
            Strip(inv.Arguments[0].Value) is IDelegateCreationOperation { Target: IAnonymousFunctionOperation })
        {
            R.Add("mechanical: game List<T>.RemoveAll(lambda) -> gen::GameListRemoveAll (in place, no delegate)", Where(inv));
            return $"gen::GameListRemoveAll({Expr(inv.Instance)}, {InlineLambda(inv.Arguments[0].Value)})";
        }
        if (m.Name == "ForEach" && owner.OriginalDefinition.ToDisplayString() == "System.Collections.Generic.List<T>" && LinqMaterialized(inv.Instance) != null)
            return $"gen::ForEach({Expr(inv.Instance)}, {InlineLambda(inv.Arguments[0].Value)})";
        if (IsFrOption(owner))
        {
            R.Add("mechanical: FortRise Option<T>: a {Value, HasValue} struct", "Option<T>." + m.Name);
            if (m.Name == "None" && m.IsStatic) return $"{Cpp(owner)}{{}}";
            if (m.Name == "TryGetValue" && !m.IsStatic && inv.Arguments.Length == 1)
            {
                var optRecv = Recv(inv.Instance);
                return $"([&]() {{ auto o_ = {optRecv}; {LValue(inv.Arguments[0].Value)} = o_.Value; return o_.HasValue; }}())";
            }
            return Unsupported(inv, "FortRise Option<T>." + m.Name);
        }
        // a C# 14 extension block member (FortRise.Content's EntryExtensions: `extension(IModSprites sprite) { ... }`): its static implementation
        // in the enclosing class, the receiver first (and a generic one's runtime type arguments, as for any generic method of ours)
        // (one with a [service] rule: the rule, the receiver as $self: FortRise's SpriteFontExt.WrapText over spans)
        if (m.ContainingType is { IsExtension: true } && gen.ServiceRule(m) != null &&
            Service(m, i => i < inv.Arguments.Length ? Expr(inv.Arguments.OrderBy(a => a.Parameter.Ordinal).ElementAt(i).Value) : "", inv, inv.Instance) is string extSvc)
            return extSvc;
        if (m.ContainingType is { IsExtension: true, ContainingType: INamedTypeSymbol extOwner })
        {
            // the member itself, emitted with its enclosing static class's unit (SelectMembers): the receiver first, then the arguments,
            // then a generic one's runtime type arguments (as for any generic method of ours)
            var em = m.OriginalDefinition;
            R.Add("mechanical: an extension block member -> a static function of its class, the receiver first", m.Name);
            var eargs = new List<string>();
            if (inv.Instance != null) eargs.Add(Expr(inv.Instance));
            eargs.AddRange(inv.Arguments.OrderBy(a => a.Parameter.Ordinal).Select(a => Expr(a.Value)));
            methodCallees.Add(em);
            var eu = SymbolEqualityComparer.Default.Equals(extOwner, type) ? null
                : OtherUnit(extOwner) is TomlTable eu0 && !eu0.ContainsKey("auto") && !eu0.ContainsKey("members") ? eu0 : AutoU(extOwner, em);
            if (eu == null && !SymbolEqualityComparer.Default.Equals(extOwner, type)) return Unsupported(inv, "extension member of a type no unit translates");
            var ename = MName(em);
            // a generic one copied per instantiation (CopiesGeneric: FortRise's GetSpriteEntryWithRelative<T>): its copy, no runtime types
            if (gen.CopiesGeneric(em) && m.TypeArguments.Select(Sub).ToArray() is var ecta && ecta.All(a => a is not ITypeParameterSymbol && !CodeGen.HasTypeParam(a)))
            {
                gen.RequestMethodInst(eu ?? unit, em, ecta);
                ename += MethodInstSuffix(ecta);
            }
            else foreach (var ta in m.TypeArguments) eargs.Add(TypeOfAny(ta));
            if (eu == null) return $"{ename}({string.Join(", ", eargs)})";
            includes.Add($"tfexgen_{eu["name"]}.hpp");
            return $"tfex::gen::{eu["name"]}::{ename}({string.Join(", ", eargs)})";
        }
        if (LogTemplate(inv) is string logged) return logged;
        if (gen.ServiceRule(m) != null)
        {
            var argOps = inv.Arguments.OrderBy(a => a.Parameter.Ordinal).ToList();
            if (Service(m, i => i < argOps.Count ? Expr(argOps[i].Value) : "", inv, inv.Instance) is string svc) return svc;
        }
        // a P/Invoke extern ([DllImport("lib")] static extern, TF.EX's GGRSFFI) with no [service] rule: a direct C call to the native library
        // linked into the module (ggrs_ffi on the Switch): structs by value as the C# signature has them (st:: PODs match #[repr(C)]),
        // ref / out as pointers, string as a UTF-8 char* (LPUTF8Str), bool as bool
        if (m.IsExtern && m.GetDllImportData() is { } dll)
            return ExternCall(m, dll, inv);
        // the unit's own member that translate.toml excludes (kept native): reached only through a [service] rule (NetplayManager's xplay group)
        if (SymbolEqualityComparer.Default.Equals(owner.OriginalDefinition, type) && Config.Strings(unit, "exclude").Contains(m.Name))
            return Unsupported(inv, "call to an excluded member without a [service] rule: " + m.Name);
        bool gameCallee = InBinary(owner) || OriginOf(owner) == Origin.Bcl;
        // string.Concat's pieces as strings (StringOf: a span piece, s.AsSpan(n), is its substring), before the arguments are translated as
        // themselves below: that would also translate the AsSpan call and leave its unbindable MemoryExtensions member as the method's blocker
        // n.ToString("0.00", CultureInfo.InvariantCulture) (PlayerProfilePanel's records): .NET's custom format "0" / "0.0" / "0.00"..., fixed
        // decimals, midpoints away from zero; the provider (invariant: '.') isn't translated
        if (m.Name == "ToString" && !m.IsStatic && inv.Instance?.Type?.SpecialType is SpecialType.System_Double or SpecialType.System_Single or SpecialType.System_Int32 &&
            inv.Arguments.Length is 1 or 2 && inv.Arguments.OrderBy(a => a.Parameter.Ordinal).First().Value.ConstantValue is { HasValue: true, Value: string fmt } &&
            System.Text.RegularExpressions.Regex.IsMatch(fmt, @"^0(\.0+)?$") &&
            (inv.Arguments.Length == 1 || inv.Arguments.OrderBy(a => a.Parameter.Ordinal).Last().Value.Type?.ToDisplayString() is "System.IFormatProvider" or "System.Globalization.CultureInfo"))
        {
            R.Add("mechanical: a number's ToString(\"0.0\"-style format, invariant) -> gen::FormatFixed", Where(inv));
            return $"gen::FormatFixed((double)({Expr(inv.Instance)}), {(fmt.Length > 1 ? fmt.Length - 2 : 0)})";
        }
        // CultureInfo.InvariantCulture.DateTimeFormat.GetAbbreviatedMonthName(m): "Jan".."Dec"
        if (m.Name == "GetAbbreviatedMonthName" && m.ContainingType.ToDisplayString() == "System.Globalization.DateTimeFormatInfo" && inv.Arguments.Length == 1 &&
            inv.Instance is IPropertyReferenceOperation { Property.Name: "DateTimeFormat", Instance: IPropertyReferenceOperation { Property.Name: "InvariantCulture" } })
            return $"gen::MonthAbbrev({Expr(inv.Arguments[0].Value)})";
        if (m.ContainingType.SpecialType == SpecialType.System_String && m.Name == "Concat" && m.IsStatic && inv.Arguments.Length is 2 or 3)
            return $"gen::Concat({string.Join(", ", inv.Arguments.OrderBy(a => a.Parameter.Ordinal).Select(a => StringOf(a.Value)))})";
        var args = inv.Arguments.OrderBy(a => a.Parameter.Ordinal).Select(a => a.Parameter.RefKind != RefKind.None && (gameCallee || a.Parameter.RefKind != RefKind.In) ? (gameCallee ? "&" : "") + LValue(a.Value) :   // `in` of ours: by value
            gameCallee && Strip(a.Value).Type is IArrayTypeSymbol && a.Parameter.Type is not IArrayTypeSymbol && !(a.Parameter.Type is INamedTypeSymbol { Name: "ReadOnlySpan" } && HasRewrite(inv)) ? $"({Expr(Strip(a.Value))}).obj" : Expr(a.Value)).ToList();   // our array to a game System.Array / object parameter (a span to a [[rewrite]]: the array)
        if (CppAttr(m, args) is string cppx) { R.Add("hand rule: glue extern [Cpp] (Switch code)", m.ToDisplayString()); return cppx; }
        if (Rewrite(inv, args) is string rw) return rw;
        if (m.Name == "GetHashCode" && m.Parameters.Length == 0 && inv.Instance != null && Strip(inv.Instance).Type?.SpecialType == SpecialType.System_Double)
        {   // double.GetHashCode() (TF.State's BrambleArrowPatch: the spread's id from actualDepth): .NET's body, as TF.EX runs it (gen::DoubleHash)
            R.Add("mechanical: double.GetHashCode() -> .NET's body (gen::DoubleHash)", Where(inv));
            includes.Add("tfexgen_code_rt.hpp");
            return $"gen::DoubleHash({Expr(Strip(inv.Instance))})";
        }
        if (MathMap.TryGetValue(full, out var fn))
        {
            if (fn == "gen_abs" && m.Parameters[0].Type.SpecialType is SpecialType.System_Double or SpecialType.System_Single)
            {   // .NET's Math.Abs(double / float) clears the sign bit (Abs(-0.0) = +0.0, NaN's sign too): fabs, as DeterministicMath needs
                R.Add("mechanical: Math.Abs(floating) -> fabs (the sign bit cleared, as .NET)", Where(inv));
                return $"{(m.Parameters[0].Type.SpecialType == SpecialType.System_Single ? "__builtin_fabsf" : "__builtin_fabs")}({args[0]})";
            }
            if (fn.StartsWith("gen_"))
            {
                includes.Add("tfexgen_code_rt.hpp");
                return fn switch
                {
                    "gen_abs" => $"({args[0]} < 0 ? -({args[0]}) : ({args[0]}))",
                    "gen_max" => $"({args[0]} > {args[1]} ? {args[0]} : {args[1]})",   // C# Math.Max(float): NaN handling aside, same pick
                    "gen_min" => $"({args[0]} < {args[1]} ? {args[0]} : {args[1]})",
                    _ => $"(({args[0]}) > 0 ? 1 : ({args[0]}) < 0 ? -1 : 0)",
                };
            }
            return $"{fn}({string.Join(", ", args)})";
        }
        switch (full)
        {
            case "string.IsNullOrEmpty": return $"gen::IsNullOrEmpty({args[0]})";
            case "string.Concat" when args.Count is 2 or 3: return $"gen::Concat({string.Join(", ", inv.Arguments.OrderBy(a => a.Parameter.Ordinal).Select(a => StringOf(a.Value)))})";
            case "Microsoft.Xna.Framework.Vector2.Lerp": return $"gen::Vector2::Lerp({string.Join(", ", args)})";
            case "Microsoft.Xna.Framework.Matrix.Lerp": return $"gen::Matrix_Lerp({string.Join(", ", args)})";   // (FNA's body: m1 + (m2 - m1) * amount per element)
            case "Microsoft.Xna.Framework.Color.Lerp": return $"gen::Color_Lerp({string.Join(", ", args)})";
            case "Microsoft.Xna.Framework.MathHelper.Lerp": return $"gen::MathHelper_Lerp({string.Join(", ", args)})";
            case "Microsoft.Xna.Framework.MathHelper.Clamp": return $"gen::MathHelper_Clamp({string.Join(", ", args)})";
            case "System.Math.Clamp": case "System.MathF.Clamp": return $"gen::Clamp({string.Join(", ", args)})";
            case "Microsoft.Xna.Framework.Vector2.Normalize" when m.IsStatic && args.Count == 1: return $"gen::Vector2::Normalize({args[0]})";   // FNA's static Normalize(v)
            case "Microsoft.Xna.Framework.Vector2.Normalize" when !m.IsStatic && args.Count == 0:   // FNA's v.Normalize(): v in place
                return $"({Expr(inv.Instance)} = gen::Vector2::Normalize({Expr(inv.Instance)}))";
            case "Microsoft.Xna.Framework.Vector2.Dot" when m.IsStatic && args.Count == 2: return $"gen::Vector2::Dot({args[0]}, {args[1]})";
            case "System.Array.GetLength" when inv.Instance?.Type is IArrayTypeSymbol { Rank: 2 } && args.Count == 1:   // a T[,]: gen::Grid's own bounds
                return $"{Paren(inv.Instance)}.GetLength({args[0]})";
            case "Microsoft.Xna.Framework.Point.Equals" when !m.IsStatic && args.Count == 1 && m.Parameters[0].Type.ToDisplayString() == "Microsoft.Xna.Framework.Point":
                return $"({Paren(inv.Instance)} == {args[0]})";   // FNA's Point.Equals(Point): X and Y equal
            case "Microsoft.Xna.Framework.Vector2.Length": return $"{Expr(inv.Instance)}.Length()";
            case "Microsoft.Xna.Framework.Vector2.LengthSquared": return $"{Paren(inv.Instance)}.LengthSquared()";
        }
        var o = OriginOf(owner);
        if (o == Origin.Tfex) methodCallees.Add(m.OriginalDefinition);
        if (o == Origin.Tfex)
        {
            // a parameter typed as a lobby model list (ModelListParamCpp) given anything else ([title], a sequence): a temporary list of it
            foreach (var a in inv.Arguments)
                if (a.Parameter != null && ModelListParamCpp(a.Parameter.OriginalDefinition) is string mlp && !IsModelList(a.Value) && a.Parameter.Ordinal < args.Count)
                {
                    var v = Strip(a.Value);
                    while (v is IConversionOperation cv) v = cv.Operand;
                    var seq = v is ICollectionExpressionOperation ce && !ce.Elements.Any(e => e is ISpreadOperation)
                        ? $"gen::MakeSeq([&](auto&& f_) {{ return {(ce.Elements.Length == 0 ? "true" : string.Join(" && ", ce.Elements.Select(e => $"f_({Expr(e)})")))}; }})"
                        : SeqOf(v);
                    R.Add("rule: a collection passed for a model-list parameter -> a temporary model list of it", Where(a));
                    args[a.Parameter.Ordinal] = $"gen::TempModelList<{mlp.TrimEnd('*')}>({seq})";
                }
                else if (a.Parameter != null && ModelListParamCpp(a.Parameter.OriginalDefinition) is string mlp2 && a.Parameter.Ordinal < args.Count &&
                         !(Strip(a.Value) is IPropertyReferenceOperation { Property: var mp2 } && !mp2.IsStatic && gen.Models.IsModel(mp2.ContainingType) &&
                           gen.Models.MemberOf(mp2.ContainingType, mp2.Name) is { } mm2 && "tfex::gen::" + mm2.CppType + "*" == mlp2))
                {   // a model list that isn't a model's own member of the parameter's type (Lobby.SessionPlayers, a computed List<Player, 4>, for
                    // MatchReport.Begin's IEnumerable<Player>, List<Player, 16>): the same list when the C++ types agree, else a copy
                    R.Add("rule: a model list passed for a model-list parameter of another capacity -> a copy of it", Where(a));
                    args[a.Parameter.Ordinal] = $"gen::ModelListAs<{mlp2.TrimEnd('*')}>({args[a.Parameter.Ordinal]})";
                }
            // TF.State's IEnumerable<int / Vector2> parameter (RoundLogicPatch.TopUpSpawns, ApplyOrder, CalcExtensions.OwnShuffle): a game
            // List<T> (C#'s are backed by Lists there); an argument that isn't one (a LINQ chain, Enumerable.Range) copied into a new List
            foreach (var a in inv.Arguments)
                if (a.Parameter != null && StateEnumListOf(a.Parameter) is INamedTypeSymbol plt && a.Parameter.Ordinal < args.Count)
                {
                    var v = Strip(a.Value);
                    while (v is IConversionOperation cv) v = cv.Operand;
                    if (v.Type is INamedTypeSymbol { OriginalDefinition.MetadataName: "List`1" } || v.ConstantValue is { HasValue: true, Value: null } || v.Type == null) continue;
                    if (v is IParameterReferenceOperation { Parameter: var vp } && StateEnumListOf(vp.OriginalDefinition) != null) continue;   // already a List
                    var ctor = plt.InstanceConstructors.First(c => c.Parameters.Length == 0);
                    var add = plt.GetMembers("Add").OfType<IMethodSymbol>().First();
                    R.Add("rule: a sequence passed for TF.State's IEnumerable<T> parameter -> a new game List<T> of it", Where(a));
                    args[a.Parameter.Ordinal] = $"([&]() {{ void* l_ = {Call(ctor, "nullptr", new List<string>(), inv)}; {SeqOf(v)}([&]({Cpp(plt.TypeArguments[0])} x_) {{ {Call(add, "l_", new List<string> { "x_" }, inv)}; return true; }}); return l_; }}())";
                }
            // a value-list parameter (VList: List / IEnumerable of a TF.EX struct) given a sequence or an array: copied into a new one
            foreach (var a in inv.Arguments)
                if (a.Parameter != null && a.Parameter.Ordinal < args.Count && ValKind(a.Parameter.OriginalDefinition.Type) == VKind.List &&
                    (!IsValColl(Strip(a.Value).Type) || Strip(a.Value) is IInvocationOperation { TargetMethod: var lm } && IsEnumerable(lm)) &&
                    Strip(a.Value).Type is { } at0 && at0.SpecialType != SpecialType.System_Object)
                {
                    var el = ((INamedTypeSymbol)a.Parameter.Type).TypeArguments[0];
                    R.Add("mechanical: a sequence for a value-list parameter -> a new VList of it", Where(a));
                    // `x.ToList()` (NetplayManager.AdvanceGameState: UpdateCurrent(inputs.ToList())): copied here anyway, so x's elements
                    var src = Strip(a.Value);
                    if (src is IInvocationOperation { TargetMethod.Name: "ToList" or "ToArray" } tli && IsEnumerable(tli.TargetMethod) && tli.Arguments.Length == 1)
                        src = Strip(tli.Arguments[0].Value);
                    args[a.Parameter.Ordinal] = $"([&]() {{ auto* v_ = gen::{NewValFn()}<{ValCpp(a.Parameter.Type)}>(); {SeqOf(src)}([&]({Cpp(el)} x_) {{ v_->Add(x_); return true; }}); return v_; }}())";
                }
            // an IEnumerable<primitive> parameter of ours (IsPrimSeqParam) given a sequence: copied into a new value list
            foreach (var a in inv.Arguments)
                if (a.Parameter != null && a.Parameter.Ordinal < args.Count && IsPrimSeqParam(a.Parameter.OriginalDefinition) &&
                    !(Strip(a.Value) is IParameterReferenceOperation { Parameter: var pp } && IsPrimSeqParam(pp)) && a.Value.ConstantValue is not { HasValue: true, Value: null })
                {
                    var el = ((INamedTypeSymbol)a.Parameter.Type).TypeArguments[0];
                    R.Add("mechanical: a sequence for an IEnumerable<primitive> parameter -> a new VList of it", Where(a));
                    args[a.Parameter.Ordinal] = $"([&]() {{ auto* v_ = gen::{NewValFn()}<{PrimSeqCpp(a.Parameter.OriginalDefinition)}>(); {SeqOf(Strip(a.Value))}([&]({Cpp(el)} x_) {{ v_->Add(x_); return true; }}); return v_; }}())";
                }
            // a TF.State generic method with concrete type arguments (SpriteExtensions.GetState<int>): its copy for them (the unit emits it)
            if (gen.CopiesGeneric(m) && owner.TypeKind != TypeKind.Interface &&
                m.TypeArguments.Select(Sub).ToArray() is var cta && cta.All(a => a is not ITypeParameterSymbol && !CodeGen.HasTypeParam(a)))
            {   // (FortRise's instance ones: the receiver first, as any instance call)
                var iu = SymbolEqualityComparer.Default.Equals(owner.OriginalDefinition, type.OriginalDefinition) ? unit : (OtherUnit(owner) ?? AutoU(owner, m));
                if (iu != null)
                {
                    gen.RequestMethodInst(iu, m.OriginalDefinition, cta);
                    methodCallees.Add(m.OriginalDefinition);
                    var iname = MName(m) + MethodInstSuffix(cta);
                    var cargs = m.IsStatic ? args : new[] { Recv(inv.Instance) }.Concat(args).ToList();
                    if (ReferenceEquals(iu, unit)) return $"{iname}({string.Join(", ", cargs)})";
                    includes.Add($"tfexgen_{iu["name"]}.hpp");
                    return $"tfex::gen::{iu["name"]}::{iname}({string.Join(", ", cargs)})";
                }
            }
            foreach (var ta in m.TypeArguments) args.Add(TypeOfAny(ta));   // generic over a class: the type argument's runtime type
            if (SymbolEqualityComparer.Default.Equals(owner, type) && m.IsStatic) { methodCallees.Add(m.OriginalDefinition); return $"{MName(m)}({string.Join(", ", args)})"; }   // (a unit of some members pulls it in)
            if (IsPodS(owner) && !m.IsStatic)   // a TF.EX struct's method: a function of the value (its unit)
            {
                var r = Recv(inv.Instance);
                if (SymbolEqualityComparer.Default.Equals(owner, type)) return $"{MName(m)}({string.Join(", ", new[] { r }.Concat(args))})";
                var su = OtherUnit(owner) is TomlTable su1 && !su1.ContainsKey("auto") && !su1.ContainsKey("members") ? su1 : AutoU(owner, m);
                if (su == null) return Unsupported(inv, "struct method " + full);
                includes.Add($"tfexgen_{su["name"]}.hpp");
                return $"tfex::gen::{su["name"]}::{MName(m)}({string.Join(", ", new[] { r }.Concat(args))})";
            }
            if (gen.Models.IsModel(owner) && !m.IsStatic && !SymbolEqualityComparer.Default.Equals(owner, type))
            {
                var mu = OtherUnit(owner) is TomlTable mu1 && !mu1.ContainsKey("auto") && !mu1.ContainsKey("members") ? mu1 : AutoU(owner, m);
                includes.Add($"tfexgen_{mu["name"]}.hpp");
                var r = Recv(inv.Instance);
                return $"tfex::gen::{mu["name"]}::{MName(m)}({string.Join(", ", new[] { r }.Concat(args))})";
            }
            if (!m.IsStatic && IsClassType(owner))
            {
                var recvS = Recv(inv.Instance);
                bool viaBase = inv.Instance?.Syntax is Microsoft.CodeAnalysis.CSharp.Syntax.BaseExpressionSyntax;
                // virtual: dispatch through the object's vtable (the slot its declaring class of ours gave it)
                if ((m.IsVirtual || m.IsAbstract || m.IsOverride) && !viaBase)
                {
                    var root = m; while (root.IsOverride && root.OverriddenMethod != null && OriginOf(root.OverriddenMethod.ContainingType) == Origin.Tfex) root = root.OverriddenMethod;
                    if (OriginOf(root.ContainingType) == Origin.Tfex && !root.IsOverride)
                        return $"gen::Virt<{Cpp(m.ReturnType)}>({string.Join(", ", new[] { recvS, $"{BaseUnit(root.ContainingType)}::kSlot_{root.Name}" }.Concat(args))})";
                    if (root.IsOverride && root.OverriddenMethod != null && InBinary(root.OverriddenMethod.ContainingType))
                        return Call(root.OverriddenMethod, recvS, args, inv);   // a game virtual we override: the game's vtable call
                }
                if (SymbolEqualityComparer.Default.Equals(owner, type)) return $"{MName(m)}({string.Join(", ", new[] { recvS }.Concat(args))})";
                var ou2 = OtherUnit(owner) is TomlTable ou3 && !ou3.ContainsKey("auto") && !ou3.ContainsKey("members") ? ou3 : AutoU(owner, m);
                includes.Add($"tfexgen_{ou2["name"]}.hpp");
                return $"tfex::gen::{ou2["name"]}::{MName(m)}({string.Join(", ", new[] { recvS }.Concat(args))})";
            }
            var other = OtherUnit(owner) is TomlTable ou0 && !ou0.ContainsKey("auto") && !ou0.ContainsKey("members") ? ou0 : (m.IsStatic ? AutoU(owner, m) : null);
            if (other != null && m.IsStatic)
            {
                var un = (string)other["name"];
                includes.Add($"tfexgen_{un}.hpp");
                return $"tfex::gen::{un}::{MName(m)}({string.Join(", ", args)})";
            }
            // a TF.EX interface with one implementation in TF.EX's code and no [service] rule: that class's generated method
            if (!m.IsStatic && owner.TypeKind == TypeKind.Interface && gen.ImplsOf(owner) is { Count: 1 } one &&
                gen.ImplMember(one[0], m) is IMethodSymbol impl && SymbolEqualityComparer.Default.Equals(impl.ContainingType, one[0]))
            {
                var iu = SymbolEqualityComparer.Default.Equals(one[0], type) ? null : OtherUnit(one[0]) is TomlTable iu0 && !iu0.ContainsKey("auto") && !iu0.ContainsKey("members") ? iu0 : AutoU(one[0], impl);
                var recvI = Recv(inv.Instance);
                methodCallees.Add(impl.OriginalDefinition);
                R.Add("mechanical: a call through a TF.EX interface -> its single TF.EX implementation (no [service] rule)", $"{owner.Name}.{m.Name} -> {one[0].Name}");
                // a generic method copied per instantiation (CopiesGeneric): the implementation's copy for the call's type arguments
                if (impl is IMethodSymbol { IsGenericMethod: true } gimpl && gen.CopiesGeneric(gimpl) &&
                    m.TypeArguments.Select(Sub).ToArray() is var icta && icta.Length == gimpl.TypeParameters.Length && icta.All(a => a is not ITypeParameterSymbol && !CodeGen.HasTypeParam(a)) &&
                    (iu ?? unit) is TomlTable giu)
                {
                    gen.RequestMethodInst(giu, gimpl.OriginalDefinition, icta);
                    var gargs = args.Take(args.Count - m.TypeArguments.Length).ToList();   // (no runtime type arguments: the copy has its own)
                    var gname = MName(gimpl) + MethodInstSuffix(icta);
                    if (iu == null) return $"{gname}({string.Join(", ", new[] { recvI }.Concat(gargs))})";
                    includes.Add($"tfexgen_{iu["name"]}.hpp");
                    return $"tfex::gen::{iu["name"]}::{gname}({string.Join(", ", new[] { recvI }.Concat(gargs))})";
                }
                if (iu == null) return $"{MName(impl)}({string.Join(", ", new[] { recvI }.Concat(ArraysAsObjects(inv, args)))})";   /* (MName: the overload's own name) */
                includes.Add($"tfexgen_{iu["name"]}.hpp");
                return $"tfex::gen::{iu["name"]}::{MName(impl)}({string.Join(", ", new[] { recvI }.Concat(ArraysAsObjects(inv, args)))})";
            }
            // ... several classes of ours (code mods' implementations of another mod's interface): dispatch on the object's class
            if (!m.IsStatic && owner.TypeKind == TypeKind.Interface && gen.ImplsOf(owner) is { Count: > 1 } manyM &&
                MultiDispatch(manyM, m, Recv(inv.Instance), args, m.ReturnType) is string mdm)
                return mdm;
            // a mod's interop interface ([[interop]]: TF.EX's ITfStateApi over TF.State's TfStateApi): the provider class's generated member
            if (!m.IsStatic && owner.TypeKind == TypeKind.Interface && gen.InteropImpl(owner, m) is IMethodSymbol xi &&
                (OtherUnit(xi.ContainingType) is TomlTable xu0 && !xu0.ContainsKey("auto") && !xu0.ContainsKey("members") ? xu0 : AutoU(xi.ContainingType, xi)) is TomlTable xu)
            {
                var recvX = Recv(inv.Instance);
                methodCallees.Add(xi.OriginalDefinition);
                R.Add("mechanical: a call through a mod's interop interface -> the provider mod's class ([[interop]])", $"{owner.Name}.{m.Name} -> {xi.ContainingType.Name}");
                includes.Add($"tfexgen_{xu["name"]}.hpp");
                return $"tfex::gen::{xu["name"]}::{Id(xi.Name)}({string.Join(", ", new[] { recvX }.Concat(ArraysAsObjects(inv, args)))})";
            }
            return Unsupported(inv, "call to a TF.EX type no unit translates: " + full);
        }
        if (full is "Microsoft.Xna.Framework.Vector2.DistanceSquared" or "Microsoft.Xna.Framework.Vector2.Distance" && m.IsStatic && inv.Arguments.Length == 2)
        {   // FNA's Vector2.DistanceSquared / Distance (static, by value): the same arithmetic
            var va = inv.Arguments.OrderBy(a => a.Parameter.Ordinal).Select(a => Expr(a.Value)).ToList();
            var sq = $"([&]() {{ auto a_ = {va[0]}; auto b_ = {va[1]}; float dx = a_.X - b_.X, dy = a_.Y - b_.Y; return dx * dx + dy * dy; }}())";
            return m.Name == "Distance" ? $"__builtin_sqrtf({sq})" : sq;
        }
        if (o == Origin.Fna && FnaValueTypes.ContainsKey(owner.ToDisplayString())) return Unsupported(inv, "FNA value-type method " + full);
        // d.Invoke(...) / d(...) on a delegate: Invoke is every delegate class's vtable slot 0x58 (docs/ws-tfex-subclass.md row 7), also for
        // instantiations the game never compiled (Func<Subtexture>, Func<an interface of ours>)
        // a Harmony field ref made by reflection (TF.State's TweenPatch: `static FieldRef<Stack<Tween>> Cached = AccessTools.StaticFieldRefAccess
        // <..>(AccessTools.Field(typeof(Tween), "cached"))`; FieldRef<T, F> = AccessTools.FieldRefAccess<T, F>(name)), invoked: the field
        // itself, read where it is (the Switch has no reflection-made delegates)
        if (owner.TypeKind == TypeKind.Delegate && m.Name == "Invoke" && owner.ContainingType?.ToDisplayString() == "HarmonyLib.AccessTools" && owner.Name is "FieldRef" &&
            Strip(inv.Instance) is IFieldReferenceOperation { Field: var frf } && FieldInit(frf) is IInvocationOperation fri && RefField(fri) is IFieldSymbol gf)
        {
            R.Add("mechanical: a Harmony FieldRef (AccessTools.(Static)FieldRefAccess) invoked -> the game field", $"{gf.ContainingType.Name}.{gf.Name}");
            // a game array field is the managed array (one pointer): wrapped as a gen::Arr, as a plain field read is (ArrowPatch's
            // Graphics: read as a gen::Arr by value, its "length" was the next field, and DoWrapRender asked for a 2.9 GB float[])
            if (gf.IsStatic) return GameArray(gf.Type, $"(gen::Cctor({B.Need("c", SwitchName(gf.ContainingType))}), gen::Static<void*>({B.Need("f", SwitchName(gf.ContainingType), gf.Name)}))")
                                    ?? $"(gen::Cctor({B.Need("c", SwitchName(gf.ContainingType))}), gen::Static<{Cpp(gf.Type)}>({B.Need("f", SwitchName(gf.ContainingType), gf.Name)}))";
            if (args.Count != 1) return Unsupported(inv, "FieldRef invoke");
            return GameArray(gf.Type, $"gen::Fld<void*>({args[0]}, {B.Need("f", SwitchName(gf.ContainingType), gf.Name)})")
                   ?? $"gen::Fld<{Cpp(gf.Type)}>({args[0]}, {B.Need("f", SwitchName(gf.ContainingType), gf.Name)})";
        }
        if (owner.TypeKind == TypeKind.Delegate && m.Name == "Invoke" && inv.Arguments.Length == 1 && Strip(inv.Instance) is IFieldReferenceOperation { Field: var rsf } &&
            ReflectedSetterField(rsf) is IPropertySymbol { IsStatic: true } rsp)
            return SetterInvoke(rsp, rsf, inv.Arguments[0].Value, inv);
        if (owner.TypeKind == TypeKind.Delegate && m.Name == "Invoke" && inv.Arguments.Length == 2 && Strip(inv.Instance) is IFieldReferenceOperation { Field: var rif } &&
            ReflectedSetterField(rif) is IPropertySymbol { IsStatic: false } rip)
        {   // an Action<T, V> made over an instance property's setter: target.Prop = value
            var ia2 = inv.Arguments.OrderBy(a => a.Parameter.Ordinal).ToList();
            R.Add("mechanical: a delegate made by reflection over an instance property setter, invoked -> the setter's call", $"{rif.ContainingType.Name}.{rif.Name}: {rip.ContainingType.Name}.{rip.Name}");
            return Call(rip.SetMethod, Expr(ia2[0].Value), new List<string> { Expr(ia2[1].Value) }, inv);
        }
        if (owner.TypeKind == TypeKind.Delegate && m.Name == "Invoke" && inv.Instance != null && DelegateDirect(owner) && m.ReturnType is not IArrayTypeSymbol &&
            !m.Parameters.Any(p => p.Type is IArrayTypeSymbol))
        {   // a delegate type the game never compiled (UnitEmitter.Class DelegateTypeKey: made as a stand-in): its function called directly
            R.Add("mechanical: invoking a delegate of a type the game never compiled -> its function directly", Where(inv));
            return $"gen::DlgInvoke<{string.Join(", ", new[] { Cpp(m.ReturnType) }.Concat(m.Parameters.Select(p => Cpp(p.Type))))}>({string.Join(", ", new[] { Expr(inv.Instance) }.Concat(args))})";
        }
        if (owner.TypeKind == TypeKind.Delegate && m.Name == "Invoke" && inv.Instance != null && !DelegateInvokeBound(m))
        {
            R.Add("mechanical: delegate Invoke -> vtable slot 0x58", Where(inv));
            // arrays cross a delegate as managed arrays (UnitEmitter.Class ManagedAbi): passed as the Arr's managed array, a returned one
            // wrapped back into an Arr
            var vargs = args.Select((a, i) => i < m.Parameters.Length && m.Parameters[i].Type is IArrayTypeSymbol pa ? $"gen::ManagedOf({a}, {B.Need("t", SwitchName(pa))})" : a).ToList();
            if (m.ReturnType is IArrayTypeSymbol vra)
                return $"gen::GameArr<{Cpp(vra.ElementType)}>(gen::Virt<void*>({string.Join(", ", new[] { Expr(inv.Instance), "0x58" }.Concat(vargs))}), {B.Need("m", "System.Array", "get_Length", "", 0, 1)})";
            return $"gen::Virt<{Cpp(m.ReturnType)}>({string.Join(", ", new[] { Expr(inv.Instance), "0x58" }.Concat(vargs))})";
        }
        var recv = m.IsStatic ? null : (inv.Instance is IInstanceReferenceOperation ir && ir.ReferenceKind == InstanceReferenceKind.ContainingTypeInstance ? "self" : Expr(inv.Instance));
        bool baseCall = inv.Instance?.Syntax is Microsoft.CodeAnalysis.CSharp.Syntax.BaseExpressionSyntax;
        return Call(m, recv, args, inv, baseCall);
    }

    /* the delegate instantiation's own Invoke body binds (Action, Func<bool> ...): called directly as before; only instantiations the game
     * never compiled go through the vtable (the binder resolves between the two passes: pass 1 asks, pass 2 knows) */
/* a call through an interface several classes of ours implement (code mods' classes over another mod's copy of the interface: LoaderAI's
     * IAgentLogic, AIJimmy's AILogic and AINova's NovaLogic): C#'s interface dispatch, as a test of the object's class against each (their
     * RuntimeType), the matching class's generated member called; none matching: the default (no such object exists). Null when one of
     * them has no unit or no member for it */
    string MultiDispatch(List<INamedTypeSymbol> impls, ISymbol member, string recv, List<string> args, ITypeSymbol ret)
    {
        var sb = new StringBuilder();
        var rt = ret.SpecialType == SpecialType.System_Void ? "void" : Cpp(ret);
        sb.Append($"([&]() -> {rt} {{ void* r_ = {recv}; ");
        foreach (var c in impls)
        {
            if (!IsClassType(c) || gen.ImplMember(c, member) is not ISymbol im) return null;
            var fn = im is IPropertySymbol ip ? ip.GetMethod : im as IMethodSymbol;
            if (fn == null) return null;
            bool own = SymbolEqualityComparer.Default.Equals(c, type);
            var u = own ? null : OtherUnit(c) is TomlTable u0 && !u0.ContainsKey("auto") && !u0.ContainsKey("members") ? u0 : AutoU(c, fn);
            if (u == null && !own) return null;
            methodCallees.Add(fn.OriginalDefinition);
            var un = u == null ? "" : $"tfex::gen::{u["name"]}::";
            if (u != null) includes.Add($"tfexgen_{u["name"]}.hpp");
            var call = $"{un}{Id(fn.Name)}({string.Join(", ", new[] { "r_" }.Concat(args))})";
            sb.Append($"if (gen::As(r_, {un}RuntimeType())) {{ {(rt == "void" ? call + "; return;" : "return " + call + ";")} }} ");
        }
        sb.Append(rt == "void" ? "}())" : "return {}; }())");
        R.Add("mechanical: a call through an interface several classes of ours implement -> dispatch on the object's class", $"{member.ContainingType.Name}.{member.Name}: {string.Join(", ", impls.Select(i => i.Name))}");
        return sb.ToString();
    }
/* an array given for a collection parameter of another class of ours (AINova's NovaLogic[] for LoaderAI's IList<IAgentLogic>, through the
     * interface): the managed array, as the callee reads an IList (a List or an array) from the object */
    List<string> ArraysAsObjects(IInvocationOperation inv, List<string> args)
    {
        var ord = inv.Arguments.OrderBy(a => a.Parameter.Ordinal).ToList();
        static IOperation Bare(IOperation o) { while (o is IConversionOperation c) o = c.Operand; return o; }
        return args.Select((a, i) => i < ord.Count && Bare(ord[i].Value).Type is IArrayTypeSymbol at && ord[i].Parameter.Type is not IArrayTypeSymbol
            ? $"gen::ManagedOf({a}, {B.Need("t", SwitchName(at))})" : a).ToList();
    }
    bool DelegateInvokeBound(IMethodSymbol m)
    {
        if (m.ContainingType.TypeArguments.Any(a => OriginOf(a) == Origin.Tfex)) return false;
        var (k, count, kp, cp) = order.Of4(m);
        int n0 = methodKeys.Count;
        bool bound = Bound(B.Need("m", SwitchName(m.ContainingType), m.Name, IlSig(m), k, count, kp, cp));
        if (!bound && methodKeys.Count > n0) methodKeys.RemoveRange(n0, methodKeys.Count - n0);   // a probe: the vtable call is used instead
        return bound;
    }

    /* overrides.toml [service] rule for a member: cpp (with $0.. = the arguments) or value; null if none */
    string ExternCall(IMethodSymbol m, DllImportData dll, IInvocationOperation inv)
    {
        var entry = dll.EntryPointName ?? m.Name;
        // an array of a blittable TF.EX struct (GGRSFFI.netplay_set_local_inputs(Input[], int)): marshalled as a pointer to its elements, as
        // .NET pins a blittable array for the call
        bool PodArr(ITypeSymbol t) => t is IArrayTypeSymbol { Rank: 1 } pa && IsPodS(pa.ElementType);
        string PType(IParameterSymbol p) => p.Type.SpecialType == SpecialType.System_String ? "const char*" : PodArr(p.Type) ? $"const {Cpp(((IArrayTypeSymbol)p.Type).ElementType)}*" : Cpp(p.Type) + (p.RefKind != RefKind.None ? "*" : "");
        var ret = m.ReturnsVoid ? "void" : m.ReturnType.SpecialType == SpecialType.System_String ? "const char*" : Cpp(m.ReturnType);
        if (m.Parameters.Any(p => (p.Type is IArrayTypeSymbol && !PodArr(p.Type)) || (p.Type.TypeKind == TypeKind.Class && p.Type.SpecialType != SpecialType.System_String)) ||
            (!m.ReturnsVoid && m.ReturnType.TypeKind == TypeKind.Class && m.ReturnType.SpecialType != SpecialType.System_String))
            return Unsupported(inv, "P/Invoke with a managed array / class parameter or result: " + m.ToDisplayString());
        externDecls.Add($"extern \"C\" {ret} {entry}({string.Join(", ", m.Parameters.Select(PType))});");
        R.Add("mechanical: a P/Invoke extern -> a direct C call into the native library", m.ContainingType.Name + "." + m.Name);
        var pre = new List<string>(); var args = new List<string>();
        foreach (var a in inv.Arguments.OrderBy(a => a.Parameter.Ordinal))
        {
            if (a.Parameter.RefKind != RefKind.None) { args.Add("&" + LValue(a.Value is IDeclarationExpressionOperation de ? de.Expression : a.Value)); continue; }
            if (a.Parameter.Type.SpecialType == SpecialType.System_String)
            {   // a managed string -> a UTF-8 copy for the call (the callee copies what it keeps)
                var v = $"u8_{tmp++}";
                pre.Add($"char {v}[512]; if (void* s_ = {Expr(a.Value)}) gen::Utf8Of(s_, {v}, sizeof {v}); else {v}[0] = 0;");
                args.Add(v); continue;
            }
            if (PodArr(a.Parameter.Type)) { args.Add($"({Expr(a.Value)}).data"); continue; }   // (an empty one: null data, with its 0 length)
            args.Add(Expr(a.Value));
        }
        var call = $"::{entry}({string.Join(", ", args)})";
        if (m.ReturnType.SpecialType == SpecialType.System_String) call = $"([&]() {{ const char* r_ = {call}; return r_ ? gen::ManagedUtf8(r_, (int)std::strlen(r_)) : nullptr; }}())";
        return pre.Count == 0 ? call : $"([&]() {{ {string.Concat(pre.Select(x => x + " "))}return {call}; }}())";
    }
    string Service(ISymbol m, List<string> args, IOperation at) => Service(m, i => i < args.Count ? args[i] : "", at, null);
    string Service(ISymbol m, Func<int, string> arg, IOperation at, IOperation instance)
    {
        var rule = gen.ServiceRule(m);
        if (rule == null) return null;
        // a [[rewrite]] of this call scoped to the class at hand (in = ...) wins over the member's global rule (CancellationTokenSource.Cancel:
        // a no-op in NetplayManager, the socket's abort in MatchmakingService)
        if (m is IMethodSymbol rm && at?.SemanticModel?.GetEnclosingSymbol(at.Syntax.SpanStart)?.ContainingType?.ToDisplayString() is string rwhere &&
            Rules("rewrite").Any(r => r.ContainsKey("call") && r.ContainsKey("in") && (string)r["in"] == rwhere &&
                                      ((string)r["call"] == rm.OriginalDefinition.ToDisplayString() || (string)r["call"] == rm.ToDisplayString())))
            return null;
        // the receiver is a service token (ResolveX()): evaluated for its effects only when it isn't one
        methodCallees.Remove(m as IMethodSymbol);
        var id = m.ContainingType.ToDisplayString() + "." + m.Name;
        string wildPat; lock (gen.WildPattern) gen.WildPattern.TryGetValue(id, out wildPat);
        if (wildPat != null) gen.Rules.UseWild(id, wildPat);   // emitted through a wildcard rule (RuleCheck.CheckInterop)
        if (rule.TryGetValue("include", out var inc)) foreach (var h in ((string)inc).Split(',')) includes.Add(h.Trim());   // one header, or several by commas
        var key = rule.TryGetValue("key", out var k) ? (string)k : "?";
        if (rule.TryGetValue("throws", out var thr) && thr is true) stmtThrows = true;   // a stand-in that can fail as .NET throws (gen::Fault)
        if (rule.TryGetValue("yields", out var yl) && yl is true) stmtYields = true;     // ... or have nothing yet (gen::WouldBlock: the pass returns)
        if (rule.TryGetValue("value", out var v))
        {
            R.Add(key == "todo" ? "service map: not on the Switch yet (to-do, fixed value)" : $"service map: fixed value ({key})", id);
            return v is string sv ? sv : Convert.ToString(v, System.Globalization.CultureInfo.InvariantCulture).ToLowerInvariant();
        }
        if (rule.ContainsKey("noop") || rule.ContainsKey("default"))
        {
            R.Add(key == "todo" ? "service map: not on the Switch yet (to-do, no-op)" : $"service map: no-op ({key})", id);
            var rt = m is IMethodSymbol mm ? mm.ReturnType : m is IPropertySymbol pp ? pp.Type : m is IFieldSymbol ff ? ff.Type : null;
            return rt == null || rt.SpecialType == SpecialType.System_Void ? "((void)0)" : Literal(null, rt);
        }
        if (rule.TryGetValue("cpp", out var c))
        {
            R.Add("service map: Switch code", id);
            includes.Add("tfexgen_services.hpp");
            var text = (string)c;
            for (int i = 9; i >= 0; i--) if (text.Contains("$" + i)) text = text.Replace("$" + i, arg(i));
            if (text.Contains("$self")) text = text.Replace("$self", instance != null && (instance is not IInstanceReferenceOperation iro || iro.ReferenceKind == InstanceReferenceKind.ImplicitReceiver) ? Recv(instance) : "self");   // the receiver (an object initializer's: the new object)
            return text;
        }
        return null;
    }

    TomlTable OtherUnit(INamedTypeSymbol t)
    {
        if (subst.Count > 0 && Sub(t) is INamedTypeSymbol st) t = st;
        var f = t.DeclaringSyntaxReferences.FirstOrDefault()?.SyntaxTree.FilePath;
        return f != null && gen.UnitOfType.TryGetValue(f + "|" + t.Name + CodeGen.InstSuffix(t), out var u) ? u : null;
    }
    /* gen.AutoUnit after substitution (in a generic copy, Sprite<T> is Sprite<int>) */
    TomlTable AutoU(INamedTypeSymbol t, ISymbol member = null) => gen.AutoUnit(subst.Count > 0 && Sub(t) is INamedTypeSymbol st ? st : t, member);

    /* a game / corlib method's parameter type: an IEnumerable<int / float / double / Vector2> there is the game's object (List<int>'s
     * copy constructor), not the value list TF.State's own parameters of that type are (VKind, state/ units) */
    string GameParamCpp(ITypeSymbol t) =>
        t is INamedTypeSymbol { OriginalDefinition.SpecialType: SpecialType.System_Collections_Generic_IEnumerable_T } et &&
        (et.TypeArguments[0].SpecialType is SpecialType.System_Int32 or SpecialType.System_Single or SpecialType.System_Double ||
         et.TypeArguments[0].ToDisplayString() == "Microsoft.Xna.Framework.Vector2") ? "void*" : Cpp(t);
    /* a game (or FNA / corlib class) method by its body, a virtual one through the vtable, a static one after its type's cctor */
    string Call(IMethodSymbol m, string recv, List<string> args, IOperation at, bool nonVirtual = false)
    {
        var owner = m.ContainingType;
        // a game method the Switch's binary has no body for that FortRise's patch class adds (patch_LevelTiles.ReloadTiles): the patch's unit
        if (InBinary(owner) && gen.FortRisePatchMember(m) is (TomlTable fpu, IMethodSymbol fpm) && !Bound(B.Need("m", SwitchName(owner), m.MethodKind == MethodKind.Constructor ? ".ctor" : m.Name, IlSig(m))))
        {
            R.Add("mechanical: a game method FortRise's patch class adds -> its translated body (the patch's unit)", $"{owner.Name}.{m.Name} -> {fpu["name"]}");
            includes.Add($"tfexgen_{fpu["name"]}.hpp");
            return $"tfex::gen::{fpu["name"]}::{Id(fpm.Name)}({string.Join(", ", (m.IsStatic ? args : new[] { recv }.Concat(args)))})";
        }
        if (m.IsGenericMethod && m.TypeArguments.Any(a => OriginOf(a) == Origin.Tfex))
            return at != null ? Unsupported(at, "generic method over a TF.EX type (no code on the Switch): " + m.OriginalDefinition.ToDisplayString()) : "TFEXGEN_UNSUPPORTED_EXPR()";
        // a generic method of a game type (Scene.Remove<T>, Layer.GetList<T>): BRUTE's instantiations aren't in the method records, and the
        // binder pairing one by name and overload paired Scene.Remove<T>(T) with Remove(GameTags) (Integration A). A [[rewrite]] or a
        // translator rule gives its body; never a guess.
        if (m.IsGenericMethod && owner.ToDisplayString() == "Monocle.Calc" && m.Name == "Choose" && at is IInvocationOperation chInv && m.Parameters.Length == 2)
        {   // Monocle's Calc.Choose<T>(this Random, params T[] / List<T>): its body, `choices[random.Next(choices.Length / Count)]` (TowerFall.exe)
            var cargs = chInv.Arguments.OrderBy(a => a.Parameter.Ordinal).ToList();
            var next = ws.Compilation.GetTypeByMetadataName("System.Random").GetMembers("Next").OfType<IMethodSymbol>().First(x => x.Parameters.Length == 1);
            R.Add("mechanical: Monocle Calc.Choose<T> (its body: choices[random.Next(n)])", m.Parameters[1].Type.ToDisplayString());
            var rv = Expr(cargs[0].Value);
            if (m.Parameters[1].Type is IArrayTypeSymbol)
                return $"([&]() {{ auto c_ = {ArrValue(cargs[1].Value)}; void* r_ = {rv}; return c_[{Call(next, "r_", new List<string> { "c_.len" }, at)}]; }}())";
            if (cargs[1].Value.Type is INamedTypeSymbol lt && lt.Name == "List")
            {
                var cnt = lt.GetMembers("Count").OfType<IPropertySymbol>().First().GetMethod;
                var item = lt.GetMembers().OfType<IPropertySymbol>().First(x => x.IsIndexer).GetMethod;
                return $"([&]() {{ void* c_ = {Expr(cargs[1].Value)}; void* r_ = {rv}; return {Call(item, "c_", new List<string> { Call(next, "r_", new List<string> { Call(cnt, "c_", new List<string>(), at) }, at) }, at)}; }}())";
            }
        }
        // Monocle's Layer.GetFirst<T>() (over its Entities) and Entity.GetFirst<T>() (over its Components): their body, the first element
        // that is a T, else null (TowerFall.exe); any game T, through the list's own Count and indexer
        if (m.IsGenericMethod && m.Name == "GetFirst" && m.Parameters.Length == 0 && !m.IsStatic && recv != null &&
            owner.ToDisplayString() is "Monocle.Layer" or "Monocle.Entity" &&
            owner.GetMembers(owner.Name == "Layer" ? "Entities" : "Components").FirstOrDefault() is ISymbol gfl &&
            (gfl is IPropertySymbol gp ? gp.Type : (gfl as IFieldSymbol)?.Type) is INamedTypeSymbol glt && glt.Name == "List")
        {
            var cnt = glt.GetMembers("Count").OfType<IPropertySymbol>().First().GetMethod;
            var item = glt.GetMembers().OfType<IPropertySymbol>().First(x => x.IsIndexer).GetMethod;
            var listOf = gfl is IPropertySymbol gpp ? Call(gpp.GetMethod, "o_", new List<string>(), at) : $"gen::Fld<void*>(o_, {B.Need("f", SwitchName(owner), gfl.Name)})";
            R.Add("mechanical: Monocle's GetFirst<T> (its body: the first element that is a T)", $"{owner.Name}.GetFirst<{m.TypeArguments[0].Name}>");
            return $"([&]() -> void* {{ void* o_ = {recv}; void* l_ = {listOf}; if (!l_) return nullptr; int n_ = {Call(cnt, "l_", new List<string>(), at)}; " +
                   $"for (int i_ = 0; i_ < n_; i_++) {{ void* e_ = {Call(item, "l_", new List<string> { "i_" }, at)}; if ({AsType("e_", m.TypeArguments[0])} != nullptr) return e_; }} return nullptr; }}())";
        }
        if (m.IsGenericMethod && OriginOf(owner) == Origin.Game)
            return at != null ? Unsupported(at, "generic game method (no instantiation to bind; add a [[rewrite]]): " + m.OriginalDefinition.ToDisplayString()) : "TFEXGEN_UNSUPPORTED_EXPR()";
        var o = OriginOf(owner);
        var tn = SwitchName(owner);
        int k = -1, count = 0, kp = -1, cp = 0;
        string sig = IlSig(m);
        if (o != Origin.Game) (k, count, kp, cp) = order.Of4(m);
        bool ctor = m.MethodKind == MethodKind.Constructor;
        var ret = ctor ? "void*" : Cpp(m.ReturnType);
        var ps = m.Parameters.Select(p => GameParamCpp(p.Type) + (p.RefKind != RefKind.None ? "*" : "")).ToList();
        if (!m.IsStatic) ps.Insert(0, "void*");
        var all = new List<string>(args);
        if (!m.IsStatic) all.Insert(0, at is IInvocationOperation { Instance.Type: IArrayTypeSymbol } ? $"gen::ObjOf({recv})" : recv);   // (an array value's method: its object)
        bool isVirtual = (m.IsVirtual || m.IsAbstract || m.IsOverride) && !nonVirtual && !ctor && !owner.IsSealed && !m.IsSealed;
        // System.Xml's virtuals (FortRise's loaders: XmlNodeList.Count / Item are abstract, the lists Mono returns override them): the
        // vtable, as for the game's classes
        if (isVirtual && (InBinary(owner) || owner.ContainingNamespace?.ToDisplayString() == "System.Xml"))
        {
            var slot = B.Need("v", tn, m.Name, sig, k, count);
            return $"gen::Virt<{ret}>({recv}, {slot}" + string.Concat(args.Select(a => ", " + a)) + ")";
        }
        var body = B.Need("m", tn, ctor ? ".ctor" : m.Name, sig, k, count, kp, cp);
        for (int i = 0; i < m.Parameters.Length; i++)   // our arrays handed to the game: their managed object
            if (m.Parameters[i].Type is IArrayTypeSymbol) { int j = m.IsStatic ? i : i + 1; ps[j] = "void*"; all[j] = $"({all[j]}).obj"; }
        bool arrRet = m.ReturnType is IArrayTypeSymbol;
        if (arrRet) ret = "void*";
        var call = $"gen::Fn<{ret} (*)({string.Join(", ", ps)})>({body})({string.Join(", ", all)})";
        if (arrRet) call = GameArray(m.ReturnType, call);
        if (m.IsStatic && InBinary(owner)) return CctorThen(tn, call);
        return call;
    }

    /* `(Cctor(T), expr)`: BRUTE's beforefieldinit check, left out where the binding says T has no class constructor */
    string CctorThen(string switchType, string expr)
    {
        var c = B.Need("c", switchType);
        return B.Value(c, "cctor") == 0 ? expr : $"(gen::Cctor({c}), {expr})";
    }

    /* a method of a corlib generic instance on the game (List<T>, Queue<T>...) by name */
    string BclCall(INamedTypeSymbol t, string method, string recv, List<string> args, string ret, string[] ilParams = null)
    {
        var m = t.GetMembers(method).OfType<IMethodSymbol>().FirstOrDefault(x => ilParams == null || x.Parameters.Length == ilParams.Length)
             ?? t.GetMembers().OfType<IPropertySymbol>().Select(p => method.StartsWith("get_") ? p.GetMethod : p.SetMethod).FirstOrDefault(x => x?.Name == method);
        if (m == null) { R.Add("unsupported: BCL member", t.ToDisplayString() + "." + method); return "TFEXGEN_UNSUPPORTED_EXPR()"; }
        return Call(m, recv, args, null);
    }
}
