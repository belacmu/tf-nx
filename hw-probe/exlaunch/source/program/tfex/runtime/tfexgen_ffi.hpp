/* The C side of TF.EX's GGRS FFI wrappers (network/TF.EX.Domain/Models: EventsImpl, InputsImpl, NetplayRequestsImpl, StatusImpl /
 * InfoHandle), for the generated NetplayManager calling ggrs_ffi directly (the ggrs-ffi port; translator construct 3). TF.EX marshals
 * the native arrays into managed ones and frees them in Dispose; these do the same. ggrs_ffi's own free functions are called here,
 * once per handle, as TF.EX's SafeHandle.ReleaseHandle does. Not wired until the port replaces the GGRSFFI.* wildcard: the rule lines
 * are in docs/ws-ggrs-ffi.md "Translator construct 3". */
#pragma once
#include "tfexgen_code_rt.hpp"
#include "tfexgen_structs.hpp"
#include "tfexgen_binding.hpp"
#include <type_traits>

extern "C" {
void netplay_events_free(tfex::gen::st::Events events);
void netplay_inputs_free(tfex::gen::st::Inputs inputs);
void netplay_requests_free(tfex::gen::st::NetplayRequets requests);
void status_info_free(intptr_t info);
tfex::gen::st::Status netplay_reset();
void netplay_free_game_state(tfex::gen::st::SafeBytesFFI bytes);
tfex::gen::st::Inputs netplay_advance_game_state();   /* (the netplay_reset rule: after xplay::EndLobbyMatch) */
}

namespace tfex::gen::ffi {
/* EventsImpl(events)._events: the C strings as a managed string[] (Marshal.PtrToStringAnsi each); then Dispose = netplay_events_free */
gen::Arr<void*> EventStrings(const st::Events& events);
inline void FreeEvents(const st::Events& events) { if (events.data) { netplay_events_free(events); const_cast<st::Events&>(events).data = 0; } }
/* The frees clear the held struct (C#'s SafeHandle frees once: TF.EX's GetEventAndUpdate calls Dispose inside its `using`, which disposes
 * again; a second netplay_events_free of the same array corrupted the heap, Ryujinx 2026-10-18 19:11) */
/* InputsImpl / NetplayRequestsImpl: the native element array, as a view (TF.EX copies it with Marshal.PtrToStructure per element) */
inline const st::Input* InputsData(const st::Inputs& in) { return reinterpret_cast<const st::Input*>(in.data); }
inline void FreeInputs(const st::Inputs& in) { if (in.data) { netplay_inputs_free(in); const_cast<st::Inputs&>(in).data = 0; } }
inline void FreeRequests(const st::NetplayRequets& r) { if (r.data) { netplay_requests_free(r); const_cast<st::NetplayRequets&>(r).data = 0; } }
/* StatusImpl.IsOk / InfoHandle.AsString(): the status's message as a managed string (null without one); then status_info_free */
inline bool IsOk(const st::Status& s) { return s.is_ok != 0; }
void* StatusInfo(const st::Status& s);
inline void FreeStatus(const st::Status& s) { if (s.info) { status_info_free(s.info); const_cast<st::Status&>(s).info = 0; } }
/* SafeBytes<GGRSConfig>: a managed string's UTF-8 in a pooled buffer (2 slots of 4 KB: netplay_init takes one at a time; a full pool or an
 * oversize string is logged and gives an empty buffer); BytesFFI = ggrs_ffi's SafeBytesFFI view; FreeBytes returns the slot */
void* BytesOfString(void* managed);
st::SafeBytesFFI BytesFFI(void* bytes);
void FreeBytes(void* bytes);
/* ToModel / ToModelGGrsFFI on an FFI result: TF.EX wraps the returned struct (often a call's temporary) in a handle object; here the
 * handle is a copy in a ring (16 per type: TF.EX's wrappers live for one `using` or one expression), and the member rules read it */
template <class T> void* Hold(const T& v) {
    static T ring[16];
    static int next = 0;
    T* slot = &ring[__atomic_fetch_add(&next, 1, __ATOMIC_RELAXED) & 15];   /* (Connect's worker thread and the game thread both hold results) */
    if constexpr (std::is_same_v<T, st::Status>) FreeStatus(*slot);   /* a status nobody disposed (IsSynchronized, IsDisconnected: every frame)
                                                                       * frees its message when its slot comes round again */
    *slot = v;
    return slot;
}
/* InputsImpl._inputs / NetplayRequestsImpl._requests: the native arrays as the runtime's value lists (VList, as the generated code keeps
 * TF.EX's List<Input> / List<NetplayRequest>); more than the list holds is a logged pool error (VList.Add) */
gen::VList<st::Input>* InputList(const st::Inputs& in);
gen::VList<int32_t>* RequestList(const st::NetplayRequets& r);
/* SafeRawBytes (NetplayManager.SaveGameState / LoadGameState). TFEX-DIFF (performance): on the Switch the "state" is not TF.State's bytes
 * but the 16-byte handle of rollback.cpp's memory snapshot (docs/ws-ggrs-ffi.md "save/load contract"); ggrs_ffi stores and returns it
 * as it would TF.State's bytes. RawOfArr: new SafeRawBytes(byte[]); RawOfFFI: new SafeRawBytes(ggrs_ffi's bytes, cleanup) (the cleanup
 * is netplay_free_game_state: FreeRaw calls it); RawFFI: ToBytesFFI; RawToArr: PtrToBytes (a copy, at most 64 bytes); FreeRaw: Dispose */
void* RawOfArr(const gen::Arr<uint8_t>& bytes);
void* RawOfFFI(st::SafeBytesFFI bytes);
st::SafeBytesFFI RawFFI(void* raw);
gen::Arr<uint8_t> RawToArr(void* raw);
void FreeRaw(void* raw);
/* GGRSFFI.IsInInit: true while Connect (on the worker thread) waits in netplay_init, which holds ggrs_ffi's lock for that time; TF.EX's
 * IsSynchronized / IsDisconnected answer without calling in meanwhile, and so does the Switch's gate (netplay_gate.cpp) */
bool InInit();
void SetInInit(bool v);
/* a status's message as C text (for the Switch's logs) */
void StatusText(const st::Status& s, char* buf, int cap);
/* each string of TF.EX's _events (a managed List<string>: GetEventAndUpdate's copy of this frame's ggrs_ffi events), as UTF-8 */
template <class F> void ForEachEvent(void* list, F&& f) {
    if (!list) return;
    char buf[256];
    gen::FromList<void*>(list, bind::m_List_1_eb65_get_Count, bind::m_List_1_eb65_get_Item_e2bf)([&](void* str) {
        if (str) { gen::Utf8Of(str, buf, sizeof buf); f(static_cast<const char*>(buf)); }
        return true;
    });
}
/* a log line from the glue (RunWorker refusing a second worker) */
void Log(const char* line);
}
