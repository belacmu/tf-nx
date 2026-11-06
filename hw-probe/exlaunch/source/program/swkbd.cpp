/* The Switch's software keyboard (the swkbd library applet) through the game's own SDK (nn::swkbd, found by name with netws::SdkExport).
 * Used where TF.EX types on a PC keyboard (PrivateJoinCode's JoinCodeEntry). Layouts read from the SDK's ShowKeyboard (sdk +0x37f560):
 * ShowKeyboardArg = KeyboardConfig (0x4d0 bytes: keyboardMode +0x0, okText +0x4, invalidCharFlag +0x1c, headerText +0x24, subText
 * +0xa6, guideText +0x1a8, textMaxLength +0x3ac, textMinLength +0x3b0, ...), then workBuf/workBufSize +0x4d0/+0x4d8, more optional
 * buffers to +0x500; String = { char16_t* ptr; size_t bufSize } (GetRequiredStringBufferSize 0x7d4); work buffer 0x1000 bytes
 * (GetRequiredWorkBufferSize(false)), page aligned. ShowKeyboard blocks until the player closes the keyboard: 0 = OK, 0x29f = cancelled. */
#include <cstdint>
#include <cstring>
#include "netws.hpp"
#include "hwlog.hpp"

namespace swkbd {
namespace {
using MakePresetFn = void (*)(void* config, int preset);
using SetTextFn = void (*)(void* config, const char16_t* text);
using ShowFn = uint32_t (*)(void* outString, const void* arg);
alignas(0x1000) uint8_t g_work[0x1000];
alignas(16) uint8_t g_arg[0x600];
char16_t g_text[0x7d4 / 2];
struct SwString { char16_t* ptr; uint64_t bufSize; };
}

/* the code typed: `len` characters of `alphabet` (lower case accepted, upper-cased); false if cancelled or invalid */
bool ReadCode(char* out, int len, const char16_t* header, const char* alphabet) {
    static MakePresetFn makePreset = reinterpret_cast<MakePresetFn>(netws::SdkExport("_ZN2nn5swkbd10MakePresetEPNS0_14KeyboardConfigENS0_6PresetE"));
    static SetTextFn setHeader = reinterpret_cast<SetTextFn>(netws::SdkExport("_ZN2nn5swkbd13SetHeaderTextEPNS0_14KeyboardConfigEPKDs"));
    static ShowFn show = reinterpret_cast<ShowFn>(netws::SdkExport("_ZN2nn5swkbd12ShowKeyboardEPNS0_6StringERKNS0_15ShowKeyboardArgE"));
    if (!makePreset || !setHeader || !show) { hw::Log("[swkbd] nn::swkbd not found in the SDK"); return false; }
    std::memset(g_arg, 0, sizeof g_arg);
    makePreset(g_arg, 0);                                             /* Preset_Default */
    *reinterpret_cast<uint32_t*>(g_arg + 0x1c) = 1u << 1;             /* invalidCharFlag: no space */
    *reinterpret_cast<uint32_t*>(g_arg + 0x3ac) = (uint32_t)len;      /* textMaxLength */
    *reinterpret_cast<uint32_t*>(g_arg + 0x3b0) = (uint32_t)len;      /* textMinLength */
    setHeader(g_arg, header);
    *reinterpret_cast<void**>(g_arg + 0x4d0) = g_work;
    *reinterpret_cast<uint64_t*>(g_arg + 0x4d8) = sizeof g_work;
    std::memset(g_text, 0, sizeof g_text);
    SwString s{ g_text, sizeof g_text };
    uint32_t r = show(&s, g_arg);
    if (r != 0) { hw::Log("[swkbd] keyboard closed without a text (%#x)", r); return false; }
    int n = 0;
    for (; n < len; n++) {
        char16_t c = g_text[n];
        if (c >= u'a' && c <= u'z') c = (char16_t)(c - 32);
        if (c == 0 || c > 0x7f || !std::strchr(alphabet, (char)c)) { hw::Log("[swkbd] not a code (character %d)", n); return false; }
        out[n] = (char)c;
    }
    if (g_text[len] != 0) { hw::Log("[swkbd] not a code (too long)"); return false; }
    out[len] = 0;
    return true;
}

/* free text (TF.EX's settings: the name, the server): up to maxLen characters, the keyboard opening on `initial`; false if cancelled */
bool ReadText(char16_t* out, int maxLen, const char16_t* header, const char16_t* initial) {
    static MakePresetFn makePreset = reinterpret_cast<MakePresetFn>(netws::SdkExport("_ZN2nn5swkbd10MakePresetEPNS0_14KeyboardConfigENS0_6PresetE"));
    static SetTextFn setHeader = reinterpret_cast<SetTextFn>(netws::SdkExport("_ZN2nn5swkbd13SetHeaderTextEPNS0_14KeyboardConfigEPKDs"));
    static SetTextFn setInitial = reinterpret_cast<SetTextFn>(netws::SdkExport("_ZN2nn5swkbd14SetInitialTextEPNS0_15ShowKeyboardArgEPKDs"));
    static ShowFn show = reinterpret_cast<ShowFn>(netws::SdkExport("_ZN2nn5swkbd12ShowKeyboardEPNS0_6StringERKNS0_15ShowKeyboardArgE"));
    if (!makePreset || !setHeader || !show) { hw::Log("[swkbd] nn::swkbd not found in the SDK"); return false; }
    std::memset(g_arg, 0, sizeof g_arg);
    makePreset(g_arg, 0);                                             /* Preset_Default */
    *reinterpret_cast<uint32_t*>(g_arg + 0x3ac) = (uint32_t)maxLen;   /* textMaxLength */
    setHeader(g_arg, header);
    *reinterpret_cast<void**>(g_arg + 0x4d0) = g_work;
    *reinterpret_cast<uint64_t*>(g_arg + 0x4d8) = sizeof g_work;
    if (setInitial && initial && initial[0]) setInitial(g_arg, initial);   /* (ShowKeyboardArg: the initial text goes in its own buffer) */
    std::memset(g_text, 0, sizeof g_text);
    SwString s{ g_text, sizeof g_text };
    uint32_t r = show(&s, g_arg);
    if (r != 0) { hw::Log("[swkbd] keyboard closed without a text (%#x)", r); return false; }
    int n = 0;
    for (; n < maxLen && g_text[n]; n++) out[n] = g_text[n];
    out[n] = 0;
    return true;
}
}

