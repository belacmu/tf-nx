/* The hardware probe: one native module for the console visit (the game takes one exefs mod at a time).
 *   xplay.cpp  the game hooks: math/rounding/timestep fixes, the scripted Trials run, update timing,
 *              the rollback drill and the changed-page tracker (sd:/xplay/script.txt, hw.txt)
 *   net.cpp    a thread: TCP/TLS/WebSocket-relay/GGRS tests (sd:/xplay/net.txt)
 *   hwlog.cpp  the shared log: sd:/xplay/hw-log.txt
 *   tfex/      TF.EX's lobby service and models, mirrored (switch-online/LOBBY-PROTOCOL.md, TFEX-UI-SPEC.md)
 * See switch-online/hw-probe/README.md. */
#include "lib.hpp"
#include "loggers.hpp"
#include <common.hpp>
#include "hwlog.hpp"
#include "devupdate.hpp"
#include "tfex/gen/tfexgen_version.hpp"   /* TFEX_VERSION */
#ifndef TFNX_BUILD
#define TFNX_BUILD "0"   /* (hw-probe/exlaunch/config.mk sets it) */
#endif
#define TFNX_VERSION TFEX_VERSION "." TFNX_BUILD   /* TF.NX's version: TF.EX's plus its build number (0.29.0.0) */

void xplay_main();
void net_main();
void rnd_main();
void dm_main();
namespace tfex::frmods { void Install(); void EarlyMods(); }
namespace tfex::frvariants { void Install(); }
namespace tfex::frmodes { void Install(); }
namespace tfex::inlined { void Install(); }
namespace tfex::aislot { void Install(); }
namespace tfex::gen::statert { void InstallThrowLog(); }
namespace tfex::switchunlocks { void Install(); }
#include "megamod/megamod.hpp"
#if XPLAY_EXTRAS
#define XPLAY_BUILD_NAME "TF.EX Switch + extras"
#else
#define XPLAY_BUILD_NAME "TF.EX Switch"
#endif
namespace gfxmem { void Install(); void FlushIfLow(); }
namespace plugins { void Install(); }

extern "C" void exl_main(void* x0, void* x1) {
    exl::hook::Initialize();
    Logging.Log("[hw] hardware probe injected (built " __DATE__ " " __TIME__ ", " XPLAY_BUILD_NAME ", TF.NX " TFNX_VERSION ", TF.EX " TFEX_VERSION ")");
    hw::Append("[hw] hardware probe injected (built " __DATE__ " " __TIME__ ", " XPLAY_BUILD_NAME ", TF.NX " TFNX_VERSION ", TF.EX " TFEX_VERSION ")");
    xplay_main();
    tfex::gen::statert::InstallThrowLog();   /* every managed throw logged first-chance, bounded (tfex/runtime/tfexgen_state.cpp) */
    gfxmem::Install();   /* more NVN command memory for a busy frame (gfxmem.cpp: the 240 Hz abort) */
    rnd_main();
    dm_main();
    net_main();
    tfex::frmods::Install();   /* FortRise content mods from sd:/xplay/mods (tfex/FrModsBoot.cpp) */
    tfex::frvariants::Install();   /* FortRise's custom variants in every MatchVariants (tfex/FrVariants.cpp) */
    tfex::frmodes::Install();   /* the Switch's inlined team tests under FortRise's custom modes (tfex/FrModes.cpp) */
    tfex::inlined::Install();   /* TF.State's Layer.Remove postfix where the Switch inlined Remove(entity) (tfex/Inlined.cpp) */
    tfex::aislot::Install();   /* the console's button for LoaderAI's AI slots on archer select (tfex/AiSlotButton.cpp) */
    tfex::switchunlocks::Install();   /* the Switch edition's own unlock events off in a TF.EX session (tfex/SwitchUnlocks.cpp) */
    megamod::Install();   /* the local-play mods' patch-word table (megamod/megamod.hpp) */
    tfex::frmods::EarlyMods();   /* the catalog mods' early parts, after every hook above (mods/<name>/mod.toml: WiderSet's 8 seats) */
    plugins::Install();   /* code plugins: sd:/xplay/mods/<Mod>/plugin.elf, after every built-in hook (plugins.cpp) */
    devupdate::Start();   /* dev updates over Wi-Fi, only with sd:/xplay/devserver.txt (devupdate.cpp) */
}

extern "C" NORETURN void exl_exception_entry() {
    EXL_ABORT("Default exception handler called!");
}
