// Game members the generated code uses -> tools/tfex/bind.py (tools/re over the Switch binary) -> tools/tfex/binding.json (committed:
// the binary is fixed) -> tfexgen_binding.hpp (constexpr offsets in namespace tfex::gen::bind).
using System.Diagnostics;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace TfexTranslator;

public sealed class Binder
{
    public sealed record Request(string Key, string Kind, string Type, string Member, string Sig, int Overload, int Count, int OverloadPub = -1, int CountPub = 0);
    readonly Dictionary<string, Request> requests = new();
    public JsonObject Binding = new();
    readonly string bindingPath, bindPy;

    /* translate.toml [pin]: "<Type>.<Member>(<sig>)" = { body = 0x..., why = "..." } for members the binder can't pick by name and
     * metadata order (hand rules: each is in the report) */
    public Dictionary<string, (string what, long value, string why)> Pins = new();
    public readonly List<string> PinsUsed = new();
    /* a pin with both `body` and `slot` (a virtual method): the slot, for its vtable-slot request (kind v) */
    public Dictionary<string, (string what, long value, string why)> SlotPins = new();

    public Binder(string tfexDir)
    {
        bindingPath = Path.Combine(tfexDir, "binding.json");
        bindPy = Path.Combine(tfexDir, "bind.py");
        if (File.Exists(bindingPath)) Binding = JsonNode.Parse(File.ReadAllText(bindingPath)) as JsonObject ?? new();
    }

    static string Ident(string s)
    {
        var sb = new StringBuilder();
        foreach (var c in s) sb.Append(char.IsLetterOrDigit(c) ? c : '_');
        return sb.ToString();
    }

    /* the C++ name of a binding: bind::<kind>_<Type>_<Member>[_<overload> | _<signature hash>] (stable across runs) */
    /* a binding the hand-written runtime refers to by a fixed name */
    public void NeedNamed(string key, string kind, string type, string member, string sig, int overload, int count) =>
        requests[key] = new Request(key, kind, type, member, sig, overload, count);

    public string Need(string kind, string type, string member = "", string sig = "", int overload = -1, int count = 0, int overloadPub = -1, int countPub = 0)
    {
        var shortType = type.Split('[')[0].Split('.').Last() + (type.Contains('[') ? "_" + Fnv(type) : "");
        var key = $"{kind}_{Ident(shortType)}" + (member.Length > 0 ? "_" + Ident(member) : "") +
                  (overload >= 0 && count > 1 ? "_" + overload : sig.Length > 0 ? "_" + Fnv(sig) : "");
        requests[key] = new Request(key, kind, type, member, sig, overload, count, overloadPub, countPub);
        OnNeed?.Invoke(key);
        return "bind::" + key;
    }

    public void ClearRequests() => requests.Clear();
    public Action<string> OnNeed;
    public string ErrorOf(string key) => !Binding.ContainsKey(key) ? "not bound" : Binding[key]?["error"]?.GetValue<string>();

    static string Fnv(string s)
    {
        uint h = 2166136261;
        foreach (var c in s) { h ^= c; h *= 16777619; }
        return (h & 0xffff).ToString("x4");
    }

    /* a request's pin: its own, or for a field FortRise's patched TowerFall publishes under a capitalised name (VersusLevelSystem.LastLevel)
     * the game's private spelling's (lastLevel), which bind.py's field records fall back to the same way */
    bool PinFor(Request r, out (string what, long value, string why) pin)
    {
        if (r.Kind == "v" && SlotPins.TryGetValue($"{r.Type}.{r.Member}({r.Sig})", out pin)) return true;
        if (Pins.TryGetValue($"{r.Type}.{r.Member}({r.Sig})", out pin)) return true;
        string m = r.Member;
        return r.Kind == "f" && m.Length > 0 && char.IsUpper(m[0]) && Pins.TryGetValue($"{r.Type}.{char.ToLower(m[0]) + m[1..]}({r.Sig})", out pin);
    }

    /* runs bind.py for requests binding.json lacks (needs tools/re/main.flat); without it, unresolved names stay 0 and are reported */
    public void Resolve()
    {
        foreach (var r in requests.Values)
            if (PinFor(r, out var pin))
            {
                Binding[r.Key] = new JsonObject { [pin.what] = pin.value, ["via"] = "pin: " + pin.why };
                if (!PinsUsed.Contains($"{r.Type}.{r.Member}: {pin.why}")) PinsUsed.Add($"{r.Type}.{r.Member}: {pin.why}");
            }
        var missing = requests.Values.Where(r => !Binding.ContainsKey(r.Key) || Binding[r.Key]?["error"] != null).ToList();
        if (missing.Count == 0) return;
        var tmp = Path.GetTempFileName();
        File.WriteAllText(tmp, JsonSerializer.Serialize(missing.Select(r => new Dictionary<string, object>
        {
            ["key"] = r.Key, ["kind"] = r.Kind switch { "t" or "c" or "d" or "z" or "vb" => "type", "f" => "field", "e" => "enum", "v" => "vslot", _ => "method" },
            ["type"] = r.Type, ["member"] = r.Member, ["sig"] = r.Sig, ["overload"] = r.Overload, ["count"] = r.Count, ["overload_pub"] = r.OverloadPub, ["count_pub"] = r.CountPub,
        })));
        try
        {
            var psi = new ProcessStartInfo("python3", $"\"{bindPy}\" \"{tmp}\" \"{bindingPath}\"") { RedirectStandardError = true };
            using var p = Process.Start(psi);
            Console.Error.Write(p.StandardError.ReadToEnd());
            p.WaitForExit();
            if (File.Exists(bindingPath)) Binding = JsonNode.Parse(File.ReadAllText(bindingPath)) as JsonObject ?? new();
            foreach (var r in requests.Values)
                if (PinFor(r, out var pin)) Binding[r.Key] = new JsonObject { [pin.what] = pin.value, ["via"] = "pin: " + pin.why };
        }
        catch (Exception e) { Console.Error.WriteLine("translator: bind.py not run: " + e.Message); }
        finally { File.Delete(tmp); }
        // the [pin] key of each request still unresolved (TFEX_PIN_KEYS=1): what a pin for it is written as
        if (Environment.GetEnvironmentVariable("TFEX_PIN_KEYS") == "1")
            foreach (var r in requests.Values.Where(r => Binding[r.Key]?["error"] != null))
                Console.Error.WriteLine($"pin key: {r.Key}: \"{r.Type}.{r.Member}({r.Sig})\"");
    }

