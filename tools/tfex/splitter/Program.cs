// TF.EX method splitter: lists every member of every C# file under a tree as one JSON line:
//   {file, type, member, kind, line, end, strict, norm, patch, text}
// file   = path relative to the root given (update.sh passes TF.EX's src/)
// type   = nesting path of the declaring type (Outer.Inner<T>), namespace left out
// member = Name(param types) | .ctor(..) | .cctor | this[..] | op_X(..) | <type> for the type's own header
// strict = hash of the member's tokens (whitespace and comments ignored)
// norm   = hash after the syntax normalizer below: equal norm but different strict = a syntax-only change
// patch  = Harmony target "Type.Method kind" when the member is a Harmony patch method
// Usage: splitter <root dir> [out.jsonl]   (stdout when no out file)
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using static Microsoft.CodeAnalysis.CSharp.SyntaxFactory;

static class Splitter
{
    static int Main(string[] args)
    {
        if (args.Length < 1) { Console.Error.WriteLine("usage: splitter <root> [out.jsonl]"); return 2; }
        var root = Path.GetFullPath(args[0]);
        var outW = args.Length > 1 ? new StreamWriter(args[1]) : new StreamWriter(Console.OpenStandardOutput());
        var opts = new CSharpParseOptions(LanguageVersion.Preview);
        var files = Directory.EnumerateFiles(root, "*.cs", SearchOption.AllDirectories)
            .Where(f => !f.Contains("/obj/") && !f.Contains("/bin/")).OrderBy(f => f, StringComparer.Ordinal);
        int n = 0;
        foreach (var f in files)
        {
            var rel = Path.GetRelativePath(root, f).Replace('\\', '/');
            var tree = CSharpSyntaxTree.ParseText(File.ReadAllText(f), opts);
            foreach (var rec in Walk(tree.GetCompilationUnitRoot(), rel))
            {
                outW.WriteLine(JsonSerializer.Serialize(rec, Json));
                n++;
            }
        }
        outW.Flush();
        Console.Error.WriteLine($"splitter: {n} members");
        return 0;
    }

    static readonly JsonSerializerOptions Json = new() { Encoder = System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping };

    public record Rec(string file, string type, string member, string kind, int line, int end,
                      string strict, string norm, string patch, string text);

    static IEnumerable<Rec> Walk(CompilationUnitSyntax cu, string file)
    {
        foreach (var t in cu.DescendantNodes(n => n is CompilationUnitSyntax || n is BaseNamespaceDeclarationSyntax)
                            .OfType<MemberDeclarationSyntax>())
            foreach (var r in Type(t, file, ""))
                yield return r;
    }

    static string TypeName(MemberDeclarationSyntax t) => t switch
    {
        TypeDeclarationSyntax td => td.Identifier.Text + (td.TypeParameterList != null ? "<" + string.Join(",", td.TypeParameterList.Parameters.Select(p => p.Identifier.Text)) + ">" : ""),
        EnumDeclarationSyntax e => e.Identifier.Text,
        DelegateDeclarationSyntax d => d.Identifier.Text,
        _ => null,
    };

    static IEnumerable<Rec> Type(MemberDeclarationSyntax t, string file, string outer)
    {
        var name = TypeName(t);
        if (name == null) yield break;
        var path = outer.Length > 0 ? outer + "." + name : name;
        if (t is EnumDeclarationSyntax || t is DelegateDeclarationSyntax)
        {
            yield return Make(file, path, "<type>", t is EnumDeclarationSyntax ? "enum" : "delegate", t, t, null);
            yield break;
        }
        var td = (TypeDeclarationSyntax)t;
        // The type's own record: attributes, modifiers, base list, primary-ctor parameters (members excluded).
        var header = td.WithMembers(default).WithOpenBraceToken(default).WithCloseBraceToken(default).WithSemicolonToken(default);
        yield return Make(file, path, "<type>", td.Keyword.Text, td, header, null);
        var classPatch = HarmonyArgs(td.AttributeLists);
        foreach (var m in td.Members)
        {
            switch (m)
            {
                case BaseTypeDeclarationSyntax or DelegateDeclarationSyntax:
                    foreach (var r in Type(m, file, path)) yield return r;
                    break;
                case BaseFieldDeclarationSyntax fd:
                    foreach (var v in fd.Declaration.Variables)
                    {
                        // one record per declarator: attributes + modifiers + type + this declarator
                        var one = fd.WithDeclaration(fd.Declaration.WithVariables(SingletonSeparatedList(v)));
                        yield return Make(file, path, v.Identifier.Text, fd is EventFieldDeclarationSyntax ? "event" : "field", m, one, null);
                    }
                    break;
                case MethodDeclarationSyntax md:
                    yield return Make(file, path, md.Identifier.Text + Generic(md.TypeParameterList) + Params(md.ParameterList), "method", m, m,
                                      Patch(classPatch, md));
                    break;
                case ConstructorDeclarationSyntax cd:
                    yield return Make(file, path, cd.Modifiers.Any(SyntaxKind.StaticKeyword) ? ".cctor" : ".ctor" + Params(cd.ParameterList), "ctor", m, m, null);
                    break;
                case DestructorDeclarationSyntax:
                    yield return Make(file, path, "Finalize()", "dtor", m, m, null);
                    break;
                case PropertyDeclarationSyntax pd:
                    yield return Make(file, path, pd.Identifier.Text, "property", m, m, null);
                    break;
                case EventDeclarationSyntax ed:
                    yield return Make(file, path, ed.Identifier.Text, "event", m, m, null);
                    break;
                case IndexerDeclarationSyntax ix:
                    yield return Make(file, path, "this" + Params(ix.ParameterList), "indexer", m, m, null);
                    break;
                case OperatorDeclarationSyntax od:
                    yield return Make(file, path, "op_" + od.OperatorToken.Text + Params(od.ParameterList), "operator", m, m, null);
                    break;
                case ConversionOperatorDeclarationSyntax co:
                    yield return Make(file, path, "op_" + co.ImplicitOrExplicitKeyword.Text + "(" + TypeText(co.Type) + ")" + Params(co.ParameterList), "operator", m, m, null);
                    break;
                default:
                    yield return Make(file, path, "<" + m.Kind() + "@" + Line(m) + ">", "other", m, m, null);
                    break;
            }
        }
    }

