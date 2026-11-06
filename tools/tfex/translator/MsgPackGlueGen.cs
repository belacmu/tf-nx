// translator msgpack-glue <tfex src> <out .cs> <cfg> <root type>: MessagePack-CSharp's formatters for a [MessagePackObject] graph (TF.State's
// GameState), written as glue C# in TF.EX's style (the translator then translates it like any other code). The bytes follow
// MessagePack-CSharp 2.5's StandardResolver: an object with integer keys = an array of (max key + 1) values, nil in the gaps; a [Union]
// = [key, object]; integers in their compact forms (unsigned forms for values >= 0), float32 / float64, str8 from 32 bytes; collections
// = arrays, dictionaries = maps, null = nil. The writer and reader are the runtime's (tfexgen_skins.cpp Mp*).
using Microsoft.CodeAnalysis;
using System.Text;

namespace TfexTranslator;

sealed class MsgPackGlueGen
{
    readonly Workspace ws;
    readonly Dictionary<ITypeSymbol, string> names = new(SymbolEqualityComparer.Default);
    readonly HashSet<string> used = new();
    readonly Queue<ITypeSymbol> todo = new();
    readonly StringBuilder body = new();
    readonly List<string> gaps = new();
    int tmp;

    public MsgPackGlueGen(Workspace ws) { this.ws = ws; }

    static readonly SymbolDisplayFormat Full = SymbolDisplayFormat.FullyQualifiedFormat;
    static string T(ITypeSymbol t) => t.ToDisplayString(Full);

    static bool Has(ISymbol s, string attr) => s.GetAttributes().Any(a => a.AttributeClass?.Name == attr || a.AttributeClass?.Name == attr + "Attribute");
    static bool IsObject(ITypeSymbol t) => t is INamedTypeSymbol n && (Has(n.OriginalDefinition, "MessagePackObject") || Has(n.OriginalDefinition, "Union"));
    static bool IsUnion(ITypeSymbol t) => t is INamedTypeSymbol n && Has(n.OriginalDefinition, "Union");

    string Name(ITypeSymbol t)
    {
        if (names.TryGetValue(t, out var n)) return n;
        var b = t is INamedTypeSymbol nt && nt.TypeArguments.Length > 0 ? nt.Name + "_" + string.Join("_", nt.TypeArguments.Select(a => a.Name)) : t.Name;
        n = b; for (int i = 2; used.Contains(n); i++) n = b + i;
        used.Add(n); names[t] = n; todo.Enqueue(t);
        return n;
    }

    // members with an integer [Key], the type's own and its bases' (MessagePack-CSharp reads both)
    static List<(int key, ISymbol m, ITypeSymbol type)> Keys(INamedTypeSymbol t)
    {
        var r = new List<(int, ISymbol, ITypeSymbol)>();
        for (var c = t; c != null && c.SpecialType != SpecialType.System_Object; c = c.BaseType)
            foreach (var m in c.GetMembers())
            {
                if (m.IsStatic || m is not (IPropertySymbol or IFieldSymbol) || Has(m, "IgnoreMember")) continue;
                var k = m.GetAttributes().FirstOrDefault(a => a.AttributeClass?.Name is "KeyAttribute" or "Key");
                if (k == null || k.ConstructorArguments.Length != 1 || k.ConstructorArguments[0].Value is not int key) continue;
                if (r.Any(x => x.Item1 == key)) continue;   // (an override of a base member: the most derived)
                r.Add((key, m, m is IPropertySymbol p ? p.Type : ((IFieldSymbol)m).Type));
            }
        return r.OrderBy(x => x.Item1).ToList();
    }

