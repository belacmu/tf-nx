// TF.EX's source at a tag as one Roslyn compilation with a semantic model: the network and state projects, against the PC game
// (TowerFall.exe), FNA, FortRise, Harmony, MessagePack, DI. Private game members are imported and accessibility is ignored, as
// TF.EX builds against a publicized TowerFall.exe. Reference paths: translate.toml [refs] (defaults below), overridable by env.
using System.Reflection;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;

namespace TfexTranslator;

public sealed class Workspace
{
    public CSharpCompilation Compilation { get; }
    public string SrcRoot { get; }
    public IReadOnlyList<SyntaxTree> Trees { get; }

    static readonly string[] Projects =
    {
        "network/TF.EX.Common", "network/TF.EX.Domain", "network/TF.EX.Core", "network/TF.EX.Patchs", "network/TF.EX.API",
        "state/TF.State.Core", "state/TF.State.Domain", "state/TF.State.Patchs", "state/TF.State.TowerFallExtensions",
        "replay/TF.Replay.Core", "replay/TF.Replay.Domain", "replay/TF.Replay.Patchs",
    };

    /* FortRise's own source (vendor/fortrise, MIT): the content-mod loader's projects, a compilation of their own (FortRise is a
     * reference of TF.EX's compilation; its source in there would change how TF.EX's FortRise calls bind). Paths "fortrise/<rel>". */
    static readonly string[] FortRiseProjects = { "InternalMods/FortRise.Content", "TowerFall.FortRise.mm" };
    public bool IsFortRise { get; }

    public static Workspace ForFortRise(string fortriseRoot, IEnumerable<string> refDirs, IEnumerable<string> refFiles)
    {
        // FortRise builds against its stripped, publicized vanilla TowerFall.exe (lib-stripped), not the FortRise-patched one
        var root = Path.GetFullPath(fortriseRoot);
        var files = new[] { Path.Combine(root, "lib-stripped", "TowerFall.exe"), Path.Combine(root, "lib-stripped", "FNA.dll"), Path.Combine(root, "lib-stripped", "Steamworks.NET.dll") }
            .Concat(refFiles.Where(f => Path.GetFileName(f) is not ("TowerFall.Patch.dll" or "TowerFall.exe" or "FNA.dll")));
        // the Switch's glue for FortRise (translator/glue/fortrise: the SD card's mods, the loader's driver), paths "fortrise/glue/<file>"
        var glue = Path.Combine(AppContext.BaseDirectory, "..", "..", "glue", "fortrise");
        return new Workspace(root, refDirs, files, new[] { glue, Path.Combine(glue, "..", "Cpp.cs") }, FortRiseProjects, "fortrise/");   // + the [Cpp] attribute
    }

    /* a FortRise mod's source folder as one compilation, against what TF.EX compiles against (FortRise as its DLL, as TF.EX sees it);
     * paths "<prefix><rel>": "mod/" for translator hooks-scan, "teamplus/" for Teams+'s units (vendor/teamplus) */
    public static Workspace ForMod(string modRoot, IEnumerable<string> refDirs, IEnumerable<string> refFiles, string prefix = "mod/") =>
        new Workspace(modRoot, refDirs, refFiles, null, new[] { "." }, prefix);

    public Workspace(string srcRoot, IEnumerable<string> refDirs, IEnumerable<string> refFiles, IEnumerable<string> glueDirs = null)
        : this(srcRoot, refDirs, refFiles, glueDirs, Projects, "") { }