    static string Generic(TypeParameterListSyntax t) => t == null ? "" : "<" + string.Join(",", t.Parameters.Select(p => p.Identifier.Text)) + ">";
    static string Params(BaseParameterListSyntax p) =>
        (p is BracketedParameterListSyntax ? "[" : "(") +
        string.Join(", ", p.Parameters.Select(x => string.Join(" ", x.Modifiers.Select(mm => mm.Text)
            .Concat(new[] { x.Type == null ? "" : TypeText(x.Type) }).Where(s => s.Length > 0)))) +
        (p is BracketedParameterListSyntax ? "]" : ")");
    // Parameter type for keys: no spaces, namespace qualifiers dropped (Models.WebSocket.Player -> Player),
    // so a re-qualified type keeps the member's key.
    static string TypeText(TypeSyntax t) =>
        System.Text.RegularExpressions.Regex.Replace(Tok(t).Replace(" ", ""), @"(?:\w+\.)+(?=\w)", "");
    static int Line(SyntaxNode n) => n.GetLocation().GetLineSpan().StartLinePosition.Line + 1;

    static Rec Make(string file, string type, string member, string kind, SyntaxNode loc, SyntaxNode body, string patch)
    {
        var span = loc.GetLocation().GetLineSpan();
        var strict = Tok(body);
        string norm;
        try { norm = Tok(new Normalizer().Visit(body)); }
        catch { norm = strict; }
        return new Rec(file, type, member, kind, span.StartLinePosition.Line + 1, span.EndLinePosition.Line + 1,
                       Hash(strict), Hash(norm), patch, norm);
    }

    static string Hash(string s) => Convert.ToHexString(SHA1.HashData(Encoding.UTF8.GetBytes(s)))[..12].ToLowerInvariant();

    // Tokens joined by one space: whitespace, comments and preprocessor-free trivia don't count.
    // A trailing comma before } or ] is dropped (optional in initializers and collection expressions).
    static string Tok(SyntaxNode n)
    {
        var toks = n.DescendantTokens().Where(t => !t.IsKind(SyntaxKind.None) && t.Text.Length > 0).ToList();
        var sb = new StringBuilder();
        for (int i = 0; i < toks.Count; i++)
        {
            var t = toks[i];
            if (t.IsKind(SyntaxKind.CommaToken) && i + 1 < toks.Count &&
                (toks[i + 1].IsKind(SyntaxKind.CloseBraceToken) || toks[i + 1].IsKind(SyntaxKind.CloseBracketToken))) continue;
            if (sb.Length > 0) sb.Append(' ');
            sb.Append(t.Text);
        }
        return sb.ToString();
    }

