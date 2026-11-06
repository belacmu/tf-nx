// TF.EX's custom skins on the Switch (item 11): where TF.EX's SkinStreamService / SkinOverlayService meet .NET and MessagePack-CSharp
// (translate.toml [[rewrite]] / [[replace]], overrides.toml [service]). OUR OWN CODE: the library work is the runtime's
// (tfex/runtime/tfexgen_skins.cpp); TF.EX's services themselves are TF.EX's code, translated.
using System;
using System.Collections.Generic;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;
using TF.EX.Domain.Models.Skin;
using TF.EX.Domain.Services;

namespace TfexGlue
{
    public static class Skins
    {
        // ---- TF.EX's services, one each (the DI singletons on the PC). Their constructor arguments are TF.EX interfaces whose members
        // are [service] rules, so the objects passed don't matter
        static SkinStreamService stream;
        static SkinOverlayService overlay;
        public static SkinStreamService Stream() => stream ??= new SkinStreamService(null);
        public static SkinOverlayService Overlay() => overlay ??= new SkinOverlayService(null, null, null, null);

        // SkinOverlayService.Clone: object.MemberwiseClone through reflection on the PC; the game's own MemberwiseClone here
        [Cpp("gen::Fn<void* (*)(void*)>(bind::m_Object_MemberwiseClone)($0)", Include = "tfexgen_binding.hpp")]
        public static extern TowerFall.ArcherData Clone(TowerFall.ArcherData original);

        // ---- System.Security.Cryptography / System.Convert / System.IO.Compression
        [Cpp("tfex::gen::skins::FromBase64($0)", Include = "tfexgen_skins.hpp")] public static extern byte[] FromBase64(string s);
        [Cpp("tfex::gen::skins::Sha256($0)", Include = "tfexgen_skins.hpp")] public static extern byte[] Sha256(byte[] data);
        [Cpp("tfex::gen::skins::HexUpper($0)", Include = "tfexgen_skins.hpp")] public static extern string HexUpper(byte[] data);
        // SkinStreamService.Deflate: MemoryStream + DeflateStream(CompressionLevel.Optimal) = raw DEFLATE
        [Cpp("tfex::gen::skins::Deflate($0)", Include = "tfexgen_skins.hpp")] public static extern byte[] Deflate(byte[] data);
        [Cpp("tfex::gen::skins::Inflate($0, $1)", Include = "tfexgen_skins.hpp")] static extern int Inflate(byte[] src, byte[] into);

        // SkinStreamService.InflateRgba, TF.EX's checks as they are; its DeflateStream read (exactly `expected` bytes, nothing after) as one
        // raw inflate into a buffer of that size
        public static byte[] InflateRgba(SkinImage image)
        {
            if (image?.DeflatedRgba == null)
            {
                return null;
            }

            var expected = image.Width * image.Height * 4;

            if (expected <= 0 || expected > SkinLimits.MaxTextureDim * SkinLimits.MaxTextureDim * 4)
            {
                return null;
            }

            var output = new byte[expected];
            return Inflate(image.DeflatedRgba, output) == expected ? output : null;
        }

        // ---- FNA's Texture2D: the game's GetData<T>(level, rect, data, start, count) pair (+0xb8bf0 bounds, +0x9106e0 the read, as its
        // GetData<Color>(Color[]) calls them) and its SetData<Color>(Color[]) (the Switch has those instantiations)
        [Cpp("tfex::gen::skins::GetRect($0, $1, $2, $3, $4, $5, $6, $7, $8, $9)", Include = "tfexgen_skins.hpp")]
        static extern void GetRect(Texture2D texture, int level, bool hasRect, int x, int y, int w, int h, Color[] data, int start, int count);
        [Cpp("tfex::gen::skins::SetRgba($0, $1, $2)", Include = "tfexgen_skins.hpp")]
        static extern void SetRgba(Texture2D texture, byte[] rgba, Color[] colors);

        // Texture2D.GetData<Color>(level, rect, data, start, count) (SkinStreamService.FromTextureRegion): only the rect is read
        public static void GetData(Texture2D texture, int level, Rectangle? rect, Color[] data, int start, int count)
        {
            var r = rect ?? new Rectangle(0, 0, 0, 0);
            GetRect(texture, level, rect.HasValue, r.X, r.Y, r.Width, r.Height, data, start, count);
        }

