/* AiSlotButton.cpp: the console's controls for LoaderAI's AI players on archer select (TFEX-DIFF S-input, docs/PLAN-TFNX-2026-11-04.md
 * section 11; the user's design 2026-11-06).
 *
 * On the PC LoaderAI gives its agents the player slots with no controller at load (a null TFGame.PlayerInputs entry), each agent's input
 * a KeyboardInput over that slot's own keys (1-8 join it, F1-F8 drop it), and on a slot with a controller Up / Down switches it between
 * human and AI. The Switch has controllers only. Here:
 *  - a slot without a controller stays one: LoaderAI's agents are taken out of PlayerInputs on archer select, so the slot shows the
 *    Switch's own "HOLD L + R TO JOIN" until an AI is put there; LoaderAI's Up / Down switch is off (its player type count held at 1: no
 *    triangles on a human's slot);
 *  - any player there (choosing or confirmed) on a full controller (one with a right stick: Pro, a pair, handheld) drives a cursor over the slots without a
 *    human: right stick left / right moves it, a click of the right stick puts LoaderAI's agent there (its input into PlayerInputs, which
 *    the game's own RollcallElement.HandleControlChange takes as a controller arriving, then one confirm joins it) or takes it out (back,
 *    then the slot's own input again), right stick up / down changes that AI's archer (left / right on its slot, rejoined);
 *  - L / ZL (SL on a sideways Joy-Con: every controller) is a shortcut that fills, then empties: an AI into the next empty slot until
 *    none is left, then the last AI out each press until none is left, then filling again (the user, 2026-11-06);
 *  - an AI stays joined: a confirm the game didn't take is made again (the game moves a slot that isn't joined off an archer someone
 *    joins on, RollcallElement.EnforceCharacterLock: an AI left unjoined changed archer whenever another player's changed, the user
 *    2026-11-07);
 *  - an AI never confirms an archer someone else has (the game's own CharacterTaken): it moves on first as a player's Right would
 *    (RollcallElement.ChangeSelectionRight: the next archer free and unlocked);
 *  - Teams+'s team screen (its ThreeTeamSelector reads each player's MenuLeft / MenuRight): right stick up / down picks an AI, left / right
 *    moves that AI to the next column (SOLO | BLUE | YELLOW | RED), as a player's own left / right does (the user, 2026-11-07);
 *  - online none of this: no AI archers in online games (the user, 2026-11-06; TF.EX itself only warns about non-netplay-safe mods).
 *    Online mode starting takes every AI out of PlayerInputs (DropAll, from pcgameplay::Set), and the controls are off on its roll call;
 *  - LoaderAI's Input (generated, tfexgen_Auto_Input) answers its menu getters from those pulses alone (no keyboard behind it; from its
 *    first object: TF.EX's InputService reads every input's menu getters), and its name is "AI". */
#include "lib.hpp"
#include "AiSlotButton.hpp"
#include "../hwlog.hpp"
#include "gen/tfexgen_codemods.hpp"
/* the public tree has no LoaderAI (no licence to distribute its translation: translate.toml [code_mods] public = false): no AI controls.
 * (TFEX_CODEMOD_LoaderAI, not __has_include: a header the file includes, so make rebuilds this file when the mod list changes) */
#ifndef TFEX_CODEMOD_LoaderAI
namespace tfex::gen { template <int N> struct GameVal; }
namespace tfex::aislot {
void Install() { hw::Log("[aislot] LoaderAI isn't in this build: no AI players on archer select"); }
void* AiNameOf(int32_t) { return nullptr; }
void BoardLaidOut(int32_t, int32_t) {}
void DropAll() {}
void SetAiSeat(int) {}
bool AiSeatOn() { return false; }
bool AiSeatState(int32_t, int, tfex::gen::GameVal<28>*) { return false; }
}
#else
#include "../rnd.hpp"
#include "gen/tfexgen_binding.hpp"
#include "gen/tfexgen_Auto_Input.hpp"
#include "gen/tfexgen_Auto_Agent.hpp"
#include "gen/tfexgen_La_TFModFortRiseLoaderAIModule.hpp"
#include "runtime/tfexgen_services.hpp"
#include "gen/tfexgen_GlueAiNames.hpp"
#include "gen/tfexgen_Tp_Selector.hpp"
#include "runtime/tfexgen_code_rt.hpp"
#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace rightstick { tfex::gen::Vector2 Of(void* joyConInput); bool Full(void* joyConInput); uint64_t Buttons(void* joyConInput); }