    // ---- Harmony ----
    // (type, method, getter/setter/ctor) from [HarmonyPatch(...)] argument lists.
    record HP(string Type, string Method, string Special);
    static HP HarmonyArgs(SyntaxList<AttributeListSyntax> lists)
    {
        string ty = null, me = null, sp = null;
        foreach (var a in lists.SelectMany(l => l.Attributes))
        {
            var an = a.Name.ToString();
            if (!an.EndsWith("HarmonyPatch")) continue;
            foreach (var arg in a.ArgumentList?.Arguments ?? default)
            {
                var e = arg.Expression;
                if (e is TypeOfExpressionSyntax to) ty ??= LastName(to.Type.ToString());
                else if (e is InvocationExpressionSyntax inv && inv.Expression.ToString() == "nameof") me ??= LastName(inv.ArgumentList.Arguments[0].ToString());
                else if (e is LiteralExpressionSyntax lit && lit.IsKind(SyntaxKind.StringLiteralExpression)) me ??= lit.Token.ValueText;
                else if (e is MemberAccessExpressionSyntax ma && ma.Expression.ToString().EndsWith("MethodType")) sp = ma.Name.ToString();
            }
        }
        return ty == null && me == null && sp == null ? null : new HP(ty, me, sp);
    }
    static string LastName(string s) { s = s.Trim(); var i = s.LastIndexOf('.'); return i < 0 ? s : s[(i + 1)..]; }

    static string Patch(HP cls, MethodDeclarationSyntax md)
    {
        var attrs = md.AttributeLists.SelectMany(l => l.Attributes).Select(a => a.Name.ToString()).ToList();
        string kind = null;
        foreach (var k in new[] { "Prefix", "Postfix", "Transpiler", "Finalizer", "ReversePatch" })
            if (attrs.Any(a => a.EndsWith("Harmony" + k)) || md.Identifier.Text == k) kind = k.ToLowerInvariant();
        var me = HarmonyArgs(md.AttributeLists);
        if (kind == null && me == null) return null;
        if (cls == null && me == null) return null;
        var ty = me?.Type ?? cls?.Type;
        var meth = me?.Method ?? cls?.Method;
        var sp = me?.Special ?? cls?.Special;
        if (sp == "Constructor") meth = ".ctor";
        else if (sp == "Getter") meth = "get_" + meth;
        else if (sp == "Setter") meth = "set_" + meth;
        return $"{ty}.{meth} {kind ?? "patch"}";
    }

    // ---- syntax normalizer ----
    // Rewrites pure-syntax variants to one form, so C# 9-12 modernizations hash equal:
    //   new T(a) / new(a) -> new(a) (outside `var x =` and member access); new T() / new() / new T[0] / Array.Empty<T>() -> [];
    //   new T { a, b } / new T[] { a } / new[] { a } -> [a, b]; x.ToArray() / x.ToList() -> [.. x];
    //   => bodies -> blocks (return e; or e;); { get => e; } -> { get { return e; } }; default(T) -> default; this.x -> x;
    //   var x = new T(a) -> T x = new(a).
    class Normalizer : CSharpSyntaxRewriter
    {
        static readonly ExpressionSyntax Empty = CollectionExpression();

        public override SyntaxNode VisitLocalDeclarationStatement(LocalDeclarationStatementSyntax node)
        {
            var d = node.Declaration;
            if (d.Type.IsVar && d.Variables.Count == 1 && d.Variables[0].Initializer?.Value is ObjectCreationExpressionSyntax oc)
                node = node.WithDeclaration(d.WithType(oc.Type.WithoutTrivia()));
            return base.VisitLocalDeclarationStatement(node);
        }

        public override SyntaxNode VisitObjectCreationExpression(ObjectCreationExpressionSyntax node)
        {
            var parent = node.Parent;
            var v = (ObjectCreationExpressionSyntax)base.VisitObjectCreationExpression(node);
            // drop the type only where C# can target-type `new(..)`, so a changed type elsewhere still shows
            bool targetTyped = parent is EqualsValueClauseSyntax { Parent: not VariableDeclaratorSyntax { Parent: VariableDeclarationSyntax { Type.IsVar: true } } } ||
                               parent is ReturnStatementSyntax || parent is ArrowExpressionClauseSyntax || parent is ArgumentSyntax ||
                               parent is AssignmentExpressionSyntax a && a.Right == node || parent is InitializerExpressionSyntax;
            bool typeNeeded = !targetTyped;
            if (typeNeeded) return v;
            return Implicit(v.ArgumentList, v.Initializer);
        }

        public override SyntaxNode VisitImplicitObjectCreationExpression(ImplicitObjectCreationExpressionSyntax node)
        {
            var v = (ImplicitObjectCreationExpressionSyntax)base.VisitImplicitObjectCreationExpression(node);
            return Implicit(v.ArgumentList, v.Initializer);
        }