        // Texture2D.SetData<Color>(colors) (Teams+'s Recolor): the game's own expansion of it
        [Cpp("tfex::gen::skins::SetColors($0, $1)", Include = "tfexgen_skins.hpp")]
        public static extern void SetColors(Texture2D texture, Color[] colors);
        // Texture2D.SetData<byte>(rgba) (SkinOverlayService.CreateSubtexture): as Colors (the same bytes)
        public static void SetData(Texture2D texture, byte[] rgba)
        {
            var colors = new Color[rgba.Length / 4];
            SetRgba(texture, rgba, colors);
        }

        // ---- MessagePack-CSharp: TF.EX's ArcherSkinBundle and its parts are [MessagePackObject] with integer keys, so each is an array of
        // its keys' values in order (null: nil); byte[] bin, int[] / List arrays, Dictionary<string, string> a map, float float32
        [Cpp("tfex::gen::skins::MpBegin()", Include = "tfexgen_skins.hpp")] static extern void Begin();
        [Cpp("tfex::gen::skins::MpArray($0)", Include = "tfexgen_skins.hpp")] static extern void Array(int n);
        [Cpp("tfex::gen::skins::MpMap($0)", Include = "tfexgen_skins.hpp")] static extern void Map(int n);
        [Cpp("tfex::gen::skins::MpNil()", Include = "tfexgen_skins.hpp")] static extern void Nil();
        [Cpp("tfex::gen::skins::MpBool($0)", Include = "tfexgen_skins.hpp")] static extern void Bool(bool v);
        [Cpp("tfex::gen::skins::MpInt($0)", Include = "tfexgen_skins.hpp")] static extern void Int(long v);
        [Cpp("tfex::gen::skins::MpF32($0)", Include = "tfexgen_skins.hpp")] static extern void F32(float v);
        [Cpp("tfex::gen::skins::MpStr($0)", Include = "tfexgen_skins.hpp")] static extern void Str(string v);
        [Cpp("tfex::gen::skins::MpBin($0)", Include = "tfexgen_skins.hpp")] static extern void Bin(byte[] v);
        [Cpp("tfex::gen::skins::MpEnd()", Include = "tfexgen_skins.hpp")] static extern byte[] End();
        [Cpp("tfex::gen::skins::MpOpen($0)", Include = "tfexgen_skins.hpp")] static extern void Open(byte[] bytes);
        [Cpp("tfex::gen::skins::MpNil_()", Include = "tfexgen_skins.hpp")] static extern bool IsNil();
        [Cpp("tfex::gen::skins::MpReadArray()", Include = "tfexgen_skins.hpp")] static extern int ReadArray();
        [Cpp("tfex::gen::skins::MpReadMap()", Include = "tfexgen_skins.hpp")] static extern int ReadMap();
        [Cpp("tfex::gen::skins::MpReadBool()", Include = "tfexgen_skins.hpp")] static extern bool ReadBool();
        [Cpp("tfex::gen::skins::MpReadInt()", Include = "tfexgen_skins.hpp")] static extern long ReadInt();
        [Cpp("tfex::gen::skins::MpReadF32()", Include = "tfexgen_skins.hpp")] static extern float ReadF32();
        [Cpp("tfex::gen::skins::MpReadStr()", Include = "tfexgen_skins.hpp")] static extern string ReadStr();
        [Cpp("tfex::gen::skins::MpReadBin()", Include = "tfexgen_skins.hpp")] static extern byte[] ReadBin();
        [Cpp("tfex::gen::skins::MpSkip()", Include = "tfexgen_skins.hpp")] static extern void Skip();
        [Cpp("tfex::gen::skins::MpOk()", Include = "tfexgen_skins.hpp")] static extern bool Ok();

        // MessagePackSerializer.Serialize(bundle)
        public static byte[] Serialize(ArcherSkinBundle b)
        {
            Begin();
            Array(29);
            Str(b.CustomArcherId); Str(b.Name0); Str(b.Name1); Int(b.ColorA); Int(b.ColorB);
            WriteImage(b.PortraitNotJoined); WriteImage(b.PortraitJoined); WriteImage(b.PortraitWin); WriteImage(b.PortraitLose); WriteImage(b.Aimer);
            WriteSprite(b.Body); WriteSprite(b.HeadNormal); WriteSprite(b.HeadNoHat); WriteSprite(b.HeadCrown); WriteSprite(b.HeadBack); WriteSprite(b.Bow);
            WriteImage(b.HatNormal); WriteImage(b.HatBlue); WriteImage(b.HatRed);
            WriteHair(b.Hair);
            Bool(b.HasBreathing); Int(b.BreathingInterval); F32(b.BreathingOffsetX); F32(b.BreathingOffsetY); F32(b.BreathingDuckingOffsetX); F32(b.BreathingDuckingOffsetY);
            Int(b.SleepHeadFrame);
            WriteSprite(b.GemMenu); WriteSprite(b.GemGameplay);
            return End();
        }

