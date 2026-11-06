// TF.EX's lobby tower and level selection, v0.19.2 and v0.21.0, written from TF.EX's code (vendor/tfex) and the PC game's
// (TowerFall.exe 1.3.3.3, ilspycmd), run on .NET's own System.Random (new Random(seed) is the legacy Knuth generator on every .NET).
// docs/ws-tfex-lockstep.md section 3 has what this checks.
//
//   dotnet run -- <PC Content dir> <PC DarkWorldContent dir> [seeds...]
// Prints, per seed and unlock set: the tower TF.EX 0.19.2 and 0.21.0 play for MapId -1 (random), and the first levels of the
// level order (round order) for that tower under both versions, for 2 players (Last Man Standing).
using System.Xml;

static class Model
{
    // Constants.NETPLAY_SAFE_MAP (TF.EX.Domain/Models/Constants.cs): every official title; the PC's VersusTowers are IDs 0..15 in this order
    static readonly string[] Titles = { "SACRED GROUND", "TWILIGHT SPIRE", "BACKFIRE", "FLIGHT", "MIRAGE", "THORNWOOD", "FROSTFANG KEEP",
        "KING'S COURT", "SUNKEN CITY", "MOONSTONE", "TOWERFORGE", "ASCENSION", "THE AMARANTH", "DREADWOOD", "DARKFANG", "CATACLYSM" };

    // Monocle.Calc.Shuffle(List<T>, Random)
    static void Shuffle<T>(List<T> list, Random random)
    {
        int num = list.Count;
        while (--num > 0) { T value = list[num]; int index; list[num] = list[index = random.Next(num + 1)]; list[index] = value; }
    }

    // UnlockData.GetTowerUnlocked (PC): 0-7 and 12-15 true, 8 SunkenCity, 9 Moonstone, 10 TowerForge, 11 Ascension.
    // unlocks bit 0 SunkenCity, 1 Moonstone, 2 TowerForge, 3 Ascension.
    static bool TowerUnlocked(int i, int unlocks) => i switch { 8 => (unlocks & 1) != 0, 9 => (unlocks & 2) != 0, 10 => (unlocks & 4) != 0, 11 => (unlocks & 8) != 0, _ => true };

    // MapScene.InitVersusButtons (+ v0.21.0 InitVersusButtons_Prefix: the four unlocked while it runs in netplay), then
    // MapScenePatch.Begin_Postfix / GetRandomVersusTower postfix. noRandom: the tower ids marked NoRandom (v0.21.0 clears them).
    public static int Tower(string version, int mapId, int seed, int unlocks, ISet<int> noRandom)
    {
        bool v21 = version == "0.21.0";
        if (v21) { unlocks = 0xf; noRandom = new HashSet<int>(); }
        var buttons = Enumerable.Range(0, 16).Where(i => TowerUnlocked(i, unlocks)).ToList();
        if (mapId != -1 && buttons.Contains(mapId)) return mapId;              // Begin_Postfix: the button with ID.X == MapId
        var list = buttons.Where(i => Titles.Contains(Titles[i])).ToList();    // VersusMapButtons, netplay-safe titles (DLC: none locked)
        if (list.Any(i => !noRandom.Contains(i))) list.RemoveAll(noRandom.Contains);
        Shuffle(list, new Random(seed));                                       // StateApi.ResetRng(); Calc.Shuffle(list, new Random(GetSeed()))
        return list[0];
    }

    // Wide (TF.EX + Teuria.WiderSet 0.9.0): the map screen shows only WiderSet's 16 registered towers, IDs 16..31 in folder order,
    // with the official titles (all netplay-safe, none locked: GetTowerUnlocked(16..31) is the default `true`). Begin_Postfix: the button
    // with ID.X == MapId (so MapId 0..15 match nothing: RANDOM), else GetRandomVersusTower = Calc.Shuffle(new Random(seed)) over them.
    public static int WideTower(int mapId, int seed)
    {
        var ids = Enumerable.Range(16, 16).ToList();
        if (ids.Contains(mapId)) return mapId;
        Shuffle(ids, new Random(seed));
        return ids[0];
    }