    static bool Signed(SpecialType s) => s is SpecialType.System_SByte or SpecialType.System_Int16 or SpecialType.System_Int32 or SpecialType.System_Int64;
    static bool Unsigned(SpecialType s) => s is SpecialType.System_Byte or SpecialType.System_UInt16 or SpecialType.System_UInt32 or SpecialType.System_UInt64;
    static ITypeSymbol Elem(ITypeSymbol t) => t is IArrayTypeSymbol a ? a.ElementType : t is INamedTypeSymbol n && n.TypeArguments.Length == 1 ? n.TypeArguments[0] : null;
    static bool IsSeq(ITypeSymbol t) => t is IArrayTypeSymbol { Rank: 1 } || t is INamedTypeSymbol n &&
        n.OriginalDefinition.ToDisplayString() is "System.Collections.Generic.List<T>" or "System.Collections.Generic.IEnumerable<T>" or "System.Collections.Generic.ICollection<T>" or
            "System.Collections.Generic.IList<T>" or "System.Collections.Generic.IReadOnlyList<T>" or "System.Collections.Generic.IReadOnlyCollection<T>" or "System.Collections.Generic.HashSet<T>";
    static bool IsMap(ITypeSymbol t) => t is INamedTypeSymbol n && n.TypeArguments.Length == 2 &&
        n.OriginalDefinition.ToDisplayString() is "System.Collections.Generic.Dictionary<TKey, TValue>" or "System.Collections.Generic.IDictionary<TKey, TValue>" or "System.Collections.Generic.IReadOnlyDictionary<TKey, TValue>";

    // ---- the writer: a statement writing x of type t
    string W(string x, ITypeSymbol t, string ind)
    {
        if (t is INamedTypeSymbol { OriginalDefinition.SpecialType: SpecialType.System_Nullable_T } nl)
            return $"{ind}if ({x}.HasValue) {{\n{W($"{x}.Value", nl.TypeArguments[0], ind + "    ")}{ind}}} else Nil();\n";
        var st = t.TypeKind == TypeKind.Enum ? ((INamedTypeSymbol)t).EnumUnderlyingType!.SpecialType : t.SpecialType;
        if (st == SpecialType.System_Boolean) return $"{ind}Bool({x});\n";
        if (Signed(st)) return $"{ind}Int((long){x});\n";
        if (Unsigned(st)) return $"{ind}UInt((ulong){x});\n";
        if (st == SpecialType.System_Single) return $"{ind}F32({x});\n";
        if (st == SpecialType.System_Double) return $"{ind}F64({x});\n";
        if (st == SpecialType.System_String) return $"{ind}Str({x});\n";
        if (t is IArrayTypeSymbol { ElementType.SpecialType: SpecialType.System_Byte }) return $"{ind}Bin({x});\n";
        if (t.ToDisplayString() == "System.TimeSpan") return $"{ind}Int({x}.Ticks);\n";
        if (IsSeq(t))
        {
            var e = $"e{tmp++}"; var n = $"n{tmp++}";
            return $"{ind}if ({x} == null) Nil(); else {{ int {n} = 0; foreach (var {e} in {x}) {n}++; Array({n});\n" +
                   $"{ind}    foreach (var {e} in {x}) {{\n{W(e, Elem(t)!, ind + "        ")}{ind}    }} }}\n";
        }
        if (IsMap(t))
        {
            var nt = (INamedTypeSymbol)t; var kv = $"kv{tmp++}";
            return $"{ind}if ({x} == null) Nil(); else {{ Map({x}.Count);\n{ind}    foreach (var {kv} in {x}) {{\n" +
                   W($"{kv}.Key", nt.TypeArguments[0], ind + "        ") + W($"{kv}.Value", nt.TypeArguments[1], ind + "        ") + $"{ind}    }} }}\n";
        }
        if (IsObject(t)) return $"{ind}W_{Name(t)}({x});\n";
        gaps.Add(t.ToDisplayString());
        return $"{ind}Nil();   // {t.ToDisplayString()}: no formatter here (gap)\n";
    }