        static void WriteImage(SkinImage i)
        {
            if (i == null) { Nil(); return; }
            Array(3); Int(i.Width); Int(i.Height); Bin(i.DeflatedRgba);
        }

        static void WriteInts(int[] a)
        {
            if (a == null) { Nil(); return; }
            Array(a.Length);
            for (int i = 0; i < a.Length; i++) Int(a[i]);
        }

        static void WriteSprite(SkinSprite s)
        {
            if (s == null) { Nil(); return; }
            Array(15);
            WriteImage(s.Sheet); Int(s.FrameWidth); Int(s.FrameHeight); F32(s.OriginX); F32(s.OriginY);
            if (s.Animations == null) Nil();
            else
            {
                Array(s.Animations.Count);
                for (int i = 0; i < s.Animations.Count; i++)
                {
                    var a = s.Animations[i];
                    if (a == null) { Nil(); continue; }
                    Array(4); Str(a.Id); F32(a.Delay); Bool(a.Loop); WriteInts(a.Frames);
                }
            }
            WriteImage(s.BlueSheet); WriteImage(s.RedSheet);
            WriteInts(s.HeadXOrigins); WriteInts(s.HeadYOrigins); WriteInts(s.BowXOffsets); WriteInts(s.BowYOffsets);
            Int(s.X); Int(s.Y);
            if (s.ExtraData == null) Nil();
            else
            {
                Map(s.ExtraData.Count);
                foreach (var pair in s.ExtraData) { Str(pair.Key); Str(pair.Value); }
            }
        }

        static void WriteHair(SkinHair h)
        {
            if (h == null) { Nil(); return; }
            Array(11);
            Int(h.Color); Int(h.OutlineColor); F32(h.OffsetX); F32(h.OffsetY); F32(h.DuckingOffsetX); F32(h.DuckingOffsetY);
            Int(h.AddLinks); F32(h.AddLinkDistance); Bool(h.ShowOnHat); WriteImage(h.Texture); WriteImage(h.TextureEnd);
        }

        // MessagePackSerializer.Deserialize<ArcherSkinBundle>(bytes): null when the bytes aren't one (TF.EX catches the exception)
        public static ArcherSkinBundle Deserialize(byte[] bytes)
        {
            Open(bytes);
            if (IsNil()) return null;
            var b = new ArcherSkinBundle();
            int n = ReadArray();
            for (int k = 0; k < n && Ok(); k++)
            {
                switch (k)
                {
                    case 0: b.CustomArcherId = ReadStr(); break;
                    case 1: b.Name0 = ReadStr(); break;
                    case 2: b.Name1 = ReadStr(); break;
                    case 3: b.ColorA = (uint)ReadInt(); break;
                    case 4: b.ColorB = (uint)ReadInt(); break;
                    case 5: b.PortraitNotJoined = ReadImage(); break;
                    case 6: b.PortraitJoined = ReadImage(); break;
                    case 7: b.PortraitWin = ReadImage(); break;
                    case 8: b.PortraitLose = ReadImage(); break;
                    case 9: b.Aimer = ReadImage(); break;
                    case 10: b.Body = ReadSprite(); break;
                    case 11: b.HeadNormal = ReadSprite(); break;
                    case 12: b.HeadNoHat = ReadSprite(); break;
                    case 13: b.HeadCrown = ReadSprite(); break;
                    case 14: b.HeadBack = ReadSprite(); break;
                    case 15: b.Bow = ReadSprite(); break;
                    case 16: b.HatNormal = ReadImage(); break;
                    case 17: b.HatBlue = ReadImage(); break;
                    case 18: b.HatRed = ReadImage(); break;
                    case 19: b.Hair = ReadHair(); break;
                    case 20: b.HasBreathing = ReadBool(); break;
                    case 21: b.BreathingInterval = (int)ReadInt(); break;
                    case 22: b.BreathingOffsetX = ReadF32(); break;
                    case 23: b.BreathingOffsetY = ReadF32(); break;
                    case 24: b.BreathingDuckingOffsetX = ReadF32(); break;
                    case 25: b.BreathingDuckingOffsetY = ReadF32(); break;
                    case 26: b.SleepHeadFrame = (int)ReadInt(); break;
                    case 27: b.GemMenu = ReadSprite(); break;
                    case 28: b.GemGameplay = ReadSprite(); break;
                    default: Skip(); break;
                }
            }
            return Ok() ? b : null;
        }

