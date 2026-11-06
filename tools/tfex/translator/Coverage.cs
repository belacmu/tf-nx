// Stage 4: how much of TF.EX.Patchs + CustomComponent the translator handles today. Every type of those trees goes through the same
// emitter as the real units (not written to the module); a method counts as translatable when nothing in it is unsupported and every
// game member it uses binds. Output: tools/tfex/reports/translator-coverage-<tag>.md (path from TFEX_COVERAGE_OUT).
using System.Text;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using Tomlyn.Model;

namespace TfexTranslator;

public sealed class Coverage
{
    readonly Workspace ws;
    readonly Config cfg;
    // the scope (overrides.toml [scope]): include = path prefixes; [scope.exclude] "<glob>" = "<why>": what the Switch deliberately
    // replaces (the reviewable list); [scope.owner] "<path substring>" = "A" | "B": who writes a needed [service] rule (default A)
    string[] Trees = { "network/TF.EX.Patchs/", "network/TF.EX.Domain/CustomComponent/" };
    readonly List<(System.Text.RegularExpressions.Regex rx, string glob, string why)> scopeExcl = new();
    readonly List<(string part, string owner)> owners = new();
    bool InScope(string f) => Trees.Any(p => f.StartsWith(p)) && !scopeExcl.Any(e => e.rx.IsMatch(f));
    string OwnerOf(string f) => owners.FirstOrDefault(o => f.Contains(o.part)).owner ?? "A";
    static System.Text.RegularExpressions.Regex Glob(string g) =>
        new("^" + System.Text.RegularExpressions.Regex.Escape(g).Replace("\\*\\*", ".*").Replace("\\*", "[^/]*") + (g.EndsWith("/") ? ".*" : "") + "$");

    public Coverage(Workspace ws, Config cfg)
    {
        this.ws = ws; this.cfg = cfg;
        var ov = Path.Combine(cfg.Dir, "overrides.toml");
        if (File.Exists(ov) && Tomlyn.Toml.ToModel(File.ReadAllText(ov)) is TomlTable t && t.TryGetValue("scope", out var sc) && sc is TomlTable st)
        {
            if (st.TryGetValue("include", out var inc) && inc is TomlArray ia) Trees = ia.Select(x => (string)x!).ToArray();
            if (st.TryGetValue("exclude", out var ex) && ex is TomlTable et) foreach (var (k, v) in et) scopeExcl.Add((Glob(k), k, (string)v));
            if (st.TryGetValue("owner", out var ow) && ow is TomlTable ot) foreach (var (k, v) in ot) owners.Add((k, (string)v));
        }
    }

