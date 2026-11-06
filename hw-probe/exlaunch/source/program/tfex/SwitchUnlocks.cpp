/* SwitchUnlocks.cpp: the Switch edition's own unlock events kept out of a TF.EX session (TFEX-DIFF S-native).
 *
 * TF.State's UnlockDataPatch (generated: gen/tfexgen_State_UnlockData, SkipInExSession) answers false in a TF.EX session for the PC's
 * save-driven unlock events (ShouldOpenPurpleArcherPortal, ShouldShowMoonBreakSequence, ...): each machine's save differs, and a
 * sequence that runs on one side only splits the match. The Switch edition has two more that TowerFall.exe doesn't (no ArcherShowdown
 * class on the PC): UnlockData.ShouldStartRedArcherShowdown / ShouldStartCyanArcherShowdown (+0xb99860 / +0xb99470, read by
 * LevelLoaderXML at +0xa8273c / +0xa825d8). With a red or cyan archer still locked in the save, the level gets an ArcherShowdown whose
 * Sequence waits for Level.Ending, then sets CanEnd = false and opens a portal: the round can't end. Console 2026-11-01 (against TF.EX
 * on a PC): the PC's match-winning kill ended the PC's round and the match; the Switch's round never ended (Level+0x89 CanEnd 0 from
 * that frame, vs-log end=10), DesyncDetected at the next checksum. Here both getters follow SkipInExSession's rule. */
#include "lib.hpp"
#include "../hwlog.hpp"
#include "gen/tfexgen_State_LocalSettingsGuard.hpp"
#include <cstdint>

namespace tfex::switchunlocks {
namespace {
constexpr uintptr_t kShouldStartCyanArcherShowdown = 0xb99470, kShouldStartRedArcherShowdown = 0xb99860;
bool Skip(const char* which, bool original) {   /* SkipInExSession: false in a TF.EX session, the save's answer otherwise */
    if (!tfex::gen::State_LocalSettingsGuard::IsExSession()) return original;
    static int logged = 0;
    if (logged++ < 4) hw::Log("[unlock] UnlockData.ShouldStart%sArcherShowdown: false in a TF.EX session (the save said %d; S-native, as TF.State's SkipInExSession)", which, (int)original);
    return false;
}
HOOK_DEFINE_TRAMPOLINE(CyanShowdownHook) { static bool Callback(void* self) { return Skip("Cyan", Orig(self)); } };
HOOK_DEFINE_TRAMPOLINE(RedShowdownHook) { static bool Callback(void* self) { return Skip("Red", Orig(self)); } };
}  // namespace

void Install() {
    CyanShowdownHook::InstallAtOffset(kShouldStartCyanArcherShowdown);
    RedShowdownHook::InstallAtOffset(kShouldStartRedArcherShowdown);
}
}  // namespace tfex::switchunlocks
