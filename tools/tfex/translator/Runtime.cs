// The hand-written runtime lives in the module's tree (hw-probe/exlaunch/source/program/tfex/runtime/) and is compiled there, in place
// (misc/mk/common.mk INCLUDES: runtime/ and gen/ include each other by name). The translator only reads it (copied next to the
// translator at build, translator.csproj) and copies it into throwaway folders (the coverage compile check); tfex/gen/ never holds it.
namespace TfexTranslator;

public static class Runtime
{
    public static string Label = Environment.GetEnvironmentVariable("TFEX_LABEL") ?? "(working tree)";
    static string Src => Path.Combine(AppContext.BaseDirectory, "runtime");
    public static IEnumerable<string> Files => Directory.EnumerateFiles(Src).Select(Path.GetFileName);

    /* a throwaway folder only (coverage): the generated files there include the runtime by name */
    public static void Copy(string outDir)
    {
        foreach (var f in Directory.EnumerateFiles(Src))
            File.Copy(f, Path.Combine(outDir, Path.GetFileName(f)), overwrite: true);
    }

    /* tfex/gen/: copies an older translator left there go (the module compiles runtime/ in place) */
    public static void RemoveCopies(string outDir)
    {
        foreach (var f in Files)
            if (File.Exists(Path.Combine(outDir, f))) { File.Delete(Path.Combine(outDir, f)); Console.Error.WriteLine("code: removed the runtime copy " + f + " (compiled in place now)"); }
    }
}