    Workspace(string srcRoot, IEnumerable<string> refDirs, IEnumerable<string> refFiles, IEnumerable<string> glueDirs, string[] projects, string prefix)
    {
        SrcRoot = Path.GetFullPath(srcRoot);
        IsFortRise = prefix == "fortrise/";
        var opts = new CSharpParseOptions(LanguageVersion.Preview);
        var trees = new List<SyntaxTree>();
        foreach (var p in projects)
        {
            var dir = Path.Combine(SrcRoot, p);
            if (!Directory.Exists(dir)) continue;
            foreach (var f in Directory.EnumerateFiles(dir, "*.cs", SearchOption.AllDirectories)
                         .Where(f => !f.Contains("/obj/") && !f.Contains("/bin/")).OrderBy(f => f, StringComparer.Ordinal))
                trees.Add(CSharpSyntaxTree.ParseText(File.ReadAllText(f), opts, path: prefix + Path.GetRelativePath(SrcRoot, f)));
        }
        // translator glue in TF.EX's style (tools/tfex/translator/glue: test drivers), compiled with TF.EX, paths "glue/<file>"
        foreach (var g in glueDirs ?? Enumerable.Empty<string>())
            if (File.Exists(g)) trees.Add(CSharpSyntaxTree.ParseText(File.ReadAllText(g), opts, path: prefix + "glue/" + Path.GetFileName(g)));
            else if (Directory.Exists(g))
                foreach (var f in Directory.EnumerateFiles(g, "*.cs").OrderBy(f => f, StringComparer.Ordinal))
                    trees.Add(CSharpSyntaxTree.ParseText(File.ReadAllText(f), opts, path: prefix + "glue/" + Path.GetFileName(f)));
        // implicit usings of an SDK project (TF.EX's csprojs have ImplicitUsings enable)
        trees.Add(CSharpSyntaxTree.ParseText(
            "global using System; global using System.Collections.Generic; global using System.IO; global using System.Linq; " +
            "global using System.Net.Http; global using System.Threading; global using System.Threading.Tasks;", opts, path: "<implicit>"));
        Trees = trees;

        var refs = new List<MetadataReference>();
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        void Add(string f)
        {
            // the game is one slot: the FortRise-patched assembly (TowerFall.Patch.dll) or a TowerFall.exe, whichever is listed first and exists
            var key = Path.GetFileName(f).Equals("TowerFall.Patch.dll", StringComparison.OrdinalIgnoreCase) ? "TowerFall.exe" : Path.GetFileName(f);
            if (IsFortRise && Path.GetFileName(f).StartsWith("FortRise", StringComparison.OrdinalIgnoreCase)) return;   // its source is compiled instead
            if (!File.Exists(f) || !seen.Add(key)) return;
            try { refs.Add(MetadataReference.CreateFromFile(f)); } catch { }
        }
        foreach (var f in refFiles) Add(f);
        string game = refFiles.FirstOrDefault(f => File.Exists(f) && Path.GetFileName(f) is "TowerFall.Patch.dll" or "TowerFall.exe");
        if (game == null && !IsFortRise) Console.Error.WriteLine("translator: ERROR: no game reference (translate.toml [refs]): neither the FortRise-patched TowerFall.Patch.dll nor a TowerFall.exe exists");
        else if (!IsFortRise && Path.GetFileName(game) != "TowerFall.Patch.dll")
            Console.Error.WriteLine($"translator: WARNING: the FortRise-patched TowerFall.Patch.dll (translate.toml [refs]) is missing; using {game}: FortRise members " +
                                    "become Invalid and default arguments can differ, so output and coverage won't match. Recreate it: tools/tfex/README.md \"FortRise reference\".");
        foreach (var d in refDirs.Where(Directory.Exists))
            foreach (var f in Directory.EnumerateFiles(d, "*.dll").OrderBy(x => x))
                if (!Path.GetFileName(f).StartsWith("TF.")) Add(f);   // TF.EX's own DLLs: the source is compiled instead
        // the running .NET's own reference set (System.*, mscorlib/netstandard facades for TowerFall.exe's net40 references)
        foreach (var f in ((string)AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES")).Split(Path.PathSeparator)) Add(f);

        var co = new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary, allowUnsafe: true,
                                              metadataImportOptions: MetadataImportOptions.All, nullableContextOptions: NullableContextOptions.Disable);
        co = IgnoreAccessibility(co);
        Compilation = CSharpCompilation.Create("TfexAll", trees, refs, co);
    }

    // Roslyn's internal BinderFlags.IgnoreAccessibility (what publicizers achieve), set through reflection.
    static CSharpCompilationOptions IgnoreAccessibility(CSharpCompilationOptions co)
    {
        var prop = typeof(CSharpCompilationOptions).GetProperty("TopLevelBinderFlags", BindingFlags.Instance | BindingFlags.NonPublic);
        if (prop == null) return co;
        var flagsType = prop.PropertyType;
        var ignore = Enum.Parse(flagsType, "IgnoreAccessibility");
        prop.SetValue(co, ignore);
        return co;
    }

    public SemanticModel Model(SyntaxTree t) => Compilation.GetSemanticModel(t, ignoreAccessibility: true);

    public SyntaxTree Tree(string relPath) => Trees.FirstOrDefault(t => t.FilePath == relPath);
}
