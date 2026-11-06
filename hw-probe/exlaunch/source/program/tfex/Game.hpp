/* The vanilla TowerFall objects the TF.EX port touches on the Switch (1.4.1.6, BRUTE AOT): field offsets, static field slots and the
 * native bodies of the methods it calls, named after the vanilla C# members (docs/reference/tf8-decompile/TowerFall). Found with
 * ../tools/re (metaresolve.py + disassembly; switch-online/SWITCH-UI-RE.md, docs/ws-lobby-ui.md) and checked at run time. */
#pragma once
#include <cstdint>
#include <cstring>

namespace tfex::game {

uintptr_t Base();

template <class T> inline T& At(const void* obj, uint32_t off) { return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(obj) + off); }
/* A static field: its slot (module offset) holds the field's address. */
template <class T> inline T& Static(uintptr_t slot) { return *reinterpret_cast<T*>(*reinterpret_cast<uintptr_t*>(Base() + slot)); }
template <class F> inline F Fn(uintptr_t off) { return reinterpret_cast<F>(Base() + off); }
/* Managed array data (arrays: data pointer at +0x10). */
template <class T> inline T* ArrayData(void* arr) { return arr ? *reinterpret_cast<T**>(reinterpret_cast<uintptr_t>(arr) + 0x10) : nullptr; }

/* A virtual call through the object's vtable (byte offset `slot`), with the game's `this` adjustment (entry bits 59.. & 0x10). */
template <class R, class... A> inline R Virt(void* obj, uint32_t slot, A... a) {
    uintptr_t desc = *reinterpret_cast<uintptr_t*>(obj);
    uintptr_t entry = (*reinterpret_cast<uintptr_t**>(desc + 8))[slot / 8];
    auto fn = reinterpret_cast<R (*)(void*, A...)>(entry & 0x7fffffffffffffffull);
    return fn(reinterpret_cast<uint8_t*>(obj) + ((entry >> 0x3b) & 0x10), a...);
}

const char* TypeName(void* obj, char* out, int cap);
bool Is(void* obj, const char* typeName);

/* A managed string outside the GC heap (strings are immutable and Boehm ignores pointers it doesn't own): class word copied from a live
 * string, 8 zero bytes, length at +0x10, UTF-16 from +0x14. */
struct alignas(16) MString { uintptr_t klass; uint64_t zero; int32_t len; char16_t ch[44]; };
void MakeString(MString* m, const void* liveString, const char* text);

/* ---- Monocle ---- */
namespace Entity {
    constexpr uint32_t Active = 0x10, Visible = 0x11, Collidable = 0x12, PositionX = 0x14, PositionY = 0x18, Scene = 0x20;
    constexpr uintptr_t Add_Component = 0x92f9d0;   /* void Entity.Add(Component) */
    constexpr uintptr_t RemoveSelf = 0x92f960;
}
namespace Engine {
    constexpr uintptr_t get_Instance = 0x92cb60, set_Scene = 0x92cc60, get_Screen = 0x92cc20 /* instance: Engine.Instance.Screen */, get_TimeMult = 0x92cb70 /* static */;
    constexpr uint32_t scene = 0xe8, nextScene = 0xf0;
}
/* Monocle.Screen (Switch): PadOffset, the x the 420-wide canvas is drawn at; -50 shows the side art (statics Screen.LeftImage / RightImage) */
namespace Screen { constexpr uint32_t PadOffset = 0x48; }
namespace Calc { constexpr uintptr_t Approach = 0x90a490; }   /* float Approach(float val, float target, float maxMove) */
constexpr uintptr_t Atlas_get_Item = 0x904b10;     /* Subtexture Atlas[string] */
namespace Component {   /* +0x10 Entity, +0x18 Parent, +0x21 Active, +0x22 Visible (from a live Image) */
    constexpr uint32_t Active = 0x21, Visible = 0x22;
}
namespace GraphicsComponent { constexpr uint32_t PositionX = 0x24, PositionY = 0x28, Scale = 0x34, Rotation = 0x40, Color = 0x44; }
namespace OutlineText {   /* obj = F(null, SpriteFont, string, Vector2 position, HorizontalAlign, VerticalAlign) (1 = center) */
    constexpr uintptr_t ctor = 0x941890;
    constexpr uintptr_t set_DrawText = 0x955b90;   /* Text.DrawText */
}

