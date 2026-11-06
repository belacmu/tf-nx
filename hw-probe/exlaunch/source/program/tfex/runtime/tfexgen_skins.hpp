/* TF.EX's custom skins (item 11), the library half (OUR OWN CODE, hand runtime): what TF.EX's SkinStreamService / SkinOverlayService take
 * from .NET and MessagePack-CSharp, for the glue's [Cpp] externs (translator/glue/Skins.cs). The bundle travels as MessagePack (TF.EX's
 * ArcherSkinBundle: [MessagePackObject], integer keys = arrays), its id is the SHA-256 of those bytes, images are raw DEFLATE of RGBA.
 * Textures: the game's own GetData<Color>(Color[]) (+0x910440, Monocle's Canvas(Texture)) and SetData<Color>(Color[]) (the pair
 * +0xb8bf0 / +0xb9120 Monocle's Texture(int, int, Color) and Canvas call). One writer at a time (the game thread). */
#pragma once
#include "tfexgen_code_rt.hpp"

namespace tfex::gen::skins {
/* System.Security.Cryptography.SHA256.HashData(byte[]) and System.Convert.ToHexString(byte[]) (upper case, as .NET) */
gen::Arr<uint8_t> Sha256(const gen::Arr<uint8_t>& data);
void* HexUpper(const gen::Arr<uint8_t>& data);
/* System.Convert.FromBase64String: the game's runtime lacks Mono's decoding icall (InternalFromBase64String +0x6cb0 always throws);
 * whitespace skipped as .NET does; anything else invalid: an empty byte[] (TF.EX's callers treat a FormatException so) */
gen::Arr<uint8_t> FromBase64(const void* s);
/* raw DEFLATE (System.IO.Compression.DeflateStream, CompressionLevel.Optimal): a new byte[] */
gen::Arr<uint8_t> Deflate(const gen::Arr<uint8_t>& data);
/* raw DEFLATE inflated into `into` (its length: the expected size): the bytes written, -1 if invalid or longer */
int32_t Inflate(const gen::Arr<uint8_t>& src, const gen::Arr<uint8_t>& into);

/* textures: Texture2D.GetData<Color>(level, rect, data, start, count) (no rect: the whole level), FNA's pair as the game's
 * GetData<Color>(Color[]) +0x910440 calls it: +0xb8bf0 resolves and checks the Rectangle? into the read's rect, +0x9106e0 reads it */
void GetRect(void* texture2D, int32_t level, bool hasRect, int32_t x, int32_t y, int32_t w, int32_t h,
             const gen::Arr<gen::Color>& data, int32_t start, int32_t count);
/* RGBA bytes (4 per pixel) into `colors` (their length / 4) and onto the texture (Texture2D.SetData) */
void SetRgba(void* texture2D, const gen::Arr<uint8_t>& rgba, const gen::Arr<gen::Color>& colors);
/* Texture2D.SetData<Color>(Color[]) (Teams+'s Recolor: a recoloured sheet): the game's expansion of it */
void SetColors(void* texture2D, const gen::Arr<gen::Color>& colors);

/* MessagePack writer: Begin, the values, End = the bytes (a new byte[]) */
void MpBegin();
void MpArray(int32_t n);
void MpMap(int32_t n);
void MpNil();
void MpBool(bool v);
void MpInt(int64_t v);
void MpF32(float v);
void MpUInt(uint64_t v);   /* uint / ulong / ushort / byte: the compact unsigned forms (MessagePack-CSharp Write(ulong)) */
void MpF64(double v);
void MpStr(const void* s);   /* null: nil */
void MpBin(const gen::Arr<uint8_t>& b);   /* null: nil */
gen::Arr<uint8_t> MpEnd();
/* where MessagePack-CSharp's buffer segments of the last write ended (each a byte offset; the last segment runs to the end): its
 * Lz4BlockArray makes one LZ4 block per segment. The count, at most max */
int32_t MpSegments(int32_t* ends, int32_t max);
/* MessagePack reader over a byte[] (the caller keeps it alive): Open, then the values in order; any mismatch sets the error */
void MpOpen(const gen::Arr<uint8_t>& bytes);
bool MpNil_();          /* the next is nil: consumed, true */
int32_t MpReadArray();  /* an array header: its count, -1 (error) otherwise */
int32_t MpReadMap();
bool MpReadBool();
int64_t MpReadInt();
float MpReadF32();
uint64_t MpReadUInt();
double MpReadF64();
void* MpReadStr();      /* nil: null */
gen::Arr<uint8_t> MpReadBin();   /* nil: null */
void MpSkip();
bool MpOk();            /* no error so far, and (MpDone) */
bool MpDone();          /* all bytes read */

}  // namespace tfex::gen::skins