        static SkinImage ReadImage()
        {
            if (IsNil()) return null;
            var i = new SkinImage();
            int n = ReadArray();
            for (int k = 0; k < n && Ok(); k++)
            {
                if (k == 0) i.Width = (int)ReadInt();
                else if (k == 1) i.Height = (int)ReadInt();
                else if (k == 2) i.DeflatedRgba = ReadBin();
                else Skip();
            }
            return i;
        }

        static int[] ReadInts()
        {
            if (IsNil()) return null;
            int n = ReadArray();
            if (n < 0) return null;
            var a = new int[n];
            for (int i = 0; i < n && Ok(); i++) a[i] = (int)ReadInt();
            return a;
        }

        static SkinSprite ReadSprite()
        {
            if (IsNil()) return null;
            var s = new SkinSprite();
            int n = ReadArray();
            for (int k = 0; k < n && Ok(); k++)
            {
                switch (k)
                {
                    case 0: s.Sheet = ReadImage(); break;
                    case 1: s.FrameWidth = (int)ReadInt(); break;
                    case 2: s.FrameHeight = (int)ReadInt(); break;
                    case 3: s.OriginX = ReadF32(); break;
                    case 4: s.OriginY = ReadF32(); break;
                    case 5:
                        if (IsNil()) { s.Animations = null; break; }
                        int count = ReadArray();
                        for (int i = 0; i < count && Ok(); i++) s.Animations.Add(ReadAnimation());
                        break;
                    case 6: s.BlueSheet = ReadImage(); break;
                    case 7: s.RedSheet = ReadImage(); break;
                    case 8: s.HeadXOrigins = ReadInts(); break;
                    case 9: s.HeadYOrigins = ReadInts(); break;
                    case 10: s.BowXOffsets = ReadInts(); break;
                    case 11: s.BowYOffsets = ReadInts(); break;
                    case 12: s.X = (int)ReadInt(); break;
                    case 13: s.Y = (int)ReadInt(); break;
                    case 14:
                        if (IsNil()) break;
                        int pairs = ReadMap();
                        s.ExtraData = new Dictionary<string, string>();
                        for (int i = 0; i < pairs && Ok(); i++) { var key = ReadStr(); s.ExtraData[key ?? ""] = ReadStr(); }
                        break;
                    default: Skip(); break;
                }
            }
            return s;
        }

        static SkinAnimation ReadAnimation()
        {
            if (IsNil()) return null;
            var a = new SkinAnimation();
            int n = ReadArray();
            for (int k = 0; k < n && Ok(); k++)
            {
                if (k == 0) a.Id = ReadStr();
                else if (k == 1) a.Delay = ReadF32();
                else if (k == 2) a.Loop = ReadBool();
                else if (k == 3) a.Frames = ReadInts();
                else Skip();
            }
            return a;
        }

        static SkinHair ReadHair()
        {
            if (IsNil()) return null;
            var h = new SkinHair();
            int n = ReadArray();
            for (int k = 0; k < n && Ok(); k++)
            {
                switch (k)
                {
                    case 0: h.Color = (uint)ReadInt(); break;
                    case 1: h.OutlineColor = (uint)ReadInt(); break;
                    case 2: h.OffsetX = ReadF32(); break;
                    case 3: h.OffsetY = ReadF32(); break;
                    case 4: h.DuckingOffsetX = ReadF32(); break;
                    case 5: h.DuckingOffsetY = ReadF32(); break;
                    case 6: h.AddLinks = (int)ReadInt(); break;
                    case 7: h.AddLinkDistance = ReadF32(); break;
                    case 8: h.ShowOnHat = ReadBool(); break;
                    case 9: h.Texture = ReadImage(); break;
                    case 10: h.TextureEnd = ReadImage(); break;
                    default: Skip(); break;
                }
            }
            return h;
        }
    }
}
