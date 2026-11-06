// translate.toml: reference assemblies, model capacities, game binding, hand rules. Paths may start with ~ or $VAR.
using Tomlyn;
using Tomlyn.Model;

namespace TfexTranslator;

public sealed class Config
{
    public TomlTable Root = new();
    public List<string> RefDirs = new();
    public List<string> RefFiles = new();
    public string Dir = ".";

    public static Config Load(string path)
    {
        var c = new Config();
        if (path == null || !File.Exists(path)) return c;
        c.Dir = Path.GetDirectoryName(Path.GetFullPath(path));
        c.Root = Toml.ToModel(File.ReadAllText(path));
        if (c.Root.TryGetValue("refs", out var r) && r is TomlTable rt)
        {
            foreach (var d in Strings(rt, "dirs")) c.RefDirs.Add(Expand(d));
            foreach (var f in Strings(rt, "files")) c.RefFiles.Add(Expand(f));
        }
        return c;
    }

    public static IEnumerable<string> Strings(TomlTable t, string key) =>
        t.TryGetValue(key, out var v) && v is TomlArray a ? a.OfType<string>() : Enumerable.Empty<string>();

    public static string Expand(string p)
    {
        if (p.StartsWith("~/")) p = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), p[2..]);
        return System.Text.RegularExpressions.Regex.Replace(p, @"\$(\w+)", m => Environment.GetEnvironmentVariable(m.Groups[1].Value) ?? "");
    }

    public TomlTable Table(string key) => Root.TryGetValue(key, out var v) && v is TomlTable t ? t : new TomlTable();
    public long Int(TomlTable t, string key, long def) => t.TryGetValue(key, out var v) && v is long l ? l : def;
}
