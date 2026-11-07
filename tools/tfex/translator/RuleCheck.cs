// Evaluation rec. 2a: every rule key in overrides.toml [service] and translate.toml [fortrise] / [invalid] / [[unit]] members must
// resolve at the tag (an upstream rename would otherwise make the rule silently stop applying), and should be used by some unit
// (else it is a leftover, or `parked = "<why>"` for a unit or feature not translated yet). CodeGen records each lookup that matched;
// after a `code` run this writes the verdict (translate.sh rules <tag>; new-tag.sh fails on unresolved keys).
using Microsoft.CodeAnalysis;
using Tomlyn.Model;

namespace TfexTranslator;

public sealed class RuleCheck
{
    public readonly HashSet<string> Used = new(StringComparer.Ordinal);   // "service:<key>", "fortrise:<key>", "invalid:<key>"
    public void Use(string table, string key) { lock (Used) Used.Add(table + ":" + key); }
    /* members a unit emitted a call to through a wildcard rule only ("Iface.*"): member -> the pattern */
    public readonly Dictionary<string, string> Wild = new(StringComparer.Ordinal);
    public void UseWild(string member, string pattern) { lock (Wild) Wild[member] = pattern; }

    /* TF.EX's ApiSurfaceTests on the Switch's side (translate.toml [[interop]] consumer / provider): every method of TF.EX's consumer
     * interface exists on the provider with the same return and parameter types (TF.EX's own check: GetApi<T>() would throw at mod
     * load), and none that generated code calls is answered only by a wildcard rule (a new or missed member would silently be the
     * wildcard's no-op; `wildcard = [...]` lists the members that may be, each a decision). Problems go in the report; the count
     * fails the code run. */
    public int CheckInterop(Workspace ws, Config cfg, Report report, IEnumerable<Workspace> others = null)
    {
        var c = ws.Compilation;
        // a type in any compilation: TF.EX's, FortRise's, a code mod's (Teams+'s copy of WiderSet's interface)
        var comps = new[] { c }.Concat((others ?? Enumerable.Empty<Workspace>()).Where(w => w != null).Select(w => w.Compilation)).ToList();
        INamedTypeSymbol Find(string n) => comps.Select(x => x.GetTypeByMetadataName(n)).FirstOrDefault(x => x != null);
        int problems = 0;
        var arr = cfg.Root.TryGetValue("interop", out var v) && v is TomlTableArray a ? a : new TomlTableArray();
        static string Sig(IMethodSymbol m) => m.ReturnType.ToDisplayString() + " " + m.Name + "(" + string.Join(", ", m.Parameters.Select(p => p.Type.ToDisplayString())) + ")";
        foreach (var t in arr)
        {
            if (t.ContainsKey("absent")) continue;   // the provider mod isn't on the Switch: nothing to check against
            var cn = (string)t["consumer"]; var pn = (string)t["provider"];
            var ct = Find(cn); var pt = Find(pn);
            // TFEX_PUBLIC=1 (Config.Load): the consumer's code mod is left out of the public tree, so it has no call to check
            if (ct == null && Environment.GetEnvironmentVariable("TFEX_PUBLIC") == "1") continue;
            if (ct == null || pt == null) { report.Add("interop: PROBLEM", $"{cn} / {pn}: type not found"); problems++; continue; }
            var allowed = Config.Strings(t, "wildcard").ToHashSet(StringComparer.Ordinal);
            var provided = pt.GetMembers().OfType<IMethodSymbol>().Where(m => m.DeclaredAccessibility == Accessibility.Public && !m.IsStatic)
                .Select(Sig).ToHashSet(StringComparer.Ordinal);
            int n = 0;
            foreach (var m in ct.GetMembers().OfType<IMethodSymbol>().Where(m => m.MethodKind == MethodKind.Ordinary))
            {
                n++;
                if (!provided.Contains(Sig(m))) { report.Add("interop: PROBLEM", $"{pn} lacks {cn}.{Sig(m)} (TF.EX's GetApi<T>() would throw)"); problems++; }
                var id = ct.ToDisplayString() + "." + m.Name;
                if (Wild.TryGetValue(id, out var pat) && !allowed.Contains(m.Name))
                { report.Add("interop: PROBLEM", $"{id} is called, answered only by the wildcard rule {pat}: give it its own rule, or list it in [[interop]] wildcard with the reason"); problems++; }
            }
            report.Add("interop: checked (consumer -> provider, TF.EX's ApiSurfaceTests)", $"{cn} -> {pn}: {n} methods");
        }
        return problems;
    }