    // ---- the reader: an expression of type t (collections through a helper method each)
    readonly Dictionary<string, string> readHelpers = new();
    string R(ITypeSymbol t)
    {
        if (t is INamedTypeSymbol { OriginalDefinition.SpecialType: SpecialType.System_Nullable_T } nl)
            return $"(IsNil() ? ({T(t)})null : {R(nl.TypeArguments[0])})";
        bool en = t.TypeKind == TypeKind.Enum;
        var st = en ? ((INamedTypeSymbol)t).EnumUnderlyingType!.SpecialType : t.SpecialType;
        if (st == SpecialType.System_Boolean) return "ReadBool()";
        if (Signed(st) || Unsigned(st))
        {
            var u = en ? ((INamedTypeSymbol)t).EnumUnderlyingType! : t;
            var v = Unsigned(st) ? $"({T(u)})ReadUInt()" : $"({T(u)})ReadInt()";
            return en ? $"({T(t)}){v}" : v;
        }
        if (st == SpecialType.System_Single) return "ReadF32()";
        if (st == SpecialType.System_Double) return "ReadF64()";
        if (st == SpecialType.System_String) return "ReadStr()";
        if (t is IArrayTypeSymbol { ElementType.SpecialType: SpecialType.System_Byte }) return "ReadBin()";
        if (t.ToDisplayString() == "System.TimeSpan") return "System.TimeSpan.FromTicks(ReadInt())";
        if (IsSeq(t) || IsMap(t))
        {
            var key = T(t);
            if (!readHelpers.TryGetValue(key, out var h))
            {
                h = $"RC_{readHelpers.Count}"; readHelpers[key] = h;
                var sb = new StringBuilder();
                var rt = key;   // the concrete collection it makes (an interface-typed return reads as a LINQ source to the translator)
                if (IsMap(t)) rt = $"System.Collections.Generic.Dictionary<{T(((INamedTypeSymbol)t).TypeArguments[0])}, {T(((INamedTypeSymbol)t).TypeArguments[1])}>";
                else if (t is INamedTypeSymbol rnt) rt = $"System.Collections.Generic.{(rnt.OriginalDefinition.ToDisplayString() == "System.Collections.Generic.HashSet<T>" ? "HashSet" : "List")}<{T(Elem(t)!)}>";
                sb.Append($"        static {rt} {h}()\n        {{\n            if (IsNil()) return null;\n");
                if (IsMap(t))
                {
                    var nt = (INamedTypeSymbol)t;
                    var dt = $"System.Collections.Generic.Dictionary<{T(nt.TypeArguments[0])}, {T(nt.TypeArguments[1])}>";
                    sb.Append($"            int n = ReadMap();\n            var d = new {dt}();\n            for (int i = 0; i < n; i++) {{ var k = {R(nt.TypeArguments[0])}; d[k] = {R(nt.TypeArguments[1])}; }}\n            return d;\n");
                }
                else if (t is IArrayTypeSymbol at)
                    sb.Append($"            int n = ReadArray();\n            var a = new {T(at.ElementType)}[n];\n            for (int i = 0; i < n; i++) a[i] = {R(at.ElementType)};\n            return a;\n");
                else
                {
                    var e = Elem(t)!;
                    var ct = ((INamedTypeSymbol)t).OriginalDefinition.ToDisplayString() == "System.Collections.Generic.HashSet<T>" ? "HashSet" : "List";
                    sb.Append($"            int n = ReadArray();\n            var l = new System.Collections.Generic.{ct}<{T(e)}>();\n            for (int i = 0; i < n; i++) l.Add({R(e)});\n            return l;\n");
                }
                sb.Append("        }\n\n");
                helpers.Append(sb);
            }
            return $"{h}()";
        }
        if (IsObject(t)) return $"R_{Name(t)}()";
        gaps.Add(t.ToDisplayString());
        return $"Skipped<{T(t)}>()";
    }
    readonly StringBuilder helpers = new();

