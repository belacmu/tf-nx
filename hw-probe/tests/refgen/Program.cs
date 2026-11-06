// See refgen.csproj. Output format = hw-probe/tests/teamspawn-test.cpp's.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Reflection;

struct V2 { public float X, Y; public V2(float x, float y) { X = x; Y = y; } }

static class Program {
    static Type _rngType;
    static Random NewGameplay(int seed) => (Random)Activator.CreateInstance(_rngType, seed);   // DeterministicRandom(int)

    // Monocle.Calc.Shuffle<T>(List<T>, Random) (PC 1.3.3.3, Monocle/Calc.cs:432), verbatim.
    static void Shuffle<T>(List<T> list, Random random) {
        int num = list.Count;
        while (--num > 0) {
            T value = list[num];
            int index;
            list[num] = list[index = random.Next(num + 1)];
            list[index] = value;
        }
    }
    // TF.State.TowerFallExtensions.CalcExtensions.OwnShuffle / OwnShuffledIndexes, with Calc.Random = the gameplay generator.
    static IEnumerable<int> OwnShuffledIndexes(int count, Random calcRandom) {
        List<int> list = Enumerable.Range(0, count).ToList();
        Shuffle(list, calcRandom);
        return list;
    }
    // TF.State.Patchs.RoundLogic.RoundLogicPatch.ApplyOrder, verbatim.
    static List<V2> ApplyOrder(List<V2> spawns, IEnumerable<int> order) =>
        (from index in order where index < spawns.Count select spawns[index]).ToList();

    static string F(float f) => f.ToString("R", CultureInfo.InvariantCulture);
    static string L(List<V2> l) => string.Join(" ", l.Select(v => "(" + F(v.X) + "," + F(v.Y) + ")"));

    static void Main(string[] args) {
        if (args[0] == "digest") { GameplayDigest.Run(); return; }
        _rngType = Assembly.LoadFrom(args[0]).GetType("TF.State.Domain.Randomness.DeterministicRandom", true);
        int[] seeds = { 0, 1, 42, 12345, -1, int.MaxValue, 987654321 };
        int[][] sizes = { new[] { 1, 1 }, new[] { 2, 2 }, new[] { 3, 2 }, new[] { 2, 3 }, new[] { 4, 4 }, new[] { 6, 6 }, new[] { 1, 3 }, new[] { 0, 2 }, new[] { 5, 1 } };
        foreach (int seed in seeds) {
            foreach (int[] sz in sizes) {
                Random r = NewGameplay(seed);
                var A = new List<V2>(); var B = new List<V2>();
                for (int i = 0; i < sz[0]; i++) A.Add(new V2(20f + 13.5f * i, 40f + 30f * i));
                for (int i = 0; i < sz[1]; i++) B.Add(new V2(300f - 13.5f * i, 40f + 30f * i));
                IEnumerable<int> order = OwnShuffledIndexes(Math.Max(A.Count, B.Count), r);
                List<V2> a2 = ApplyOrder(A, order), b2 = ApplyOrder(B, order);
                Console.WriteLine($"teams seed {seed} A {sz[0]} B {sz[1]}: order {string.Join(" ", order)}");
                Console.WriteLine($"  A {L(a2)}");
                Console.WriteLine($"  B {L(b2)}");
                Console.WriteLine($"  next {r.Next()}");
            }
            // SpikeballPatch postfix: spinDir = Calc.Choose<int>(Calc.Random, new int[2] { 1, -1 }) = choices[random.Next(2)].
            Random s = NewGameplay(seed);
            var dirs = new List<int>();
            for (int i = 0; i < 12; i++) { int[] choices = { 1, -1 }; dirs.Add(choices[s.Next(choices.Length)]); }
            Console.WriteLine($"spindir seed {seed}: {string.Join(" ", dirs)}");
        }
    }
}