namespace tfex::aislot {
namespace {
namespace bind = tfex::gen::bind;
namespace la = tfex::gen::La_TFModFortRiseLoaderAIModule;
/* PlayerInput's virtual menu getters (tfex/InputLock.cpp: MenuConfirm 0x38 ... MenuBackCheck 0xc0, all bool) */
constexpr uint32_t kConfirm = 0x38, kBack = 0x48, kAlt2 = 0x68, kRight = 0x80, kLeft = 0x88, kFirstMenu = 0x38, kLastMenu = 0xc0;
constexpr int kSlots = 8;                    /* LoaderAI's tables are for eight players */
constexpr int kNotJoined = 0, kJoined = 1;   /* RollcallElement's StateMachine states (its constructor's SetCallbacks) */
constexpr uint64_t kStickClick = 1u << 5;    /* nn::hid::NpadButton StickR */
constexpr int kQueue = 6;

enum Act : uint8_t { None, Wait, Confirm, Back, Left, Right, Restore, Swap };
struct Slot {
    Act queue[kQueue]; int n;   /* what this slot's AI input does, one action a frame (its element's Update) */
    Act now;                    /* this frame's action: the getters answer it */
    void* ai;                   /* LoaderAI's agent input while an AI is here */
    void* prev;                 /* the slot's own input before (null, or the controller object of an npad with nothing attached) */
    void* prevType;             /* LoaderAI's player type for the slot before */
    int swapTo;                 /* (Swap) the AI type index it changes to */
    int retries;                /* confirms again since its last queued one (the AI stays joined: see RollcallUpdate) */
};
Slot g_slot[kSlots] = {};
void* g_elems[kSlots] = {};
int g_cursor = -1;
bool g_anyFull = false, g_overridden = false;
struct Pad { bool armedX = true, armedY = true, click = false; };
Pad g_pad[kSlots];
int g_logged = 0;
gen::LitN<3> kAiName{u"AI"};
gen::LitN<5> kNone{u"NONE"};
gen::LitN<5> kAdd{u"ADD "};
gen::LitN<7> kNext{u"NEXT: "};
gen::LitN<10> kRemoveAi{u"REMOVE AI"};

void Log(const char* fmt, int a, int b = 0) { if (g_logged++ < 40) hw::Log(fmt, a, b); }
bool IsAi(void* input) { void* t = tfex::gen::Auto_Input::RuntimeType(); return t && input && gen::As(input, t) != nullptr; }
int StateOf(void* elem) {
    void* sm = gen::Fld<void*>(elem, bind::f_RollcallElement_state);
    return sm ? gen::Fld<int32_t>(sm, bind::f_StateMachine_state) : -1;
}
void** Inputs(int* n) {
    void* arr = gen::Static<void*>(bind::f_TFGame_PlayerInputs);
    *n = arr ? **reinterpret_cast<int32_t**>(reinterpret_cast<uintptr_t>(arr) + 0x20) : 0;
    return arr ? game::ArrayData<void*>(arr) : nullptr;
}
void* InputAt(int i) { int n; void** d = Inputs(&n); return d && i < n ? d[i] : nullptr; }
void SetInputAt(int i, void* v) { int n; void** d = Inputs(&n); if (d && i < n) d[i] = v; }
bool IsJoyCon(void* in) { char tn[32] = ""; if (in) rnd::TypeName(in, tn, sizeof tn); return !std::strcmp(tn, "JoyConInput"); }
bool Attached(void* in) { return in && game::Virt<bool>(in, bind::v_PlayerInput_get_Attached); }
/* a slot an AI may take: no human there (no controller, or an npad with nothing attached), or an AI already */
bool AiAble(int i) {
    if (!g_elems[i]) return false;
    void* in = InputAt(i);
    return !in || IsAi(in) || (IsJoyCon(in) && !Attached(in));
}

/* LoaderAI's Input's getters: this frame's pulse for its slot, nothing else */
int SlotOfAi(void* self) { for (int i = 0; i < kSlots; i++) if (g_slot[i].ai == self) return i; return -1; }
bool Pulse(void* self, Act a) { int i = SlotOfAi(self); return i >= 0 && g_slot[i].now == a; }
bool AiMenu(void*) { return false; }
bool AiConfirm(void* self) { return Pulse(self, Confirm); }
bool AiBack(void* self) { return Pulse(self, Back); }
bool AiLeft(void* self) { return Pulse(self, Left); }
bool AiRight(void* self) { return Pulse(self, Right); }
/* its name under the slot's controller icon: LoaderAI's player type for the slot (the AI mod's agent type: AIJimmy's JIMMY) */
void* AiName(void* self) {
    int i = SlotOfAi(self);
    void* type = i >= 0 ? la::currentPlayerType()->Get(i) : nullptr;
    return type ? type : kAiName.P();
}

void Override() {
    if (g_overridden || !tfex::gen::Auto_Input::RuntimeType()) return;
    gen::Class& c = tfex::gen::Auto_Input::ClassOf();
    for (uint32_t s = kFirstMenu; s <= kLastMenu; s += 8) gen::Override(c, s, reinterpret_cast<void*>(&AiMenu));
    gen::Override(c, kConfirm, reinterpret_cast<void*>(&AiConfirm));
    gen::Override(c, kBack, reinterpret_cast<void*>(&AiBack));
    gen::Override(c, kLeft, reinterpret_cast<void*>(&AiLeft));
    gen::Override(c, kRight, reinterpret_cast<void*>(&AiRight));
    gen::Override(c, bind::v_PlayerInput_get_Name, reinterpret_cast<void*>(&AiName));
    g_overridden = true;
    hw::Log("[aislot] LoaderAI's AI input: menu getters from the console's AI controls (right stick on archer select), no keyboard; named AI");
}

/* the slot's element takes PlayerInputs' entry as the game does when a controller attaches or detaches (RollcallElement.HandleControlChange:
 * a different input resets the slot to not joined and redraws its controls; none: "HOLD L + R TO JOIN") */
void TakeInput(void* elem) { if (elem) gen::Fn<void (*)(void*)>(bind::m_RollcallElement_HandleControlChange)(elem); }
void Queue(int i, std::initializer_list<Act> acts) {
    Slot& s = g_slot[i];
    for (Act a : acts) {
        if (s.n < kQueue) s.queue[s.n++] = a;
        else { hw::Log("[pools] ERROR aislot action queue full (%d) on slot %d: an action dropped", kQueue, i + 1); break; }
    }
}
/* LoaderAI's registered AI types (listAgentType, by registration: AIJimmy's JIMMY, AINova's NOVA ...), and a slot's place among them */
int TypeCount() { auto* t = la::listAgentType(); return t ? t->Count() : 0; }
void* TypeAt(int k) { return la::listAgentType()->Get(k); }
int TypeOf(int i) {
    void* cur = la::currentPlayerType()->Get(i);
    for (int k = 0; k < TypeCount(); k++) if (gen::StrEq(TypeAt(k), cur)) return k;
    return -1;
}
void AiOn(int i) {
    auto* types = la::listAgentType();
    if (!types || types->Count() == 0) { Log("[aislot] slot %d: no AI mod registered an agent", i + 1); return; }
    Slot& s = g_slot[i];
    s.prev = InputAt(i);
    s.prevType = la::currentPlayerType()->Get(i);
    la::currentPlayerType()->Set(i, TypeAt(0));
    void* ai = la::GetCurrentPlayerInput(i);
    if (!ai || !IsAi(ai)) { la::currentPlayerType()->Set(i, s.prevType); Log("[aislot] slot %d: LoaderAI has no agent for it", i + 1); return; }
    s.ai = ai;
    SetInputAt(i, ai);                 /* as the slot's controller arriving */
    TakeInput(g_elems[i]);
    Queue(i, {Wait, Confirm});         /* then it joins */
    Log("[aislot] an AI joins slot %d", i + 1);
}
/* the stick's click on an AI slot: the next AI type there (its agent's input for this slot: LoaderAI made one per slot per type), after
 * the last the AI out */
void NextType(int i) {
    int k = TypeOf(i);
    if (k < 0 || k + 1 >= TypeCount()) { void AiOff(int); AiOff(i); return; }
    g_slot[i].swapTo = k + 1;
    if (StateOf(g_elems[i]) == kJoined) Queue(i, {Back, Swap, Wait, Confirm}); else Queue(i, {Swap, Wait, Confirm});
    Log("[aislot] slot %d: the next AI type (%d)", i + 1, k + 2);
}
void AiOff(int i) {
    if (StateOf(g_elems[i]) == kJoined) Queue(i, {Back, Restore}); else Queue(i, {Restore});
    Log("[aislot] the AI leaves slot %d", i + 1);
}
void NextArcher(int i, bool next) {
    if (StateOf(g_elems[i]) == kJoined) Queue(i, {Back, next ? Right : Left, Confirm}); else Queue(i, {next ? Right : Left});
    Log(next ? "[aislot] slot %d's AI: the next archer" : "[aislot] slot %d's AI: the previous archer", i + 1);
}

/* the L / ZL shortcut (MenuAlt2: SL on a sideways Joy-Con, so every controller has it): filling (an AI into the next empty slot) until
 * every slot is taken, then emptying (the last AI out) until no AI is left, then filling again */
bool g_emptying = false;
void Shortcut(int human) {
    bool canAdd = false, canRemove = false;
    for (int i = 0; i < kSlots; i++) { if (AiAble(i) && !g_slot[i].ai) canAdd = true; if (g_slot[i].ai) canRemove = true; }
    if (g_emptying && !canRemove) g_emptying = false;
    if (!g_emptying && !canAdd) g_emptying = true;
    if (!g_emptying) { for (int i = 0; i < kSlots; i++) if (AiAble(i) && !g_slot[i].ai) { AiOn(i); return; } }
    else { for (int i = kSlots - 1; i >= 0; i--) if (g_slot[i].ai) { AiOff(i); return; } }
    Log("[aislot] P%d's L / ZL: no empty slot for an AI and no AI to take out", human + 1);
}
/* the AI's Confirm on an archer another player has (the game lets a confirm through on a taken one: two AIs on one archer, seen in the
 * emulator): first the next free, unlocked archer, the game's own ChangeSelectionRight (tools/re callmap) */
constexpr uintptr_t kChangeSelectionRight = 0xb34570;   /* TowerFall.RollcallElement::ChangeSelectionRight() */
constexpr uintptr_t kCharacterTaken = 0xb76630;          /* TowerFall.TFGame::CharacterTaken(int) */
void FreeArcher(void* elem, int pi) {
    int32_t ch = gen::Fn<int32_t (*)(void*)>(bind::m_RollcallElement_get_CharacterIndex)(elem);
    if (!gen::Fn<bool (*)(int32_t)>(kCharacterTaken)(ch)) return;
    gen::Fn<void (*)(void*)>(kChangeSelectionRight)(elem);
    Log("[aislot] slot %d's AI: archer %d is taken, the next free one first", pi + 1, ch);
}

namespace teams { void Override(); }
/* once a frame (the first element's Update): LoaderAI's switch off, its agents out of the slots nobody put an AI in, the controls */
void Frame() {
    Override();
    static bool typesLogged = false;
    if (!typesLogged && TypeCount() > 0) {   /* which AI mods registered their agents with LoaderAI (one type each: AIJimmy's JIMMY, AINova's NOVA) */
        typesLogged = true;
        char line[160] = ""; int w = 0;
        for (int k = 0; k < TypeCount() && w < 150; k++) {
            void* t = TypeAt(k); int n = gen::Len(t);
            w += std::snprintf(line + w, sizeof line - w, "%s", k ? ", " : "");
            for (int c = 0; c < n && w < 150; c++) line[w++] = (char)gen::CharAt(t, c);
            line[w] = 0;
        }
        hw::Log("[aislot] LoaderAI's AI types: %d (%s)", TypeCount(), line);
    }
    auto& nb = la::nbPlayerType();
    for (int i = 0; i < kSlots && i < nb.len; i++) if (nb[i] > 1) nb[i] = 1;   /* LoaderAI's Up / Down human <-> AI switch: off */
    g_anyFull = false;
    if (tfex::gen::svc::OnlineMode()) {   /* no AI archers online: no controls (DropAll took any AI out as online mode started) */
        static bool said = false;
        if (!said) { said = true; hw::Log("[aislot] online roll call: no AI archers (the AI controls are off in online games)"); }
        g_cursor = -1;
        return;
    }
    teams::Override();
    for (int j = 0; j < kSlots; j++) {   /* every human on archer select, still choosing or confirmed (the console: nobody had confirmed) */
        if (!g_elems[j]) continue;
        void* in = InputAt(j);
        if (!IsJoyCon(in) || !Attached(in)) continue;
        if (game::Virt<bool>(in, kAlt2)) Shortcut(j);
        static bool said[kSlots][2];   /* console evidence: which joined controllers the right-stick controls see */
        bool full = rightstick::Full(in);
        if (!said[j][full]) { said[j][full] = true; Log(full ? "[aislot] P%d on archer select: a right stick (the AI controls on it)" : "[aislot] P%d on archer select: no right stick (a single Joy-Con: L / ZL / SL only)", j + 1); }
        if (!full) continue;
        g_anyFull = true;
        Pad& p = g_pad[j];
        tfex::gen::Vector2 v = rightstick::Of(in);
        bool click = (rightstick::Buttons(in) & kStickClick) != 0;
        if (g_cursor < 0 || !AiAble(g_cursor)) { g_cursor = -1; for (int i = 0; i < kSlots; i++) if (AiAble(i)) { g_cursor = i; break; } }
        if (g_cursor < 0) { p.click = click; continue; }
        float ax = v.X < 0 ? -v.X : v.X, ay = v.Y < 0 ? -v.Y : v.Y;
        if (p.armedX && ax > 0.6f && ax > ay) {
            p.armedX = false;
            for (int k = 1; k < kSlots; k++) { int i = (g_cursor + (v.X > 0 ? k : kSlots - k)) % kSlots; if (AiAble(i)) { g_cursor = i; break; } }
        } else if (ax < 0.3f) p.armedX = true;
        if (p.armedY && ay > 0.6f && ay > ax) {
            p.armedY = false;
            if (g_slot[g_cursor].ai) NextArcher(g_cursor, v.Y > 0);   /* (Y down: down = the next archer) */
        } else if (ay < 0.3f) p.armedY = true;
        if (click && !p.click) { if (g_slot[g_cursor].ai) NextType(g_cursor); else AiOn(g_cursor); }
        p.click = click;
    }
}

HOOK_DEFINE_TRAMPOLINE(RollcallUpdate) {
    static void Callback(void* self) {
        int pi = gen::Fld<int32_t>(self, bind::f_RollcallElement_playerIndex);
        if (pi < 0 || pi >= kSlots) { Orig(self); return; }
        g_elems[pi] = self;
        if (pi == 0) Frame();
        Slot& s = g_slot[pi];
        if (void* in = InputAt(pi); in && IsAi(in) && !s.ai) {   /* an agent LoaderAI gave an empty slot at load: the slot stays empty */
            SetInputAt(pi, nullptr);
            la::currentPlayerType()->Set(pi, kNone.P());
            TakeInput(self);
            Log("[aislot] slot %d: LoaderAI's agent taken out (no controller: the slot stays empty until an AI is put there)", pi + 1);
        }
        s.now = None;
        if (s.n > 0) {
            s.now = s.queue[0];
            for (int k = 1; k < s.n; k++) s.queue[k - 1] = s.queue[k];
            s.n--;
            if (s.now == Swap) {   /* another AI type: its agent's input for this slot, taken as a controller change */
                la::currentPlayerType()->Set(pi, TypeAt(s.swapTo));
                void* ai = la::GetCurrentPlayerInput(pi);
                if (ai && IsAi(ai)) { s.ai = ai; SetInputAt(pi, ai); TakeInput(self); }
            }
            if (s.now == Confirm) FreeArcher(self, pi);
            if (s.now == Restore) {   /* the AI out: the slot's own input again (none, or an npad with nothing attached) */
                SetInputAt(pi, s.prev);
                la::currentPlayerType()->Set(pi, s.prevType ? s.prevType : kNone.P());
                s.ai = nullptr; s.prev = nullptr; s.prevType = nullptr; s.n = 0; s.retries = 0;
                TakeInput(self);
            }
        }
        Orig(self);
        s.now = None;
        /* the AI not joined with nothing queued (its confirm not taken): confirm again, a few times, logged */
        if (s.ai && s.n == 0 && s.now == None && StateOf(self) == kNotJoined) {
            if (s.retries < 20) {
                s.retries++;
                Queue(pi, {Wait, Confirm});
                static int said = 0;
                if (said < 8) { said++; hw::Log("[aislot] slot %d's AI not joined after its confirm: confirming again (%d)", pi + 1, s.retries); }
            } else if (s.retries == 20) {
                s.retries++;
                hw::Log("[aislot] ERROR slot %d's AI: 20 confirms not taken, left unjoined", pi + 1);
            }
        } else if (StateOf(self) == kJoined) s.retries = 0;
    }
};
/* the console's stick-click picture (the menu atlas's controls/buttons/stick-press, 16 x 16) centred on y from x left. The atlas has only the
 * left stick's (an L on its face): its L repainted as an R, pixel by pixel, in its own colours */
void DrawRStick(void* icon, float left, float y) {
    if (!icon) return;
    constexpr float kIcon = 16.0f;
    gen::Fn<void (*)(void*, gen::Vector2, gen::Color)>(bind::m_Draw_TextureCentered_39ab)(icon, gen::Vector2{left + kIcon / 2.0f, y}, gen::Color_White);
    float x0 = left, y0 = y - kIcon / 2.0f;
    auto px = [&](int x, int yy, uint32_t c) { gen::Fn<void (*)(float, float, float, float, gen::Color)>(bind::m_Draw_Rect_9c33)(x0 + x, y0 + yy, 1.0f, 1.0f, gen::Color{c}); };
    constexpr uint32_t kFace = 0xffd8d8d8u, kInk = 0xff3c3c3cu;   /* the icon's face (216) and glyph (60) greys */
    static const uint8_t kL[][2] = { {6,6}, {6,7},{7,7}, {6,8},{7,8}, {6,9},{7,9},{8,9},{9,9},{10,9}, {6,10},{7,10},{8,10},{9,10},{10,10} };
    static const uint8_t kR[][2] = { {6,7},{7,7},{8,7},{9,7},   /* (row 6: the arrow's bottom; the L's top pixel there: face) */ {6,8},{7,8},{10,8}, {6,9},{7,9},{8,9},{9,9}, {6,10},{7,10},{9,10}, {6,11},{7,11},{10,11} };
    for (auto& p : kL) px(p[0], p[1], kFace);
    for (auto& p : kR) px(p[0], p[1], kInk);
}

/* ---- Teams+'s team screen: the right stick moves an AI between the columns ----
 * Teams+'s ThreeTeamSelector (translated) reads TFGame.PlayerInputs[its player].MenuLeft / MenuRight in its Update; an AI's input answers
 * them from g_slot[].now (AiLeft / AiRight). Its Update and Render are wrapped through its vtable: the first selector's Update of a frame
 * reads the humans' right sticks (up / down: the AI cursor; left / right: that AI's next column), each AI selector's Update gets its
 * pulse for that call, and the picked AI's Render draws the right-stick picture over its P label. */
namespace teams {
int g_cursor = -1;          /* the AI slot the right stick moves */
Act g_pulse[kSlots] = {};   /* this frame's left / right for that AI's selector */
uint32_t g_seen = 0;        /* selectors updated this frame (one seen again: a new frame) */
bool g_any = false;         /* a human with a right stick on the team screen */
bool g_overridden = false;
void Sticks() {
    g_any = false;
    for (int j = 0; j < kSlots; j++) {
        void* in = InputAt(j);
        if (!IsJoyCon(in) || !Attached(in) || !rightstick::Full(in)) continue;
        g_any = true;
        if (g_cursor < 0 || !g_slot[g_cursor].ai) { g_cursor = -1; for (int i = 0; i < kSlots; i++) if (g_slot[i].ai) { g_cursor = i; break; } }
        Pad& p = g_pad[j];
        tfex::gen::Vector2 v = rightstick::Of(in);
        float ax = v.X < 0 ? -v.X : v.X, ay = v.Y < 0 ? -v.Y : v.Y;
        if (g_cursor < 0) { if (ax < 0.3f) p.armedX = true; if (ay < 0.3f) p.armedY = true; continue; }
        if (p.armedY && ay > 0.6f && ay > ax) {   /* up / down: the previous / next AI (Y down) */
            p.armedY = false;
            for (int k = 1; k < kSlots; k++) { int i = (g_cursor + (v.Y > 0 ? k : kSlots - k)) % kSlots; if (g_slot[i].ai) { g_cursor = i; break; } }
        } else if (ay < 0.3f) p.armedY = true;
        if (p.armedX && ax > 0.6f && ax > ay) {   /* left / right: that AI one column over */
            p.armedX = false;
            g_pulse[g_cursor] = v.X > 0 ? Right : Left;
            static int said = 0;
            if (said++ < 6) hw::Log("[aislot] team screen: P%d's right stick moves slot %d's AI %s", j + 1, g_cursor + 1, v.X > 0 ? "right" : "left");
        } else if (ax < 0.3f) p.armedX = true;
    }
}
void SelUpdate(void* self) {
    int pi = tfex::gen::Tp_Selector::S(self)->playerIndex;
    if (pi >= 0 && pi < kSlots) {
        if (g_seen & (1u << pi)) g_seen = 0;
        if (!g_seen) Sticks();
        g_seen |= 1u << pi;
    }
    const bool ai = pi >= 0 && pi < kSlots && g_slot[pi].ai && g_slot[pi].ai == InputAt(pi);
    if (ai) { g_slot[pi].now = g_pulse[pi]; g_pulse[pi] = None; }
    tfex::gen::Tp_Selector::Update(self);
    if (ai) g_slot[pi].now = None;
}
void SelRender(void* self) {
    tfex::gen::Tp_Selector::Render(self);
    int pi = tfex::gen::Tp_Selector::S(self)->playerIndex;
    if (!g_any || pi != g_cursor || pi < 0 || pi >= kSlots || !g_slot[pi].ai) return;
    gen::Vector2 at = gen::Fld<gen::Vector2>(self, bind::f_Entity_Position);
    DrawRStick(tfex::gen::svc::SwitchMenuIcon("stick-press"), (float)(int)(at.X - 8.0f), (float)(int)(at.Y - 16.0f));
}
/* the wrappers into the selector's vtable, once its class is made (on the roll call: a menu, before the team screen makes selectors) */
void Override() {
    if (g_overridden || !tfex::gen::Tp_Selector::EnsureClass()) return;
    gen::Class& c = tfex::gen::Tp_Selector::ClassOf();
    gen::Override(c, bind::v_Entity_Update, reinterpret_cast<void*>(&SelUpdate));
    gen::Override(c, bind::v_Entity_Render, reinterpret_cast<void*>(&SelRender));
    g_overridden = true;
    hw::Log("[aislot] Teams+'s team screen: the right stick picks an AI (up / down) and moves its team (left / right)");
}
}  // namespace teams

/* the cursor: over the highlighted slot what the click does, while a joined player is on a full controller */
HOOK_DEFINE_TRAMPOLINE(RollcallRender) {
    static void Callback(void* self) {
        Orig(self);
        int pi = gen::Fld<int32_t>(self, bind::f_RollcallElement_playerIndex);
        if (!g_anyFull || pi != g_cursor || pi < 0 || pi >= kSlots) return;
        gen::Vector2 at = gen::Fld<gen::Vector2>(self, bind::f_Entity_Position);
        at.Y += g_elems[6] ? 46.0f : 76.0f;   /* under the portrait, below its gem (the 8-slot roll call's 60 x 60 portraits; the user, 2026-11-07) */
        void* font = (gen::Cctor(bind::c_TFGame), gen::Fn<void* (*)()>(bind::m_TFGame_get_Font)());
        /* what the click does: add the first AI type, the next type, or (after the last) take the AI out */
        int k = g_slot[pi].ai ? TypeOf(pi) : -1;
        void* text = !g_slot[pi].ai ? (TypeCount() ? gen::Concat(kAdd.P(), TypeAt(0)) : kAdd.P())
                   : k >= 0 && k + 1 < TypeCount() ? gen::Concat(kNext.P(), TypeAt(k + 1)) : kRemoveAi.P();
        /* the right-stick picture (DrawRStick), then what the click does, centered together */
        constexpr float kIcon = 16.0f, kGap = 3.0f;
        void* icon = tfex::gen::svc::SwitchMenuIcon("stick-press");
        float w = gen::Fn<gen::Vector2 (*)(void*, void*)>(bind::m_SpriteFont_MeasureString_0)(font, text).X;
        float left = (float)(int)(at.X - ((icon ? kIcon + kGap : 0.0f) + w) / 2.0f);
        at.Y = (float)(int)at.Y;
        DrawRStick(icon, left, at.Y);
        gen::Fn<void (*)(void*, void*, gen::Vector2, gen::Color, gen::Color, float)>(bind::m_Draw_OutlineTextCentered_cbf5)(
            font, text, gen::Vector2{left + (icon ? kIcon + kGap : 0.0f) + w / 2.0f, at.Y}, gen::Color_White, gen::Color_Black, 1.0f);
    }
};
/* the player marker above an AI in a level ("P3" on the game's PlayerIndicator): LoaderAI's name for it (GetAIPlayerName: "JIMMY 3"), as
 * Archer's MyPlayerIndicator shows its player names there on the PC (Archer itself isn't on the Switch: its names part, for AIs only) */
constexpr uintptr_t kPlayerIndicatorCtor = 0xb04bf0;   /* TowerFall.PlayerIndicator::.ctor(Vector2 offset, int playerIndex, bool crown) (tools/re callmap) */
HOOK_DEFINE_TRAMPOLINE(PlayerIndicatorCtor) {
    static void* Callback(void* self, gen::Vector2 offset, int32_t playerIndex, bool crown) {
        void* r = Orig(self, offset, playerIndex, crown);
        void* obj = self ? self : r;
        if (obj && playerIndex >= 0 && playerIndex < kSlots && IsAi(InputAt(playerIndex))) {
            if (void* name = la::GetAIPlayerName(playerIndex)) {
                gen::Fld<void*>(obj, bind::f_PlayerIndicator_text) = name;
                Log("[aislot] P%d's marker: LoaderAI's AI name", playerIndex + 1);
            }
        }
        return r;
    }
};
/* the round results' "P3" texts: LoaderAI's AI names (glue AiNames: Archer's MyVersusRoundResults prefix, AIs only) */
constexpr uintptr_t kVersusRoundResultsUpdate = 0xbb74e0;   /* TowerFall.VersusRoundResults::Update (tools/re callmap) */
HOOK_DEFINE_TRAMPOLINE(RoundResultsUpdate) {
    static void Callback(void* self) { if (self) tfex::gen::GlueAiNames::RoundResults(self); Orig(self); }
};
/* the AI input's getters in place as soon as one is made (LoaderAI's Input runs KeyboardInput's constructor first, its class made
 * already): TF.EX's InputService scans every TFGame.PlayerInputs entry for menu activity from the main menu on, and LoaderAI puts its
 * inputs there when the game has loaded (its TFGame.Update prefix) */
HOOK_DEFINE_TRAMPOLINE(KeyboardInputCtor) {
    static void* Callback(void* self) { void* r = Orig(self); Override(); return r; }
};
}  // namespace

void* AiNameOf(int32_t playerIndex) {
    if (playerIndex < 0 || playerIndex >= kSlots || !IsAi(InputAt(playerIndex))) return nullptr;
    return la::GetAIPlayerName(playerIndex);
}

void BoardLaidOut(int32_t names, int32_t labels) {
    static int boards = 0;
    if (++boards <= 3 || boards % 50 == 0)
        hw::Log("[aislot] round results: %d AI name(s) of %d labels, rows laid out in the game's place (board %d)", names, labels, boards);
}

void DropAll() {
    int dropped = 0, n;
    void** d = Inputs(&n);
    for (int i = 0; i < kSlots; i++) {
        Slot& s = g_slot[i];
        bool tracked = s.ai != nullptr, ai = d && i < n && IsAi(d[i]);
        if (ai) { d[i] = tracked ? s.prev : nullptr; dropped++; }
        if ((tracked || ai) && la::currentPlayerType()) la::currentPlayerType()->Set(i, tracked && s.prevType ? s.prevType : kNone.P());
        s = Slot{};
        g_elems[i] = nullptr;   /* the old roll call's elements: the next roll call's Update records its own */
    }
    g_cursor = -1;
    g_emptying = false;
    if (dropped) hw::Log("[aislot] online mode: %d AI archer(s) taken out of the player slots (no AI archers in online games)", dropped);
}

/* ---- TEST ONLY (hw.txt "aiseat <n>"): LoaderAI's AI of type n plays the Switch's local seats in an online match ----
 * As LoaderAI's AgentUpdate (its Level.Update prefix) does for an AI slot: the seat's agent gets the level (SetLevel) and decides
 * (Play) on the newest state, once per GGRS frame, outside the level's re-simulated updates; its input's GetState then goes through
 * TF.EX's own InputService.UpdatePolledInput (its KeyboardInput.GetState postfix's step), in place of the test script. The AI only
 * reads the game (no game RNG, no entities added: LoaderAI, AIJimmy, AINova), so the match's state stays TF.EX's on both sides. */
namespace { int g_aiSeat = 0; void* g_seatLevel[kSlots] = {}; int g_seatFrame[kSlots]; bool g_seatPlayed[kSlots] = {}; int g_seatLogged = 0; }
void SetAiSeat(int n) { g_aiSeat = n; hw::Log("[aiseat] hw.txt aiseat %d: LoaderAI's AI type %d plays the Switch's online seats (TEST ONLY)", n, n); }
bool AiSeatOn() { return g_aiSeat > 0; }
bool AiSeatState(int32_t seat, int frame, gen::GameVal<28>* out) {
    if (seat < 0 || seat >= kSlots || TypeCount() < g_aiSeat) {
        if (g_seatLogged < 3) { g_seatLogged++; hw::Log("[aiseat] ERROR seat %d: no AI (LoaderAI has %d AI types, aiseat asks for %d)", seat, TypeCount(), g_aiSeat); }
        return false;
    }
    void* level = gen::As(gen::Fn<void* (*)(void*)>(bind::m_Engine_get_Scene)((gen::Cctor(bind::c_Engine), gen::Fn<void* (*)()>(bind::m_Engine_get_Instance)())), gen::TypeOf(bind::t_Level));
    if (!level) return false;
    void* type = TypeAt(g_aiSeat - 1);
    if (!gen::StrEq(la::currentPlayerType()->Get(seat), type)) la::currentPlayerType()->Set(seat, type);
    void* agent = la::listAgentByType()->Get(type)[seat];
    if (!agent) return false;
    if (g_seatLevel[seat] != level) {
        g_seatLevel[seat] = level; g_seatPlayed[seat] = false;
        gen::Virt<void>(agent, tfex::gen::Auto_Agent::kSlot_SetLevel, level);
        char tn[24] = ""; gen::Utf8Of(type, tn, sizeof tn);
        hw::Log("[aiseat] seat %d: LoaderAI's %s agent on this level", seat, tn);
    }
    if (!g_seatPlayed[seat] || g_seatFrame[seat] != frame) {   /* once per GGRS frame: its decision on the newest state */
        bool playing = gen::Fn<int32_t (*)(void*)>(bind::m_Level_get_LivingPlayers)(level) > 0 && la::IsAgentPlaying(seat, level)
                    && !gen::Fn<bool (*)(void*)>(bind::m_Level_get_Paused)(level) && !gen::Fn<bool (*)(void*)>(bind::m_Level_get_Frozen)(level)
                    && !gen::Fld<bool>(level, bind::f_Level_Ending);
        if (playing) {
            gen::Virt<void>(agent, tfex::gen::Auto_Agent::kSlot_Play);
            if (!g_seatPlayed[seat]) hw::Log("[aiseat] seat %d: the AI plays (frame %d)", seat, frame);
            g_seatPlayed[seat] = true;
        }
        g_seatFrame[seat] = frame;
    }
    *out = tfex::gen::Auto_Input::GetState(gen::Virt<void*>(agent, tfex::gen::Auto_Agent::kSlot_getInput));
    return true;
}

void Install() {
    KeyboardInputCtor::InstallAtOffset(bind::m_KeyboardInput__ctor);
    RollcallUpdate::InstallAtOffset(bind::m_RollcallElement_Update);
    RollcallRender::InstallAtOffset(bind::m_RollcallElement_Render);
    PlayerIndicatorCtor::InstallAtOffset(kPlayerIndicatorCtor);
    RoundResultsUpdate::InstallAtOffset(kVersusRoundResultsUpdate);
    hw::Log("[aislot] archer select: a full controller's right stick puts LoaderAI's AI players in the empty slots");
}
}  // namespace tfex::aislot
#endif