        static ExpressionSyntax Implicit(ArgumentListSyntax args, InitializerExpressionSyntax init)
        {
            bool noArgs = args == null || args.Arguments.Count == 0;
            if (noArgs && init == null) return Empty;
            if (noArgs && init != null && init.IsKind(SyntaxKind.CollectionInitializerExpression) &&
                init.Expressions.All(e => e is not InitializerExpressionSyntax))
                return CollectionExpression(SeparatedList<CollectionElementSyntax>(init.Expressions.Select(e => ExpressionElement(e))));
            return ImplicitObjectCreationExpression(args ?? ArgumentList(), init);
        }

        public override SyntaxNode VisitArrayCreationExpression(ArrayCreationExpressionSyntax node)
        {
            var v = (ArrayCreationExpressionSyntax)base.VisitArrayCreationExpression(node);
            if (v.Initializer != null)
                return CollectionExpression(SeparatedList<CollectionElementSyntax>(v.Initializer.Expressions.Select(e => ExpressionElement(e))));
            var ranks = v.Type.RankSpecifiers;
            if (ranks.Count == 1 && ranks[0].Sizes.Count == 1 && ranks[0].Sizes[0] is LiteralExpressionSyntax l && l.Token.ValueText == "0")
                return Empty;
            return v;
        }

        public override SyntaxNode VisitImplicitArrayCreationExpression(ImplicitArrayCreationExpressionSyntax node)
        {
            var v = (ImplicitArrayCreationExpressionSyntax)base.VisitImplicitArrayCreationExpression(node);
            return CollectionExpression(SeparatedList<CollectionElementSyntax>(v.Initializer.Expressions.Select(e => ExpressionElement(e))));
        }

        public override SyntaxNode VisitInvocationExpression(InvocationExpressionSyntax node)
        {
            var v = (InvocationExpressionSyntax)base.VisitInvocationExpression(node);
            if (v.ArgumentList.Arguments.Count == 0 && v.Expression is MemberAccessExpressionSyntax ma)
            {
                var n = ma.Name.Identifier.Text;
                if ((n == "ToArray" || n == "ToList") && ma.Name is IdentifierNameSyntax)
                    return CollectionExpression(SingletonSeparatedList<CollectionElementSyntax>(SpreadElement(ma.Expression)));
                if (n == "Empty" && ma.Name is GenericNameSyntax && ma.Expression.ToString() == "Array")
                    return Empty;
            }
            return v;
        }

        public override SyntaxNode VisitDefaultExpression(DefaultExpressionSyntax node) =>
            LiteralExpression(SyntaxKind.DefaultLiteralExpression, Token(SyntaxKind.DefaultKeyword));

        public override SyntaxNode VisitMemberAccessExpression(MemberAccessExpressionSyntax node)
        {
            var v = (MemberAccessExpressionSyntax)base.VisitMemberAccessExpression(node);
            if (v.Expression is ThisExpressionSyntax)
                return v.Name;
            return v;
        }

        public override SyntaxNode VisitMethodDeclaration(MethodDeclarationSyntax node)
        {
            var v = (MethodDeclarationSyntax)base.VisitMethodDeclaration(node);
            if (v.ExpressionBody == null) return v;
            bool isVoid = v.ReturnType is PredefinedTypeSyntax p && p.Keyword.IsKind(SyntaxKind.VoidKeyword);
            return v.WithExpressionBody(null).WithSemicolonToken(default).WithBody(Body(v.ExpressionBody.Expression, !isVoid));
        }

        public override SyntaxNode VisitConstructorDeclaration(ConstructorDeclarationSyntax node)
        {
            var v = (ConstructorDeclarationSyntax)base.VisitConstructorDeclaration(node);
            if (v.ExpressionBody == null) return v;
            return v.WithExpressionBody(null).WithSemicolonToken(default).WithBody(Body(v.ExpressionBody.Expression, false));
        }

        public override SyntaxNode VisitPropertyDeclaration(PropertyDeclarationSyntax node)
        {
            var v = (PropertyDeclarationSyntax)base.VisitPropertyDeclaration(node);
            if (v.ExpressionBody == null) return v;
            var get = AccessorDeclaration(SyntaxKind.GetAccessorDeclaration, Body(v.ExpressionBody.Expression, true));
            return v.WithExpressionBody(null).WithSemicolonToken(default).WithAccessorList(AccessorList(SingletonList(get)));
        }

        public override SyntaxNode VisitAccessorDeclaration(AccessorDeclarationSyntax node)
        {
            var v = (AccessorDeclarationSyntax)base.VisitAccessorDeclaration(node);
            if (v.ExpressionBody == null) return v;
            return v.WithExpressionBody(null).WithSemicolonToken(default)
                    .WithBody(Body(v.ExpressionBody.Expression, v.IsKind(SyntaxKind.GetAccessorDeclaration)));
        }

        static BlockSyntax Body(ExpressionSyntax e, bool ret) =>
            Block(ret ? ReturnStatement(e) : ExpressionStatement(e));
    }
}