/* ---- TowerFall ---- */
namespace TFGame {
    constexpr uintptr_t Players = 0x4793890, Characters = 0x4792458, AltSelect = 0x4792460, PlayerInputs = 0x4791fa8;   /* static slots */
    constexpr uintptr_t get_PlayerAmount = 0xb74cd0, get_MenuAtlas = 0xb747f0, get_Atlas = 0xb74790, get_Font = 0xb74bb0;
    inline uint8_t* Players_() { return ArrayData<uint8_t>(Static<void*>(Players)); }
    inline int32_t Players_Len() { void* a = Static<void*>(Players); return a ? **reinterpret_cast<int32_t**>(reinterpret_cast<uintptr_t>(a) + 0x20) : 0; }   /* 8 (megamod/eightplayer.hpp widens it) */
    inline int32_t* Characters_() { return ArrayData<int32_t>(Static<void*>(Characters)); }
    inline int32_t* AltSelect_() { return ArrayData<int32_t>(Static<void*>(AltSelect)); }
    inline void** PlayerInputs_() { return ArrayData<void*>(Static<void*>(PlayerInputs)); }
}
namespace MainMenu {
    enum MenuState { None = 0, Loading = 1, PressStart = 2, Main = 3, Fade = 4, Rollcall = 5, CoOp = 6, VersusOptions = 7,
                     VersusSub = 7 };   /* 7 on the Switch: VERSUS's STANDARD / WIDE screen (CreateVersusSub +0xaa1e20; FightButton.MenuAction +0xa42be0 goes there) */
    enum RollcallModes { Versus = 0, SixPlayer = 1 };
    constexpr uint32_t BackState = 0x9c, state = 0xb4, switchTo = 0xc0, ToStartSelected = 0xc8;
    constexpr uintptr_t Update = 0xa9f4d0;
    constexpr uintptr_t NoGamepadUpdates = 0x4794a00, NoGamepads = 0x4794a08;   /* static bools (MainMenu.Update skips gamepad discovery) */
    constexpr uintptr_t RollcallMode = 0x4792b58, CurrentMatchSettings = 0x4793818, VersusMatchSettings = 0x4793398;   /* static slots */
    constexpr uintptr_t ctor = 0xa9e890;           /* MainMenu new(MenuState) (factory: x0 = null) */
    constexpr uintptr_t CreateMain = 0xaa0bd0, set_State = 0xa9ed20, AddItems = 0xaa1030;   /* AddItems: MainMenu.Add<MenuItem>(List) */
}
namespace MenuItem {
    constexpr uint32_t Selected = 0x68, MainMenu = 0x70, DownItem = 0x78, UpItem = 0x80, RightItem = 0x88, LeftItem = 0x90;   /* get_Selected +0xac9180 */
    constexpr uint32_t TweenIn = 0x80, TweenOut = 0x88, OnConfirm = 0xa0;   /* vtable slots */
}
namespace MainModeButton {
    constexpr uint32_t text = 0xc8, playersText = 0xd0, tweenFrom = 0xdc, tweenTo = 0xe4;
    constexpr uintptr_t ctor = 0xaa6700;           /* (this, position, tweenFrom, text, playersText) */
}
namespace Buttons {   /* factories: obj = F(null, Vector2 position, Vector2 tweenFrom) */
    constexpr uintptr_t FightButton = 0xa428f0, CoOpButton = 0x9d5f50, ArchivesButton = 0x97c8e0, TrialsButton = 0xb885f0;
    constexpr uint32_t ArchivesButton_image = 0xf0, ArchivesButton_glow = 0xf8;
}
constexpr uintptr_t ListMenuItem_ctor = 0x971a50, ListMenuItem_Add = 0x971b20;
namespace RollcallElement {
    constexpr uint32_t portrait = 0x98, finishedJoined = 0xb8, archerType = 0xbc, playerIndex = 0xc4, input = 0xc8, shakeTimer = 0x110;
    constexpr uintptr_t NotJoinedUpdate = 0xb32a20, JoinedUpdate = 0xb335c0, StartVersus = 0xb35c90, ForceStart = 0xb359f0;
}
namespace ArcherPortrait {
    constexpr uintptr_t StartJoined = 0x96fb20, Join = 0x96fc10, Leave = 0x96fd70, SetCharacter = 0x96fe70;   /* SetCharacter(index, ArcherTypes, dir) */
}
namespace PlayerInput {   /* vtable slots */
    constexpr uint32_t MenuConfirm = 0x38, MenuBack = 0x48, MenuStart = 0x50;   /* the Switch roll call starts versus with MenuStart (+) */
    constexpr uint32_t MenuUp = 0x70, MenuDown = 0x78;   /* PC order (JoyConInput's vtable at +0x470b868: ..., Alt 0x60, Alt2 0x68, Up, Down) */
}
constexpr uintptr_t ReadyBanner_GetReadyState = 0xb2c520;
constexpr uintptr_t MenuInput_Confirm = 0xac6a00;