    public int Run(string _ = null)
    {
        var gen = new CodeGen(ws, cfg) { WholeAutoUnits = true };
        // the configured units first (calls between units resolve to them), then one unit per type of the trees
        var units = cfg.Root.TryGetValue("unit", out var u) && u is TomlTableArray ta ? ta.ToList() : new List<TomlTable>();
        foreach (var un in units) gen.UnitOfType[CodeGen.UnitKey(un)] = un;
        var all = new List<(string file, string type, UnitEmitter em, string crash)>();
        foreach (var tree in ws.Trees.Where(t => InScope(t.FilePath)))
        {
            foreach (var td in tree.GetRoot().DescendantNodes().OfType<TypeDeclarationSyntax>().Where(t => t.Parent is not TypeDeclarationSyntax))
            {
                var key = tree.FilePath + "|" + td.Identifier.Text;
                var unit = gen.UnitOfType.TryGetValue(key, out var existing) && !existing.ContainsKey("members") ? existing   // a unit of some members only: measure the whole type
                         : new TomlTable { ["name"] = "Cov_" + td.Identifier.Text, ["file"] = tree.FilePath, ["type"] = td.Identifier.Text };
                if (unit.ContainsKey("exclude"))   // a unit's `exclude` keeps members out of the module, not out of the count: measure them too
                {
                    var whole = new TomlTable();
                    foreach (var (k, v) in unit) if (k != "exclude") whole[k] = v;
                    unit = whole;
                }
                gen.UnitOfType[key] = unit;
            }
        }
        foreach (var (key, unit) in gen.UnitOfType.Where(kv => InScope(kv.Key.Split('|')[0])).OrderBy(kv => kv.Key, StringComparer.Ordinal))
            gen.Pending.Enqueue(unit);
        foreach (var un in units.Where(u => ((string)u["file"]).StartsWith("glue/")))   // glue the rules call into
            gen.Pending.Enqueue(un);
        var order = new MetadataOrder(cfg);
        var genDir = Environment.GetEnvironmentVariable("TFEX_COVERAGE_GEN");   // write every unit (to compile them: translator/test/compile-gen.sh)
        if (genDir != null) Directory.CreateDirectory(genDir);
        while (gen.Pending.Count > 0)   // the trees' types, then the TF.EX types they call (auto units)
        {
            var unit = gen.Pending.Dequeue();
            var em = new UnitEmitter(gen, ws, cfg, unit, order);
            string crash = null;
            try
            {
                var (h, c) = em.Emit();
                if (genDir != null) { File.WriteAllText(Path.Combine(genDir, $"tfexgen_{unit["name"]}.hpp"), h); File.WriteAllText(Path.Combine(genDir, $"tfexgen_{unit["name"]}.cpp"), c); }
            }
            catch (Exception e) { crash = e.GetType().Name + ": " + e.Message.Split('\n')[0]; }
            all.Add(((string)unit["file"], (string)unit["type"], em, crash));
        }
        gen.NeedRuntime();
        gen.Binder.Resolve();   // bind.py over every game member used (cached in binding.json)
        if (genDir != null)
        {
            File.WriteAllText(Path.Combine(genDir, "tfexgen_binding.hpp"), gen.Binder.Header());
            File.WriteAllText(Path.Combine(genDir, "tfexgen_structs.hpp"), gen.Structs.Header(Runtime.Label));
            Runtime.Copy(genDir);
            new ModelsGen(ws, cfg).Run(genDir);
        }
        // a method is blocked by its own blockers, its unbound game members, and any TF.EX member it calls that is blocked
        var blocked = new Dictionary<IMethodSymbol, string>(SymbolEqualityComparer.Default);
        var results = all.Where(a => a.crash == null).SelectMany(a => a.em.Methods).ToList();
        // test/compile-gen.sh's verdict: methods whose generated C++ doesn't compile (a translator bug, or a construct it emits wrong)
        var compileErrors = new HashSet<string>();
        if (Environment.GetEnvironmentVariable("TFEX_COMPILE_ERRORS") is string ce && File.Exists(ce))
            foreach (var l in File.ReadAllLines(ce)) compileErrors.Add(l.Trim());
        foreach (var m in results)
            if (compileErrors.Contains(m.Method.ContainingType.Name + "." + m.Method.Name)) m.Blockers.Add("generated C++ doesn't compile");
        foreach (var m in results)
            if ((m.Blockers.Count > 0 || m.Keys.Any(k => gen.Binder.ErrorOf(k.Replace("bind::", "")) != null)) &&
                gen.ServiceRule(m.Method.OriginalDefinition) == null && !m.Method.IsAbstract)   // a [service]-ruled member's callers use the rule,
                                                                                                    // an interface member's its implementation
                blocked[m.Method.OriginalDefinition] = "self";
        var known = new HashSet<IMethodSymbol>(results.Select(r => r.Method.OriginalDefinition), SymbolEqualityComparer.Default);
        for (bool changed = true; changed;)
        {
            changed = false;
            foreach (var m in results)
            {
                if (blocked.ContainsKey(m.Method.OriginalDefinition)) continue;
                var bad = m.Callees.FirstOrDefault(c => blocked.ContainsKey(c) || (!known.Contains(c) && gen.ServiceRule(c) == null && !c.IsAbstract && c.ContainingType?.TypeKind != TypeKind.Interface && false));
                if (bad != null) { blocked[m.Method.OriginalDefinition] = bad.ContainingType.Name + "." + bad.Name; changed = true; }
            }
        }

        // per method
        var rows = new List<(string file, string type, string method, int lines, bool harmony, List<string> blockers)>();
        var calleeBlocks = new Dictionary<string, int>();
        foreach (var (f, t, em, crash) in all)
        {
            if (crash != null) { rows.Add((f, t, "(type)", 1, false, new List<string> { "translator error" })); continue; }
            if (!InScope(f)) continue;   // auto units count through their callers
            foreach (var m in em.Methods)
            {
                if (m.Method.IsAbstract || m.Method.IsExtern) continue;   // a declaration (an interface / abstract member, a P/Invoke): no body to translate
                var blockers = new List<string>(m.Blockers);
                foreach (var c in m.Callees.Distinct(SymbolEqualityComparer.Default).Cast<IMethodSymbol>())
                    if (blocked.TryGetValue(c, out var why) && !SymbolEqualityComparer.Default.Equals(c, m.Method.OriginalDefinition))
                    {
                        blockers.Add("calls a TF.EX member that doesn't translate");
                        var cn = c.ContainingType.Name + "." + c.Name;
                        calleeBlocks[cn] = calleeBlocks.GetValueOrDefault(cn) + 1;
                    }
                foreach (var k in m.Keys.Distinct()) if (gen.Binder.ErrorOf(k.Replace("bind::", "")) is string err) blockers.Add("unresolved game member: " + Short(err));
                bool harmony = m.Method.GetAttributes().Any(a => a.AttributeClass?.Name.StartsWith("Harmony") == true) || m.Method.Name is "Prefix" or "Postfix";
                rows.Add((f, t, m.Method.Name, m.Lines, harmony, blockers));
            }
        }
        // the ledger's [skip] (overrides.toml): members deliberately not on the Switch (PC input, debug keys, updater ...): listed apart,
        // out of the applicable count (keys: "<file glob> <Type>.<Member>" / "<Type>.*" / "*", as manifest.toml)
        var skips = new List<(System.Text.RegularExpressions.Regex file, string member, string why)>();
        var ovPath = Path.Combine(cfg.Dir, "overrides.toml");
        // also [replace] entries the Switch replaces with its own platform input (S-input: the controllers, S-keyboard: swkbd): TF.EX's PC
        // keyboard / gamepad code is never translated for them
        var notApplicable = new List<(string k, object v)>();
        if (File.Exists(ovPath) && Tomlyn.Toml.ToModel(File.ReadAllText(ovPath)) is Tomlyn.Model.TomlTable ovt0)
        {
            if (ovt0.TryGetValue("skip", out var sk0) && sk0 is Tomlyn.Model.TomlTable skt0) foreach (var (k, v) in skt0) notApplicable.Add((k, v));
            if (ovt0.TryGetValue("replace", out var rp0) && rp0 is Tomlyn.Model.TomlTable rpt0)
                foreach (var (k, v) in rpt0)
                    if (v is Tomlyn.Model.TomlTable rt && rt.TryGetValue("key", out var rk) && rk is string rks && (rks.StartsWith("S-input") || rks.StartsWith("S-keyboard")))
                        notApplicable.Add((k, rks + " (replaced by the Switch's input: " + (rt.TryGetValue("at", out var at) ? at : "") + ")"));
        }
        if (notApplicable.Count > 0)
            foreach (var (k, v) in notApplicable)
            {
                var sp = k.IndexOf(' ');
                var fileGlob = sp < 0 ? k : k[..sp]; var mem = sp < 0 ? "*" : k[(sp + 1)..];
                var rx = new System.Text.RegularExpressions.Regex("^" + System.Text.RegularExpressions.Regex.Escape(fileGlob).Replace("\\*", ".*") + "$");
                skips.Add((rx, mem, v is string vs ? vs : v is Tomlyn.Model.TomlTable vt && vt.TryGetValue("key", out var kk) ? (string)kk : "skip"));
            }
        string SkipOf(string file, string type, string method)
        {
            foreach (var (rx, mem, why) in skips)
                if (rx.IsMatch(file) && (mem == "*" || mem == type + ".*" || mem == type + "." + method || mem.StartsWith(type + "." + method + "("))) return why;
            return null;
        }
        var skipped = rows.Where(r => SkipOf(r.file, r.type, r.method) != null).ToList();
        int allN = rows.Count, allOk = rows.Count(r => r.blockers.Count == 0);
        rows = rows.Where(r => SkipOf(r.file, r.type, r.method) == null).ToList();
        var tag = Runtime.Label;
        var sb = new StringBuilder();
        int n = rows.Count, ok = rows.Count(r => r.blockers.Count == 0);
        int lines = rows.Sum(r => r.lines), okLines = rows.Where(r => r.blockers.Count == 0).Sum(r => r.lines);
        sb.Append($"# Translator coverage: all of TF.EX at {tag}\n\n");
        sb.Append($"Generated by `tools/tfex/translate.sh coverage`. Every type in scope (overrides.toml [scope]: {string.Join(", ", Trees.Select(t => "`" + t + "`"))} minus the exclusions listed below) went " +
                  "through the translator (as stage 2-3 units, not written to the module). A method is **translatable** when nothing in it is " +
                  "unsupported, every game member it uses binds on the Switch, every TF.EX member it calls is translatable, and its generated C++ " +
                  "compiles with the Switch toolchain (translator/test/compile-gen.sh). Hand rules (translate.toml) count as translatable: they are " +
                  "listed in the configured units' tfexgen_report.txt.\n\n");
        sb.Append($"**{ok} of {n} applicable methods ({100.0 * ok / Math.Max(1, n):0}%), {okLines} of {lines} lines ({100.0 * okLines / Math.Max(1, lines):0}%) translate today.** " +
                  $"All methods, the ledger's skips included: {allOk} of {allN}. {skipped.Count} methods are not applicable by the ledger (overrides.toml " +
                  "[skip], and [replace] by the Switch's own input: S-input / S-keyboard) and listed at the end.\n\n");
        // blockers by owner: a TF.EX call that needs a Switch [service] rule belongs to A (menus, lobby, matchmaking, roll call) or B
        // (match and after) by the method's file ([scope.owner]); everything else (constructs, compile errors, game members, a
        // translator crash) is the translator's. A blocked callee counts for whoever blocks the callee, so it isn't counted again here.
        static bool IsRule(string b) => b.Contains("no unit translates") || b.Contains("without a set rule") || b.Contains("without a [service] rule") ||
                                        b.Contains("[service]") || b.Contains("service map");
        string OwnerOfBlocker(string file, string b) => b.StartsWith("calls a TF.EX member") ? null : IsRule(b) ? OwnerOf(file) : "translator";
        sb.Append("## Blockers by owner\n\nPer owner: the methods with at least one blocker of theirs (a method can count for several owners), " +
                  "their lines, and the distinct blocker items. Blocked callees are not counted again (they count where they are).\n\n" +
                  "| owner | methods | lines | distinct items |\n|---|---|---|---|\n");
        foreach (var o in new[] { "translator", "A", "B" })
        {
            var mine = rows.Where(r => r.blockers.Any(b => OwnerOfBlocker(r.file, b) == o)).ToList();
            var items = rows.SelectMany(r => r.blockers.Where(b => OwnerOfBlocker(r.file, b) == o)).Distinct().Count();
            sb.Append($"| {(o == "translator" ? "translator (constructs, compile errors, game members)" : o == "A" ? "A: [service] rules (menus, lobby, matchmaking, roll call)" : "B: [service] rules (match and after)")} | {mine.Count} | {mine.Sum(r => r.lines)} | {items} |\n");
        }
        sb.Append($"| (blocked only by a blocked callee) | {rows.Count(r => r.blockers.Count > 0 && r.blockers.All(b => b.StartsWith("calls a TF.EX member")))} | " +
                  $"{rows.Where(r => r.blockers.Count > 0 && r.blockers.All(b => b.StartsWith("calls a TF.EX member"))).Sum(r => r.lines)} | |\n");
        // an interface call's TF.EX implementations in scope: TF.EX's own class (generated) may be the answer instead of a Switch rule
        var implCache = new Dictionary<string, string>();
        string ImplOf(string item)
        {
            var mi = item.IndexOf("no unit translates: ");
            if (mi < 0) return "";
            var full = item[(mi + 20)..]; var dot = full.LastIndexOf('.'); if (dot < 0) return "";
            var tn = full[..dot];
            if (implCache.TryGetValue(tn, out var c)) return c;
            var it = ws.Compilation.GetTypeByMetadataName(tn);
            var impls = it == null || it.TypeKind != TypeKind.Interface ? new List<string>() :
                ws.Trees.Where(t => InScope(t.FilePath)).SelectMany(t => t.GetRoot().DescendantNodes().OfType<TypeDeclarationSyntax>()
                    .Select(d => (ws.Model(t).GetDeclaredSymbol(d) as INamedTypeSymbol, t.FilePath)))
                    .Where(x => x.Item1 != null && x.Item1.AllInterfaces.Contains(it, SymbolEqualityComparer.Default)).Select(x => x.Item1!.Name).Distinct().ToList();
            return implCache[tn] = impls.Count == 0 ? (it == null ? "" : "none") : string.Join(", ", impls);
        }
        foreach (var o in new[] { "A", "B" })
        {
            sb.Append($"\n### Rules needed: {o}\n\nThe last column: an interface's TF.EX implementation in scope. Where one exists, the choice is a Switch " +
                      "rule (the Switch replaces it) or TF.EX's own class (generated; the translator then calls it).\n\n" +
                      "| TF.EX call / field | methods | lines | first method | TF.EX implementation in scope |\n|---|---|---|---|---|\n");
            foreach (var g in rows.SelectMany(r => r.blockers.Where(b => OwnerOfBlocker(r.file, b) == o).Distinct().Select(b => (b, r))).GroupBy(x => x.b)
                         .OrderByDescending(g => g.Sum(x => x.r.lines)))
                sb.Append($"| {g.Key.Replace("unsupported: ", "").Replace("|", "\\|")} | {g.Count()} | {g.Sum(x => x.r.lines)} | {g.First().r.type}.{g.First().r.method} | {ImplOf(g.Key)} |\n");
        }
        sb.Append("\n### Translator blockers, biggest first\n\n| construct | methods | lines |\n|---|---|---|\n");
        foreach (var g in rows.SelectMany(r => r.blockers.Where(b => OwnerOfBlocker(r.file, b) == "translator").Select(Bucket).Distinct().Select(b => (b, r))).GroupBy(x => x.b)
                     .OrderByDescending(g => g.Sum(x => x.r.lines)).Take(60))
            sb.Append($"| {g.Key.Replace("unsupported: ", "").Replace("|", "\\|")} | {g.Count()} | {g.Sum(x => x.r.lines)} |\n");
        sb.Append("\n## Out of scope (overrides.toml [scope.exclude]): what the Switch deliberately replaces\n\n| path | why |\n|---|---|\n");
        foreach (var (_, glob, why) in scopeExcl) sb.Append($"| {glob} | {why.Replace("|", "/")} |\n");
        sb.Append("\n");
        int hn = rows.Count(r => r.harmony), hok = rows.Count(r => r.harmony && r.blockers.Count == 0);
        sb.Append($"Harmony patch methods: {hok} of {hn} have translatable bodies. A patch class becomes exlaunch hooks with `hooks = true` on its " +
                  "translate.toml unit (translator/HookGen.cs; installed by hw.txt `genpatch <Class>`).\n\n");
        sb.Append("## What blocks the rest, by construct\n\nA method can have several blockers; each is counted once per method.\n\n| construct | methods | lines |\n|---|---|---|\n");
        foreach (var g in rows.SelectMany(r => r.blockers.Distinct().Select(b => (b: Bucket(b), r))).GroupBy(x => x.b).OrderByDescending(g => g.Count()))
            sb.Append($"| {g.Key} | {g.Select(x => x.r).Distinct().Count()} | {g.Select(x => x.r).Distinct().Sum(r => r.lines)} |\n");
        sb.Append("\n## Blockers that alone block a method (fix one, the method translates)\n\n| construct | methods |\n|---|---|\n");
        foreach (var g in rows.Where(r => r.blockers.Select(Bucket).Distinct().Count() == 1).GroupBy(r => Bucket(r.blockers[0])).OrderByDescending(g => g.Count()))
            sb.Append($"| {g.Key} | {g.Count()} |\n");
        sb.Append("\n## Per file\n\n| file | types | methods ok / all | lines ok / all |\n|---|---|---|---|\n");
        foreach (var g in rows.GroupBy(r => r.file).OrderBy(g => g.Key, StringComparer.Ordinal))
            sb.Append($"| {g.Key.Replace("network/", "")} | {string.Join(", ", g.Select(r => r.type).Distinct())} | {g.Count(r => r.blockers.Count == 0)} / {g.Count()} | " +
                      $"{g.Where(r => r.blockers.Count == 0).Sum(r => r.lines)} / {g.Sum(r => r.lines)} |\n");
        sb.Append("\n## Unresolved game members (most used)\n\n| member | methods |\n|---|---|\n");
        foreach (var g in gen.Binder.Unresolved.GroupBy(u => u.key).Take(0)) { }
        var unres = rows.SelectMany(r => r.blockers.Where(b => b.StartsWith("unresolved"))).GroupBy(b => b).OrderByDescending(g => g.Count()).Take(30);
        foreach (var g in unres) sb.Append($"| {g.Key.Replace("unresolved game member: ", "")} | {g.Count()} |\n");
        sb.Append("\n## TF.EX members that block their callers most (fix them first)\n\n| member | callers blocked |\n|---|---|\n");
        foreach (var (k, v) in calleeBlocks.OrderByDescending(kv => kv.Value).Take(25)) sb.Append($"| {k} | {v} |\n");
        sb.Append("\n## Not applicable by the ledger (overrides.toml [skip]; [replace] with S-input / S-keyboard)\n\n| method | translates anyway | why |\n|---|---|---|\n");
        foreach (var r in skipped.OrderBy(r => r.file, StringComparer.Ordinal).ThenBy(r => r.method))
            sb.Append($"| {r.type}.{r.method} | {(r.blockers.Count == 0 ? "yes" : "no")} | {SkipOf(r.file, r.type, r.method)!.Replace("|", "/")} |\n");
        sb.Append("\n## Most frequent unsupported items (where to add the next rule or unit)\n\n| item | places |\n|---|---|\n");
        foreach (var (k, l) in gen.Report.Items.Where(kv => kv.Key.StartsWith("unsupported")).OrderByDescending(kv => kv.Value.Count).Take(40))
            sb.Append($"| {k.Replace("unsupported: ", "").Replace("|", "\\|")} | {l.Count} |\n");
        if (Environment.GetEnvironmentVariable("TFEX_COVERAGE_DUMP") is string dump0)   // per method: its blockers (own, unbound members, callees)
        {
            var lines2 = new List<string>();
            foreach (var (f, t, em, crash) in all)
                if (crash == null)
                    foreach (var m in em.Methods)
                    {
                        var bl = new List<string>(m.Blockers);
                        foreach (var k in m.Keys.Distinct()) if (gen.Binder.ErrorOf(k.Replace("bind::", "")) is string err) bl.Add("unbound " + k + ": " + err);
                        foreach (var c in m.Callees.Distinct(SymbolEqualityComparer.Default).Cast<IMethodSymbol>()) if (blocked.TryGetValue(c, out var w) && !SymbolEqualityComparer.Default.Equals(c, m.Method.OriginalDefinition)) bl.Add("calls " + c.ContainingType.Name + "." + c.Name);
                        if (bl.Count > 0) lines2.Add($"{t}.{m.Method.Name}\t{string.Join(" | ", bl.Distinct())}");
                    }
            File.WriteAllText(dump0 + ".methods", string.Join("\n", lines2));
        }
        if (Environment.GetEnvironmentVariable("TFEX_COVERAGE_DUMP") is string dump)   // every unsupported item with its places (for the translator's developer)
            File.WriteAllText(dump, string.Join("\n", gen.Report.Items.OrderByDescending(kv => kv.Value.Count).Select(kv => $"{kv.Value.Count}\t{kv.Key}\t{string.Join(" | ", kv.Value)}")));
        var outPath = Environment.GetEnvironmentVariable("TFEX_COVERAGE_OUT") ?? Path.Combine(cfg.Dir, "reports", "translator-coverage.md");
        File.WriteAllText(outPath, sb.ToString());
        Console.Error.WriteLine($"coverage: {ok}/{n} applicable methods ({allOk}/{allN} with the ledger skips), {okLines}/{lines} lines -> {outPath}");
        // the rule-key check over all of TF.EX in scope (translate.sh rules): a rule no translation of the whole scope looks up is a leftover
        if (Environment.GetEnvironmentVariable("TFEX_RULES_OUT") is string ro && ro.Length > 0) return gen.Rules.Write(ws, cfg, gen.Services, units, ro);
        return 0;
    }

    static string Short(string e) => e.Length > 90 ? e[..90] + "…" : e;
    /* a blocker -> its construct class for the tables */
    static string Bucket(string b)
    {
        foreach (var k in new[] { "call to a TF.EX type no unit translates", "[service] field without a set rule", "call to an excluded member without a [service] rule" })
            if (b.StartsWith(k)) return k + " (a [service] rule)";
        if (b.StartsWith("unresolved game member")) return b.Contains("no body on the Switch") ? "game member with no body on the Switch (inlined/removed)" :
                                                         b.Contains("no type") ? "game type missing on the Switch" :
                                                         b.Contains("overload") || b.Contains("bodies named") ? "game overload the binder can't pick" : "game member not found on the Switch";
        return b;
    }
}
