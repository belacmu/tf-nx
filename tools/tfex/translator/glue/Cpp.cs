// Translator glue: [Cpp("expr")] on a `static extern` method = C++ the Switch already has ($0, $1... are the arguments).
namespace TfexGlue
{
    [System.AttributeUsage(System.AttributeTargets.Method)]
    public sealed class CppAttribute : System.Attribute
    {
        public CppAttribute(string expr) { }
        public string Include { get; set; }
    }
}
