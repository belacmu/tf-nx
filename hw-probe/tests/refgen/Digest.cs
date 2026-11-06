// The gameplay digest (hw-probe/exlaunch/source/program/digest.hpp, docs/ws-parity.md section 3) in C#: this class is meant to be
// copied into the browser's TF.EX patch, which fills DigestInput from the Level at GGRS's save and hands Compute()'s value to
// ggrs-ffi as the frame's checksum. Run: refgen digest (prints tests/digest-ref.txt).
using System;
using System.Collections.Generic;

public struct DigestSeat { public bool Alive; public float X, Y, SX, SY; }
public class DigestInput {
    public ulong[] Rng = new ulong[4];              // DeterministicRandom's s0..s3 (TF.State Rng model S0..S3)
    public DigestSeat[] Seat = new DigestSeat[4];   // seat = Player.PlayerIndex; Alive = in Level[GameTags.Player] && !Dead
    public List<(float X, float Y)> Arrows = new List<(float, float)>();   // Level[GameTags.Arrow], any order
}

public static class GameplayDigest {
    public const uint Version = 1;
    static uint Bits(float f) => BitConverter.SingleToUInt32Bits(f);
    public static ulong Compute(DigestInput d) {
        ulong h = 1469598103934665603UL;
        void Byte(byte b) { h = (h ^ b) * 1099511628211UL; }
        void U32(uint v) { for (int i = 0; i < 4; i++) Byte((byte)(v >> (8 * i))); }
        void U64(ulong v) { for (int i = 0; i < 8; i++) Byte((byte)(v >> (8 * i))); }
        U32(Version);
        for (int i = 0; i < 4; i++) U64(d.Rng[i]);
        for (int s = 0; s < 4; s++) {
            DigestSeat p = d.Seat[s];
            Byte(p.Alive ? (byte)1 : (byte)0);
            if (p.Alive) { U32(Bits(p.X)); U32(Bits(p.Y)); U32(Bits(p.SX)); U32(Bits(p.SY)); }
        }
        U32((uint)d.Arrows.Count);
        var pos = new List<ulong>();
        foreach (var a in d.Arrows) pos.Add(((ulong)Bits(a.X) << 32) | Bits(a.Y));
        pos.Sort();
        foreach (ulong v in pos) U64(v);
        return h;
    }

    // Reference cases (same generator as digest-test.cpp).
    public static void Run() {
        ulong x = 0x2545F4914F6CDD1DUL;
        ulong R() { x ^= x << 13; x ^= x >> 7; x ^= x << 17; return x; }
        float F() { return (float)((long)(R() % 640000) - 160000) / 1000f; }
        for (int c = 0; c < 40; c++) {
            var d = new DigestInput();
            for (int i = 0; i < 4; i++) d.Rng[i] = R();
            for (int s = 0; s < 4; s++) d.Seat[s] = new DigestSeat { Alive = R() % 3 != 0, X = F(), Y = F(), SX = F(), SY = F() };
            int n = (int)(R() % (c < 20 ? 6UL : 40UL));
            for (int k = 0; k < n; k++) d.Arrows.Add((F(), F()));
            if (c == 7) d.Arrows.Add(d.Arrows.Count > 0 ? d.Arrows[0] : (1f, 2f));          // a duplicate position
            if (c == 9 && d.Arrows.Count > 0) d.Arrows[0] = (-0f, d.Arrows[0].Item2);         // negative zero
            Console.WriteLine($"digest {c} {Compute(d):x16}");
        }
    }
}