/* The scene the engine runs, and its type. */
void* CurrentScene();
bool SceneIs(const char* typeName);

/* ---- BRUTE's managed runtime: what a class of our own needs to be a real game class (docs/ws-tfex-subclass.md; tools/re/rtypes.py) ----
 * obj[0] = descriptor (.data, 3 words: runtime type, vtable, interface table); obj[1] = sync word; fields from +0x10.
 * The runtime type (0x168 bytes, built lazily by each type's getter through MakeType +0x2460, allocated uncollectable):
 *   +0x00 instance size, +0x08 flags (bit 3: holds references -> GC_malloc, scanned conservatively; else GC_malloc_atomic + memset;
 *   bit 5: finalizer), +0x0c hash, +0x18 kind, +0x20/28/30/38 name / namespace / full name / assembly-qualified name (UTF-16),
 *   +0x60 descriptor, +0x70 class-constructor runner (0 = none), +0x88 base type getter (`is`/casts walk it: IsInst +0x950 ->
 *   +0x2580), +0xd8/+0xe0 interface getters / count, +0xe8/+0xfa field builder / count, +0x130/+0x140 method builder / count.
 * A delegate (System.Action, Action`1[...]: 0x68 bytes): +0x10 the function, +0x20 the target (null: static), +0x28 its method record
 * (0x60 bytes: +0x00 name, +0x08 declaring runtime type, +0x18 body), +0x58 the previous delegate of a multicast. Invoke (vtable 0x58)
 * calls fn(target, args...) (or fn(args...) without a target). */
namespace brute {
    constexpr uintptr_t NewObject = 0x610;            /* void* (RuntimeType*): allocate + header word (= the type's descriptor) */
    constexpr uintptr_t IsInst = 0x950;               /* void* (obj, RuntimeType*): obj if obj is that type or a subclass, else null */
    constexpr uintptr_t MallocUncollectable = 0xe50;  /* void* (size): GC_malloc_uncollectable (kind 2: scanned, never freed) */
    /* void* (arrayType, elementSize, const int32_t* lengths, rank): C# `new T[n]` (e.g. String[]: type getter +0x8811b0, element size
     * 8, rank 1). The array: +0x10 data pointer, +0x18 element size, +0x1c rank, +0x20 lengths pointer, elements at data. */
    constexpr uintptr_t NewArray = 0x6110;
    constexpr uint32_t RtSize = 0x00, RtFlags = 0x08, RtHash = 0x0c, RtName = 0x20, RtNamespace = 0x28, RtFullName = 0x30, RtAqName = 0x38,
                       RtDesc = 0x60, RtCctor = 0x70, RtParent = 0x88, RtFieldBuilder = 0xe8, RtFieldCount = 0xfa, RtPropCount = 0x110,
                       RtMethodBuilder = 0x130, RtMethodCount = 0x140, RtRecord = 0x168;
    constexpr uint32_t DlgFn = 0x10, DlgTarget = 0x20, DlgMethod = 0x28, DlgPrev = 0x58;
    /* type getters (return the runtime type, building it on first use) */
    constexpr uintptr_t Type_Entity = 0x6f1470, Type_Action = 0x6f44f0, Type_ActionTween = 0x6f5cf0, Type_MainMenu = 0x8b62f0,
                        Type_Level = 0x8b5570, Type_ListEntity = 0x827f30, Type_QueueString = 0x82beb0;
    constexpr uintptr_t Desc_Entity = 0x46f6470, Desc_String = 0x45aefb8;
    constexpr uint32_t EntityVtableBytes = 0x90;      /* 18 slots: Object 0..0x18, SceneBegin 0x20 .. HandleGraphicsDispose 0x68, IComponentHolder 0x70..0x88 */
    namespace EntitySlot { constexpr uint32_t SceneBegin = 0x20, SceneEnd = 0x28, Added = 0x30, Removed = 0x38, Update = 0x40, PreRender = 0x48,
                                              Render = 0x50, DebugRender = 0x58; }