    void EmitType(ITypeSymbol t)
    {
        var n = names[t]; var nt = (INamedTypeSymbol)t; var ty = T(t);
        if (IsUnion(t))
        {
            var cases = nt.OriginalDefinition.GetAttributes().Where(a => a.AttributeClass?.Name is "UnionAttribute" or "Union")
                .Select(a => (key: (int)a.ConstructorArguments[0].Value!, type: (INamedTypeSymbol)a.ConstructorArguments[1].Value!)).ToList();
            // `is` matches subclasses: the most derived cases first
            int Depth(INamedTypeSymbol c) { int d = 0; for (var b = c.BaseType; b != null; b = b.BaseType) d++; return d; }
            body.Append($"        static void W_{n}({ty} v)\n        {{\n            if (v == null) {{ Nil(); return; }}\n            Array(2);\n");
            foreach (var (key, ct) in cases.OrderByDescending(c => Depth(c.type)))
                body.Append($"            if (v is {T(ct)} u{key}) {{ Int({key}); W_{Name(ct)}(u{key}); return; }}\n");
            body.Append("            Nil(); Nil();   // a case the union doesn't list\n        }\n\n");
            body.Append($"        static {ty} R_{n}()\n        {{\n            if (IsNil()) return null;\n            if (ReadArray() != 2) {{ Skip(); Skip(); return null; }}\n            long key = ReadInt();\n");
            foreach (var (key, ct) in cases) body.Append($"            if (key == {key}) return R_{Name(ct)}();\n");
            body.Append("            Skip();\n            return null;\n        }\n\n");
            return;
        }
        var keys = Keys(nt);
        int max = keys.Count == 0 ? -1 : keys.Max(k => k.key);
        bool rf = t.IsReferenceType;
        body.Append($"        static void W_{n}({ty} v)\n        {{\n");
        if (rf) body.Append("            if (v == null) { Nil(); return; }\n");
        body.Append($"            Array({max + 1});\n");
        for (int k = 0; k <= max; k++)
        {
            var m = keys.FirstOrDefault(x => x.key == k);
            body.Append(m.m == null ? "            Nil();\n" : W($"v.{m.m.Name}", m.type, "            "));
        }
        body.Append("        }\n\n");
        body.Append($"        static {ty} R_{n}()\n        {{\n");
        if (rf) body.Append("            if (IsNil()) return null;\n");
        body.Append($"            int n = ReadArray();\n            var v = new {ty}();\n");
        for (int k = 0; k <= max; k++)
        {
            var m = keys.FirstOrDefault(x => x.key == k);
            body.Append(m.m == null ? $"            if (n > {k}) Skip();\n" : $"            if (n > {k}) v.{m.m.Name} = {R(m.type)};\n");
        }
        body.Append($"            for (int i = {max + 1}; i < n; i++) Skip();\n            return v;\n        }}\n\n");
    }