    /* "A.B.Type.Member", "A.B.Type" or "A.B.Type.Prefix*": the type by its longest resolvable prefix, then the member (or the prefix) */
    static string Resolve(Compilation c, string key)
    {
        // a constructed generic type ("SafeBytes<TF.EX.Domain.Models.GGRSConfig>"): its definition by metadata name ("SafeBytes`1")
        key = System.Text.RegularExpressions.Regex.Replace(key, @"<([^<>]*)>", m => "`" + (m.Groups[1].Value.Split(',').Length));
        var parts = key.Split('.');
        for (int n = parts.Length; n >= 1; n--)
        {
            var tn = string.Join(".", parts.Take(n));
            var t = c.GetTypeByMetadataName(tn) ?? NestedType(c, parts, n);
            if (t == null) continue;
            if (n == parts.Length) return null;   // the type itself
            if (n != parts.Length - 1) return $"type {tn} has no nested type {parts[n]}";
            var m = parts[n];
            if (m == "*") return null;
            bool wild = m.EndsWith("*"); var p = wild ? m[..^1] : m;
            for (var bt = t; bt != null; bt = bt.BaseType)
                if (bt.GetMembers().Any(x => wild ? x.Name.StartsWith(p, StringComparison.Ordinal) : x.Name == p)) return null;
            if (t.AllInterfaces.Any(i => i.GetMembers().Any(x => wild ? x.Name.StartsWith(p, StringComparison.Ordinal) : x.Name == p))) return null;
            return $"{tn} has no member {m}";
        }
        return "no type " + string.Join(".", parts.Take(parts.Length - 1));
    }
    static INamedTypeSymbol NestedType(Compilation c, string[] parts, int n)
    {   // Outer.Inner written with a dot (metadata name: Outer+Inner)
        for (int k = n - 1; k >= 1; k--)
            if (c.GetTypeByMetadataName(string.Join(".", parts.Take(k))) is INamedTypeSymbol o)
            {
                INamedTypeSymbol cur = o;
                for (int j = k; j < n && cur != null; j++) cur = cur.GetTypeMembers(parts[j]).FirstOrDefault();
                return cur;
            }
        return null;
    }

    public int Write(Workspace ws, Config cfg, List<(string pattern, TomlTable rule)> services, List<TomlTable> units, string outPath)
    {
        var c = ws.Compilation;
        var unresolved = new List<string>(); var unused = new List<string>(); var parked = new List<string>();
        void Check(string table, string key, TomlTable rule, bool resolve)
        {
            if (resolve && Resolve(c, key) is string why) unresolved.Add($"[{table}] `{key}`: {why}");
            if (Used.Contains(table + ":" + key)) return;
            if (rule != null && rule.TryGetValue("parked", out var pk)) parked.Add($"[{table}] `{key}`: {pk}");
            else unused.Add($"[{table}] `{key}`");
        }
        foreach (var (k, r) in services) Check("service", k, r, true);
        foreach (var (k, v) in cfg.Table("fortrise")) Check("fortrise", k, v as TomlTable, true);
        foreach (var (k, v) in cfg.Table("invalid")) Check("invalid", k, v as TomlTable, false);   // keys are source text, not symbols
        foreach (var u in units)
        {
            var tn = (string)u["type"];
            var type = c.GetSymbolsWithName(tn, SymbolFilter.Type).OfType<INamedTypeSymbol>()
                .FirstOrDefault(t => t.DeclaringSyntaxReferences.Any(r => r.SyntaxTree.FilePath.EndsWith((string)u["file"], StringComparison.Ordinal)));
            if (type == null) { unresolved.Add($"[[unit]] `{u["name"]}`: no type {tn} in {u["file"]}"); continue; }
            foreach (var key in new[] { "members", "exclude" })
                foreach (var m in Config.Strings(u, key))
                    if (!type.GetMembers(m).Any()) unresolved.Add($"[[unit]] `{u["name"]}` {key}: {tn} has no member {m}");
        }
        var md = new System.Text.StringBuilder();
        md.Append($"# Rule keys at {Runtime.Label}\n\nGenerated by `tools/tfex/translate.sh rules <tag>` (translator RuleCheck.cs). Unresolved: a key that names a type or member\n" +
                  "TF.EX / the game doesn't have at this tag (the rule no longer applies: fix or delete it). Unused: no unit's translation looked it\n" +
                  "up (a leftover: delete it, or mark it `parked = \"<unit or feature>\"`).\n\n");
        md.Append($"## Unresolved ({unresolved.Count})\n\n").Append(unresolved.Count == 0 ? "none\n" : string.Join("\n", unresolved.Select(x => "- " + x)) + "\n");
        md.Append($"\n## Unused, not parked ({unused.Count})\n\n").Append(unused.Count == 0 ? "none\n" : string.Join("\n", unused.Select(x => "- " + x)) + "\n");
        md.Append($"\n## Parked ({parked.Count})\n\n").Append(parked.Count == 0 ? "none\n" : string.Join("\n", parked.Select(x => "- " + x)) + "\n");
        File.WriteAllText(outPath, md.ToString());
        Console.Error.WriteLine($"rules: {unresolved.Count} unresolved, {unused.Count} unused, {parked.Count} parked -> {outPath}");
        return unresolved.Count > 0 ? 3 : 0;
    }
}