    inline void* Type(uintptr_t getter) { return Fn<void* (*)()>(getter)(); }
    /* A game type's class-constructor runner (runtime type +0x70; rtypes.py "cctor"). BRUTE checks beforefieldinit at the call sites:
     * before our code calls a game type's static method or reads its static fields, it runs this (as the game's callers do). */
    inline void Cctor(uintptr_t runner) { if (runner) Fn<void (*)()>(runner)(); }
    namespace CctorOf { constexpr uintptr_t Alarm = 0x2680690, Tween = 0x26d7c84, Ease = 0x26aa4bc, TFGame = 0x2812bc4; }
    inline void* As(void* obj, void* type) { return obj ? Fn<void* (*)(void*, void*)>(IsInst)(obj, type) : nullptr; }   /* C# `obj as T` */

    /* A class of ours deriving from a game class: its runtime type (a copy of the base's with our size, names, descriptor and base),
     * descriptor and vtable (the base's, with our overrides). The type record is allocated like the game's (uncollectable); the
     * descriptor and vtable live in our module. All of it is made once, outside any rollback window, and never changes. */
    struct Subclass {
        void* type = nullptr; void* base = nullptr;
        uintptr_t desc[3] = {};
        uintptr_t vtable[32] = {};
        uint8_t method[8][0x60] = {};   /* method records for the delegates we make (declaring type = ours) */
    };
    /* names: UTF-16, static storage. size: our instance size (>= the base's). */
    bool MakeSubclass(Subclass& c, uintptr_t baseGetter, uintptr_t baseDesc, uint32_t vtableBytes, uint32_t size,
                      const char16_t* name, const char16_t* ns, const char16_t* fullName, const char16_t* aqName, uint32_t hash);
    inline void Override(Subclass& c, uint32_t slot, void* fn) { c.vtable[slot / 8] = reinterpret_cast<uintptr_t>(fn); }
    inline void* New(Subclass& c) { return Fn<void* (*)(void*)>(NewObject)(c.type); }
    /* C# `new Action(target.Method)` / a lambda capturing `this`: a delegate of delegateType calling fn(target, args...). */
    void* NewDelegate(Subclass& c, int methodIndex, uintptr_t delegateTypeGetter, void* target, void* fn);
    /* A delegate calling plain C++ (no class of ours): fn(target, args...); its method record names Monocle.Entity (no class
     * constructor). E.g. TF.EX's `() => button.Selected` as a System.Func`1[System.Boolean] for a generated class. target must be a
     * managed object (or null): Invoke reads its type's flags (value-type targets are unboxed). */
    void* NewNativeDelegate(uintptr_t delegateTypeGetter, void* target, void* fn);
    /* A static string literal outside the GC heap (strings are immutable; Boehm ignores pointers it doesn't own). */
    inline void Literal(MString* m, const char* text) {
        m->klass = Base() + Desc_String; m->zero = 0; m->len = 0;
        while (text[m->len] && m->len < 43) { m->ch[m->len] = (char16_t)text[m->len]; m->len++; }
        m->ch[m->len] = 0;
    }
}

