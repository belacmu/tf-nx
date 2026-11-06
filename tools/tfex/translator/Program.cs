// TF.EX C# -> Switch C++ translator. Usage:
//   translator diag <tfex src>                       binding report of the compilation (errors by code)
//   translator models <tfex src> <out dir> <cfg>     Models/WebSocket -> tfexgen_Models.{hpp,cpp}
//   translator code <tfex src> <out dir> <cfg>       the [[unit]] list of translate.toml -> tfexgen_<unit>.{hpp,cpp}
//   translator coverage <tfex src> <cfg>             how much of TF.EX.Patchs + CustomComponent translates, by construct
//   translator fields-glue <tfex src> <out .cs> <cfg> <game type>...   a game struct's public fields as glue C# (TF.EX's reflection over them)
//   translator msgpack-glue <tfex src> <out .cs> <cfg> <root> [plain array types...]   MessagePack-CSharp formatters of a [MessagePackObject] graph, as glue C#
using Microsoft.CodeAnalysis;

namespace TfexTranslator;

static class Program
{
    static int Main(string[] args)
    {
        if (args.Length < 2) { Console.Error.WriteLine("usage: translator diag|models|code|coverage <tfex src> [out dir] [translate.toml]"); return 2; }
        var cfg = Config.Load(args.Length > 3 ? args[3] : args.Length > 2 && args[0] == "coverage" ? args[2] : null);
        var ws = args[0] is "diag-fortrise" or "hooks-scan" ? null : new Workspace(args[1], cfg.RefDirs, cfg.RefFiles, new[] { Path.Combine(cfg.Dir, "translator", "glue") });
        switch (args[0])
        {
            case "diag-fortrise":   // translator diag-fortrise <fortrise root> _ <cfg>: FortRise's compilation (vendor/fortrise)
            {
                var fw = Workspace.ForFortRise(args[1], cfg.RefDirs, cfg.RefFiles);
                var fd = fw.Compilation.GetDiagnostics().Where(d => d.Severity == DiagnosticSeverity.Error).ToList();
                Console.WriteLine($"{fw.Trees.Count} files, {fd.Count} errors");
                foreach (var g in fd.GroupBy(d => d.Id).OrderByDescending(g => g.Count()).Take(20))
                    Console.WriteLine($"  {g.Key} x{g.Count()}: {g.First().GetMessage()}  ({g.First().Location.SourceTree?.FilePath})");
                return 0;
            }
            case "hooks-scan":   // translator hooks-scan <mod src> _ <cfg>: the hooks HookGen makes of a mod's Harmony patches, and what it can't
            {
                var mw = Workspace.ForMod(args[1], cfg.RefDirs, cfg.RefFiles);
                var md = mw.Compilation.GetDiagnostics().Where(d => d.Severity == DiagnosticSeverity.Error).ToList();
                Console.WriteLine($"{mw.Trees.Count} files, {md.Count} errors");
                foreach (var g in md.GroupBy(d => d.Id).OrderByDescending(g => g.Count()).Take(10))
                    Console.WriteLine($"  {g.Key} x{g.Count()}: {g.First().GetMessage()}  ({g.First().Location.SourceTree?.FilePath})");
                var hg = new HookGen(mw);
                foreach (var t in mw.Compilation.SyntaxTrees.SelectMany(t => t.GetRoot().DescendantNodes().OfType<Microsoft.CodeAnalysis.CSharp.Syntax.ClassDeclarationSyntax>()
                             .Select(c => mw.Model(t).GetDeclaredSymbol(c))).Where(c => c != null).Distinct(SymbolEqualityComparer.Default).Cast<INamedTypeSymbol>())
                    hg.Collect(t, t.Name);
                foreach (var line in hg.Describe()) Console.WriteLine(line);
                return 0;
            }
            case "diag":
            {
                var diags = ws.Compilation.GetDiagnostics().Where(d => d.Severity == DiagnosticSeverity.Error).ToList();
                Console.WriteLine($"{ws.Trees.Count} files, {diags.Count} errors");
                foreach (var g in diags.GroupBy(d => d.Id).OrderByDescending(g => g.Count()).Take(15))
                    Console.WriteLine($"  {g.Key} x{g.Count()}: {g.First().GetMessage()}  ({g.First().Location.SourceTree?.FilePath})");
                return 0;
            }
            case "models":
                return new ModelsGen(ws, cfg).Run(args[2]);
            case "code":
            {
                // FortRise's source for its units (translate.toml files "fortrise/..."): vendor/fortrise beside tools/
                var fr = Path.GetFullPath(Path.Combine(cfg.Dir, "..", "..", "vendor", "fortrise"));
                // absent, the FortRise units would be dropped from gen/ without a word (2026-10-21): refuse instead
                if (!Directory.Exists(Path.Combine(fr, "InternalMods"))) { Console.Error.WriteLine($"translator: vendor/fortrise missing ({fr}): run tools/tfex/ensure-vendor.sh"); return 1; }
                var fws = Workspace.ForFortRise(fr, cfg.RefDirs, cfg.RefFiles);
                // FortRise code mods translated onto the Switch (translate.toml [code_mods]: "teamplus/" = vendor/teamplus/src, Teams+ ...), a
                // compilation each; units whose file starts with the prefix use it
                var mods = new Dictionary<string, Workspace>();
                foreach (var (prefix, v) in cfg.Table("code_mods"))
                    if (v is Tomlyn.Model.TomlTable mt && mt["src"] is string msrc && Path.GetFullPath(Path.Combine(cfg.Dir, "..", "..", msrc)) is var mdir && Directory.Exists(mdir))
                        mods[prefix] = Workspace.ForMod(mdir, cfg.RefDirs, cfg.RefFiles, prefix);
                return new CodeGen(ws, cfg, fws, mods).Run(args[2]);
            }
            case "coverage":
                return new Coverage(ws, cfg).Run();
            case "fields-glue":   // translator fields-glue <tfex src> <out .cs> <cfg> <game type>...: reflection over game fields as glue (FieldsGlueGen.cs)
                return new FieldsGlueGen(ws).Run(args[2], args.Skip(4).ToArray());
            case "msgpack-glue":   // translator msgpack-glue <tfex src> <out .cs> <cfg> <root type>: MessagePack formatters as glue (MsgPackGlueGen.cs)
                return new MsgPackGlueGen(ws).Run(args[2], args[4], args.Skip(5).ToArray());
        }
        return 2;
    }
}