    /* a game enum's literals on the Switch by name (pass 2), or null; `None` absent from the binary's literal table is 0 */
    public Dictionary<string, long> EnumValues(string switchType)
    {
        var k = Need("e", switchType)[6..];
        if (!Binding.ContainsKey(k) || Binding[k] is not JsonObject o || o["values"] is not JsonObject v) return null;
        return v.ToDictionary(kv => kv.Key, kv => kv.Value.GetValue<long>());
    }

    public List<(string key, string error)> Unresolved = new();
    /* TF.State's state path (the rollback measurement): unresolved members its units use are gaps, not compile errors. A field: an offset
     * gen::Fld turns into a dummy (kGapField + its number, logged as a state gap); a call / type getter / class constructor: the game's
     * `mov x0, xzr; ret` (+0x103c), so a call returns 0 / null */
    public HashSet<string> StateGapOk = new();

    /* a bound value (pass 2), or null */
    public long? Value(string key, string field)
    {
        var k = key.StartsWith("bind::") ? key[6..] : key;
        return Binding.ContainsKey(k) && Binding[k] is JsonObject o && o["error"] == null && o[field] != null ? o[field].GetValue<long>() : null;
    }

    public string Header()
    {
        var sb = new StringBuilder();
        sb.Append("/* GENERATED by tools/tfex/translator: the game members the generated code uses, on the Switch binary (1.4.1.6), resolved by\n" +
                  " * tools/tfex/bind.py (tools/re) and kept in tools/tfex/binding.json. Offsets from the main module. Do not edit. */\n" +
                  "#pragma once\n#include <cstdint>\n\nnamespace tfex::gen::bind {\n");
        Unresolved.Clear();
        foreach (var r in requests.Values.OrderBy(r => r.Key, StringComparer.Ordinal))
        {
            var b = Binding.ContainsKey(r.Key) ? Binding[r.Key] as JsonObject : null;
            string err = b == null ? "not bound (bind.py not run)" : b["error"]?.GetValue<string>();
            string val = null, ty = "uintptr_t";
            if (err == null)
            {
                switch (r.Kind)
                {
                    case "t": val = Hex(b["getter"]); break;
                    case "c": val = Hex(b["cctor"]); break;
                    case "d": val = Hex(b["desc"]); break;
                    case "z": val = Hex(b["size"]); ty = "uint32_t"; break;
                    case "vb": val = Hex(b["vtbytes"]); ty = "uint32_t"; break;
                    case "f":
                        if (b["off"] != null) { val = Hex(b["off"]); ty = "uint32_t"; }
                        else val = Hex(b["static"]);
                        break;
                    case "v": val = Hex(b["slot"]); ty = "uint32_t"; break;
                    case "e": continue;   // enum literals: used at generation time (EnumValues), nothing in C++
                    default: val = Hex(b["body"]); break;
                }
                if (val == null) err = "no value";
            }
            var what = $"{r.Type}{(r.Member.Length > 0 ? "." + r.Member : "")}{(r.Sig.Length > 0 ? "(" + r.Sig + ")" : "")}";
            // unresolved: declared only for the coverage compile; in the module a use doesn't compile (never a call to Base() + 0)
            if (err != null && StateGapOk.Contains(r.Key))
            {
                Unresolved.Add((r.Key, err));
                int gi = Unresolved.Count;
                if (r.Kind == "f") sb.Append($"constexpr uint32_t {r.Key} = 0x7fff0000u + {gi};   /* UNRESOLVED {what}: {err}; a TF.State measurement gap (field #{gi}, a dummy) */\n");
                else sb.Append($"constexpr {ty} {r.Key} = 0x103c;   /* UNRESOLVED {what}: {err}; a TF.State measurement gap (the game's `return 0`) */\n");
                continue;
            }
            if (err != null) { Unresolved.Add((r.Key, err)); sb.Append($"#ifdef TFEXGEN_COVERAGE\nconstexpr {ty} {r.Key} = 0;   /* UNRESOLVED {what}: {err} */\n#else\n/* UNRESOLVED {r.Key}: {what}: {err} (a use is a compile error) */\n#endif\n"); }
            else sb.Append($"constexpr {ty} {r.Key} = {val};   /* {what} */\n");
        }
        sb.Append("} // namespace tfex::gen::bind\n");
        return sb.ToString();
    }

    static string Hex(JsonNode n) => n == null ? null : "0x" + n.GetValue<long>().ToString("x");
}