/* ---- members TF.EX's Notification uses (tools/re: rtypes.py fields, sigs.py bodies; overloads by IL order) ---- */
struct Vector2 { float X, Y; };
struct Color { uint32_t packed; };   /* XNA Color: R | G << 8 | B << 16 | A << 24 */
constexpr Color Color_White{0xffffffffu}, Color_Black{0xff000000u};
namespace Entity {   /* Monocle.Entity (more) */
    constexpr uint32_t Size = 0x68, LayerIndex = 0x54;
    constexpr uintptr_t ctor_int = 0x92d7f0;          /* .ctor(int layerIndex) (this, layer) */
    constexpr uintptr_t set_Scene = 0x92d8e0, set_Depth = 0x92d970;
    constexpr uintptr_t Update = 0x92f3d0, Render = 0x92f680, Removed = 0x92f2c0, SceneEnd = 0x92f090;   /* the base bodies */
}
namespace Scene { constexpr uint32_t Layers = 0x18; }                  /* Dictionary<int, Layer> (get_Layers +0x943c40) */
constexpr uintptr_t DictIntLayer_get_Item = 0x92d9f0;                   /* Dictionary<int, Layer>[int] */
namespace Layer {   /* Monocle.Layer */
    constexpr uint32_t Scene = 0x30, Index = 0x38, Entities = 0x40, toAdd = 0x60;   /* toAdd: private (Layer.Add = toAdd.Add) */
    constexpr uintptr_t Add = 0x938e40;               /* Add(Entity, bool) */
}
namespace ListEntity {   /* System.Collections.Generic.List`1[Monocle.Entity]: _items +0x10, _size +0x18 */
    constexpr uint32_t items = 0x10, size = 0x18;
    constexpr uintptr_t ctor = 0x913450, Add = 0x9132b0, Remove = 0x1c5a140, get_Item = 0x931690, get_Count = 0x1c59fa0;
}
namespace QueueString { constexpr uintptr_t ctor = 0xb28d70, Enqueue = 0xb29350, Dequeue = 0xb294a0, Clear = 0x218f6ec; constexpr uint32_t size = 0x20; }
namespace MainMenu { constexpr uintptr_t get_UILayer = 0xa9ec00; }   /* a CameraLayer */
namespace CameraLayer { constexpr uint32_t Camera = 0x70; }
namespace Camera { constexpr uintptr_t get_Y = 0x90f4d0; }
namespace Tween {   /* Monocle.Tween */
    enum TweenMode { Persist = 0, Oneshot = 1 };
    constexpr uint32_t Easer = 0x28, OnUpdate = 0x30, OnComplete = 0x38, OnStart = 0x40;
    constexpr uintptr_t Create = 0x9597d0, get_Eased = 0x959780;   /* Create(TweenMode, Ease.Easer, int duration, bool start) static */
}
namespace Alarm {
    enum AlarmMode { Persist = 0, Oneshot = 1 };
    constexpr uint32_t OnComplete = 0x28;
    constexpr uintptr_t Create = 0x903620;            /* Create(AlarmMode, Action, int duration, bool start) static */
}
namespace Ease { constexpr uintptr_t CubeOut = 0x488bf18; }   /* static field storage (Ease.Easer) */
namespace Canvas { constexpr uintptr_t ctor = 0x9100b0; }    /* new Canvas(int width, int height) (null, w, h) */
namespace Texture { constexpr uintptr_t get_Width = 0x956500; }
namespace SpriteFont { constexpr uintptr_t MeasureString = 0xd9590; }   /* Vector2 (font, string) */
namespace String { constexpr uintptr_t ToUpper = 0x3b8ff0; }
namespace Draw {
    constexpr uintptr_t Texture_SubPosColorOriginScale = 0x9266b0;    /* Texture(Subtexture, Vector2, Color, Vector2 origin, Vector2 scale) (#6) */
    constexpr uintptr_t Texture_SubPosColorOriginScaleRotEffects = 0x9268f0;   /* (..., Vector2 origin, float scale, float rotation, SpriteEffects) (#8) */
    constexpr uintptr_t TextCentered = 0x923f20;      /* TextCentered(SpriteFont, string, Vector2, Color) (#0) */
    enum SpriteEffects { None = 0, FlipHorizontally = 1 };
}

} // namespace tfex::game
