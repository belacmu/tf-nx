using System;
using Microsoft.Xna.Framework;
using Monocle;
using MonoMod.Utils;

namespace NetplayModeTest;

/// <summary>An entity of the mode's own: it drifts for 200 frames and goes (the round makes one every 90 frames).</summary>
public sealed class ModTestMarker : Entity
{
    private Vector2 velocity;
    private readonly Counter life = new Counter();

    public ModTestMarker()
    {
    }

    public ModTestMarker(Vector2 position, Vector2 velocity) : base(position)
    {
        this.velocity = velocity;
        life.Set(200);
    }

    private static int addedLogged;

    public override void Added()
    {
        base.Added();
        if (addedLogged++ < 6)   // (the first few: the depth the layer gave it, Switch against the PC)
        {
            ModTestModule.Log($"marker added: depth {Depth}, actualDepth bits {BitConverter.DoubleToInt64Bits(DynamicData.For(this).Get<double>("actualDepth"))}");
        }
    }

    public override void Update()
    {
        base.Update();
        Position += velocity;
        life.Update();
        if (!life)
        {
            RemoveSelf();
        }
    }

    public byte[] SaveState() => Text(BitConverter.SingleToInt32Bits(Position.X), BitConverter.SingleToInt32Bits(Position.Y),
        BitConverter.SingleToInt32Bits(velocity.X), BitConverter.SingleToInt32Bits(velocity.Y), BitConverter.SingleToInt32Bits(GetCounter(life)));

    public void LoadState(byte[] bytes)
    {
        var values = Values(bytes);
        Position = new Vector2(BitConverter.Int32BitsToSingle(values[0]), BitConverter.Int32BitsToSingle(values[1]));
        velocity = new Vector2(BitConverter.Int32BitsToSingle(values[2]), BitConverter.Int32BitsToSingle(values[3]));
        SetCounter(life, BitConverter.Int32BitsToSingle(values[4]));
    }

    // the state as little-endian ints, each float by its bits: exact, and nothing a translator of the mod needs beyond arithmetic
    internal static byte[] Text(params int[] values)
    {
        var bytes = new byte[values.Length * 4];
        for (int i = 0; i < values.Length; i++)
        {
            bytes[i * 4] = (byte)values[i];
            bytes[i * 4 + 1] = (byte)(values[i] >> 8);
            bytes[i * 4 + 2] = (byte)(values[i] >> 16);
            bytes[i * 4 + 3] = (byte)(values[i] >> 24);
        }
        return bytes;
    }

    internal static int[] Values(byte[] bytes)
    {
        var values = new int[bytes.Length / 4];
        for (int i = 0; i < values.Length; i++)
        {
            values[i] = bytes[i * 4] | (bytes[i * 4 + 1] << 8) | (bytes[i * 4 + 2] << 16) | (bytes[i * 4 + 3] << 24);
        }
        return values;
    }

    internal static float GetCounter(Counter counter) => DynamicData.For(counter).Get<float>("counter");

    internal static void SetCounter(Counter counter, float value) => DynamicData.For(counter).Set("counter", value);
}