    // VersusTowerData ctor (PC): Directory.GetFiles(directory, "*.oel") sorted (Array.Sort), then the DLC directory's, sorted.
    public static List<(string path, int spawns)> Levels(string content, string dwContent, int tower, out bool fixedFirst)
    {
        string folder = Directory.GetDirectories(Path.Combine(content, "Levels", "Versus")).Select(Path.GetFileName).Single(f => f.StartsWith($"{tower:00} - "));
        string dir = Path.Combine(content, "Levels", "Versus", folder);
        var xml = new XmlDocument(); xml.Load(Path.Combine(dir, "tower.xml"));
        fixedFirst = xml["tower"]?["fixedFirst"] != null;
        var res = new List<(string, int)>();
        foreach (var d in new[] { dir, Path.Combine(dwContent, "Levels", "Versus", folder) })
        {
            if (!Directory.Exists(d)) continue;
            var files = Directory.GetFiles(d, "*.oel", SearchOption.TopDirectoryOnly);
            Array.Sort(files);
            foreach (var f in files)
            {
                var lx = new XmlDocument(); lx.Load(f);
                res.Add((f, lx["level"]["Entities"].GetElementsByTagName("PlayerSpawn").Count));
            }
        }
        return res;
    }

    // v0.21.0 VersusLevelSystemExtensions.SortByFolderAndName
    static List<string> SortByFolderAndName(List<string> levels)
    {
        var folders = levels.Select(level => Path.GetDirectoryName(level)).Distinct().ToList();
        return [.. levels.OrderBy(level => folders.IndexOf(Path.GetDirectoryName(level))).ThenBy(level => Path.GetFileName(level), StringComparer.OrdinalIgnoreCase)];
    }

    // TF.State's OwnGenLevel (first generation: lastLevel == null), on GetLevels' list (FortRise mode 11: PlayerSpawns >= PlayerAmount)
    public static List<string> LevelOrder(string version, List<(string path, int spawns)> all, bool fixedFirst, int seed, int players)
    {
        var levels = all.Where(l => l.spawns >= players).Select(l => l.path).ToList();
        if (version == "0.21.0") levels = SortByFolderAndName(levels).Select(level => level.Replace('/', '\\')).ToList();
        if (fixedFirst)
        {
            string item = levels[0]; levels.RemoveAt(0);
            Shuffle(levels, new Random(seed));
            levels.Insert(0, item);
            return levels;
        }
        Shuffle(levels, new Random(seed));
        return levels;
    }

    static string Names(IEnumerable<string> l) => string.Join(" ", l.Select(p => Path.GetFileNameWithoutExtension(p.Replace('\\', '/'))));

    static int Main(string[] args)
    {
        string content = args[0], dw = args[1];
        var seeds = args.Skip(2).Select(int.Parse).ToList();
        if (seeds.Count == 0) seeds = [109144773, 109144726, 1288949485, 42, 7, 123456789, -5, int.MaxValue];
        bool allSameOrder = true;
        foreach (int seed in seeds)
        {
            foreach (int unlocks in new[] { 0xf, 0x0, 0x1, 0x6 })
            {
                int t19 = Tower("0.19.2", -1, seed, unlocks, new HashSet<int>()), t21 = Tower("0.21.0", -1, seed, unlocks, new HashSet<int>());
                Console.WriteLine($"seed {seed} unlocks {unlocks:x}: random tower 0.19.2 {t19} ({Titles[t19]}), 0.21.0 {t21} ({Titles[t21]})");
            }
            int tn = Tower("0.19.2", -1, seed, 0xf, new HashSet<int> { 0, 1, 2 });
            Console.WriteLine($"seed {seed}: 0.19.2 with NoRandom {{0,1,2}}: {tn} ({Titles[tn]})");
            for (int tower = 0; tower < 16; tower++)
            {
                var all = Levels(content, dw, tower, out bool ff);
                var o19 = LevelOrder("0.19.2", all, ff, seed, 2); var o21 = LevelOrder("0.21.0", all, ff, seed, 2);
                bool same = Names(o19) == Names(o21); allSameOrder &= same;
                if (tower == Tower("0.21.0", -1, seed, 0xf, new HashSet<int>()) || !same)
                    Console.WriteLine($"seed {seed} tower {tower}{(ff ? " (fixedFirst)" : "")}: levels {Names(o21)}{(same ? " (0.19.2: same)" : $" | 0.19.2: {Names(o19)}")}");
            }
        }
        Console.WriteLine(allSameOrder ? "level order: 0.21.0 == 0.19.2 for every seed and tower" : "level order: DIFFERS");
        foreach (int seed in seeds.Concat(new[] { 1004 }).Distinct())
            Console.WriteLine($"wide seed {seed}: MapId -1 -> {WideTower(-1, seed)} ({Titles[WideTower(-1, seed) - 16]}), MapId 0 -> {WideTower(0, seed)}, MapId 17 -> {WideTower(17, seed)}");
        return 0;
    }
}