    // plain: types whose arrays TF.EX serializes with MessagePack's default options (no LZ4), e.g. VersusAwards' seed (MatchStats[])
    public int Run(string outFile, string rootName, string[] plain)
    {
        var root = ws.Compilation.GetTypeByMetadataName(rootName);
        if (root == null) { Console.Error.WriteLine($"msgpack-glue: no type {rootName}"); return 1; }
        var rn = Name(root);
        var plainTypes = new List<(ITypeSymbol t, string n)>();
        foreach (var pn in plain)
        {
            var pt = ws.Compilation.GetTypeByMetadataName(pn);
            if (pt == null) { Console.Error.WriteLine($"msgpack-glue: no type {pn}"); return 1; }
            plainTypes.Add((pt, Name(pt)));
        }
        var done = new HashSet<ITypeSymbol>(SymbolEqualityComparer.Default);
        while (todo.Count > 0) { var t = todo.Dequeue(); if (done.Add(t)) EmitType(t); }
        var sb = new StringBuilder();
        sb.Append("// GENERATED by tools/tfex/translator (msgpack-glue " + rootName + "): MessagePack-CSharp 2.5's formatters for TF.State's state, as glue.\n");
        sb.Append("// Do not edit: re-run tools/tfex/translate.sh state-msgpack <tag>. The writer / reader are the runtime's (tfexgen_skins.cpp Mp*); the LZ4 block array is\n");
        sb.Append("// tfexgen_state.cpp's (MessagePack-CSharp's own LZ4 codec, ported). " + done.Count + " types" + (gaps.Count > 0 ? "; gaps: " + string.Join(", ", gaps.Distinct()) : "") + ".\n");
        sb.Append("namespace TfexGlue\n{\n    public static class StateMsgPack\n    {\n");
        foreach (var (name, cpp, sig) in new[] {
            ("Begin", "MpBegin()", "void Begin()"), ("Array", "MpArray($0)", "void Array(int n)"), ("Map", "MpMap($0)", "void Map(int n)"), ("Nil", "MpNil()", "void Nil()"),
            ("Bool", "MpBool($0)", "void Bool(bool v)"), ("Int", "MpInt($0)", "void Int(long v)"), ("UInt", "MpUInt($0)", "void UInt(ulong v)"), ("F32", "MpF32($0)", "void F32(float v)"),
            ("F64", "MpF64($0)", "void F64(double v)"), ("Str", "MpStr($0)", "void Str(string v)"), ("Bin", "MpBin($0)", "void Bin(byte[] v)"), ("End", "MpEnd()", "byte[] End()"),
            ("Open", "MpOpen($0)", "void Open(byte[] bytes)"), ("IsNil", "MpNil_()", "bool IsNil()"), ("ReadArray", "MpReadArray()", "int ReadArray()"), ("ReadMap", "MpReadMap()", "int ReadMap()"),
            ("ReadBool", "MpReadBool()", "bool ReadBool()"), ("ReadInt", "MpReadInt()", "long ReadInt()"), ("ReadUInt", "MpReadUInt()", "ulong ReadUInt()"), ("ReadF32", "MpReadF32()", "float ReadF32()"),
            ("ReadF64", "MpReadF64()", "double ReadF64()"), ("ReadStr", "MpReadStr()", "string ReadStr()"), ("ReadBin", "MpReadBin()", "byte[] ReadBin()"), ("Skip", "MpSkip()", "void Skip()") })
            sb.Append($"        [Cpp(\"tfex::gen::skins::{cpp}\", Include = \"tfexgen_skins.hpp\")] static extern {sig};\n");
        sb.Append("        [Cpp(\"tfex::gen::statert::Lz4BlockArray($0)\", Include = \"tfexgen_state.hpp\")] static extern byte[] Lz4BlockArray(byte[] raw);\n");
        sb.Append("        [Cpp(\"tfex::gen::statert::Lz4Unwrap($0)\", Include = \"tfexgen_state.hpp\")] static extern byte[] Lz4Unwrap(byte[] bytes);\n");

        sb.Append($"        // MessagePackSerializer.Serialize<{root.Name}>(value, StateSerialization.Options): the bytes, then the LZ4 block array (Lz4BlockArray)\n");
        sb.Append($"        public static byte[] Serialize({T(root)} value)\n        {{\n            Begin();\n            W_{rn}(value);\n            return Lz4BlockArray(End());\n        }}\n\n");
        sb.Append($"        public static {T(root)} Deserialize(byte[] bytes)\n        {{\n            var raw = Lz4Unwrap(bytes);\n            Open(raw);\n            return R_{rn}();\n        }}\n\n");
        foreach (var (pt, pn) in plainTypes)
            sb.Append($"        // MessagePackSerializer.Serialize<{pt.Name}[]>(values): MessagePack's default options (no compression)\n" +
                      $"        public static byte[] Serialize{pn}Array({T(pt)}[] values)\n        {{\n            Begin();\n" +
                      W("values", ws.Compilation.CreateArrayTypeSymbol(pt), "            ") + "            return End();\n        }\n\n");
        sb.Append(body).Append(helpers).Append("    }\n}\n");
        File.WriteAllText(outFile, sb.ToString());
        Console.WriteLine($"msgpack-glue: {done.Count} types, {readHelpers.Count} collection readers, {gaps.Distinct().Count()} gaps -> {outFile}");
        foreach (var g in gaps.Distinct()) Console.WriteLine("  gap: " + g);
        return 0;
    }
}
