// oracle <in dir> <out dir>: each <name>.json holds one lobby-server message ({"<Message>": {...}}); writes <name>.json to <out dir>
// as TF.EX re-serializes it (the message class is <Message>Message in TF.EX.Domain.Models.WebSocket.Server or .Client).
using System.Reflection;
using System.Text.Json;
using MessagePack;

static class Oracle
{
    static int Main(string[] args)
    {
        var types = typeof(Oracle).Assembly.GetTypes().Where(t => t.Namespace?.StartsWith("TF.EX.Domain.Models.WebSocket") == true)
                                  .GroupBy(t => t.Name).ToDictionary(g => g.Key, g => g.ToList());
        Directory.CreateDirectory(args[1]);
        int n = 0, bad = 0, skipped = 0;
        foreach (var f in Directory.EnumerateFiles(args[0], "*.json").OrderBy(x => x))
        {
            var json = File.ReadAllText(f);
            string name;
            using (var d = JsonDocument.Parse(json)) name = d.RootElement.EnumerateObject().First().Name;
            if (!types.TryGetValue(name + "Message", out var ts)) { Console.Error.WriteLine($"{Path.GetFileName(f)}: no type {name}Message (TF.EX handles it by name): skipped"); skipped++; continue; }
            var t = ts[0];
            try
            {
                var obj = MessagePackSerializer.Deserialize(t, MessagePackSerializer.ConvertFromJson(json));
                File.WriteAllText(Path.Combine(args[1], Path.GetFileName(f)), MessagePackSerializer.ConvertToJson(MessagePackSerializer.Serialize(t, obj)));
                n++;
            }
            catch (Exception e)
            {
                // a message whose body type has no members ({"GetLobbies":{}}): TF.EX never reads one, it sends Serialize(new T())
                try
                {
                    File.WriteAllText(Path.Combine(args[1], Path.GetFileName(f)), MessagePackSerializer.ConvertToJson(MessagePackSerializer.Serialize(t, Activator.CreateInstance(t))));
                    n++;
                }
                catch { Console.Error.WriteLine($"{Path.GetFileName(f)}: {e.GetType().Name}: {e.Message}"); bad++; }
            }
        }
        Console.Error.WriteLine($"oracle: {n} messages, {skipped} skipped, {bad} failed");
        return bad == 0 ? 0 : 1;
    }
}
