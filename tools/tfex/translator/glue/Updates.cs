// TF.EX's update flow on the Switch (TFGamePatch's NETPLAY check, StartUpdate, HandleAutoUpdate; translate.toml [[rewrite]]), with TF.NX's
// updater underneath (program/tfnxupdate.cpp: TF.EX's IAutoUpdater for the Switch's install; docs/PLAN-UPDATER-2026-11-07.md).
using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using Monocle;
using MonoMod.Utils;
using TowerFall;

namespace TfexGlue
{
    public static class Updates
    {
        static Thread worker;
        static TF.EX.Domain.CustomComponent.UpdaterDialog dialog;
        static OptionsButton entry;

        [Cpp("tfex::gen::ffi::Log(\"[update] a check or download already runs: this one dropped\")", Include = "tfexgen_ffi.hpp")]
        static extern void LogBusy();
        [Cpp("tfnxupdate::RestartNeeded()", Include = "../../tfnxupdate.hpp")] static extern void RestartNeeded();
        [Cpp("(int32_t)tfnxupdate::UiState()", Include = "../../tfnxupdate.hpp")] static extern int UiState();
        [Cpp("gen::ManagedUtf8(tfnxupdate::LatestText(), (int)std::strlen(tfnxupdate::LatestText()))", Include = "../../tfnxupdate.hpp")] static extern string LatestText();
        [Cpp("gen::ManagedUtf8(tfnxupdate::CurrentText(), (int)std::strlen(tfnxupdate::CurrentText()))", Include = "../../tfnxupdate.hpp")] static extern string CurrentText();
        [Cpp("tfnxupdate::CheckNow()", Include = "../../tfnxupdate.hpp")] static extern void CheckNow();
        [Cpp("tfnxupdate::AutoStarted()", Include = "../../tfnxupdate.hpp")] static extern void AutoStarted();
        [Cpp("tfex::gen::NetplayEntry::StartUpdate($0, tfex::gen::svc::Service(), tfex::gen::svc::Service())", Include = "tfexgen_NetplayEntry.hpp")]
        static extern void StartUpdate(TowerFall.MainMenu menu);

        // `Task.Run(async () => { await autoUpdater.CheckForUpdate(); ... })` and StartUpdate's `Task.Run(async () => { ... DownloadAndApply ... })`:
        // TFEX-DIFF S-native: a game System.Threading.Thread (managed, so the GC knows it), one at a time, as Reports.RunUpload.
        public static void Run(Func<Task> work)
        {
            if (worker != null && worker.IsAlive) { LogBusy(); return; }
            worker = new Thread(() => work());
            worker.IsBackground = true;
            worker.Start();
        }

        // StartUpdate's `dialog.SetPhase("RESTARTING")`: TFEX-DIFF S-update: the console can't relaunch the game for us (the user chose a
        // notice): the dialog says to restart, then (tfnxupdate.cpp, 3 s later) Dismiss takes it away.
        public static void RestartPhase(TF.EX.Domain.CustomComponent.UpdaterDialog d, string phase)
        {
            dialog = d;
            d.SetPhase("RESTART THE GAME TO FINISH");
        }

        // HandleAutoUpdate's `FortRise.RiseCore.WillRestart = true; instance.Exit();` (TF.EX restarts the PC game through FortRise): the
        // updater notes the restart is the player's (its main-menu notice)
        public static void Restart(TFGame game) => RestartNeeded();

        // 3 s after "restart the game": the dialog goes and the controllers come back (TF.EX's StartUpdate took them; on the PC the game exits)
        public static void Dismiss()
        {
            if (dialog != null) dialog.RemoveSelf();
            dialog = null;
            TF.EX.Domain.ServiceCollections.ResolveInputService().EnableAllControllers();
        }

        // MatchmakingService's `?version={autoUpdater.GetCurrentVersion()}`: TF.EX's version (the server knows TF.EX's versions)
        [Cpp("tfex::gen::svc::TfexVersion()", Include = "tfexgen_services.hpp")]
        public static extern Version ServerVersion(TF.EX.Common.IAutoUpdater updater);

        // TF.NX's OPTIONS entry (after TF.EX's settings, AUTO UPDATE among them): TF.NX's version and the update state; Confirm downloads a
        // newer release (TF.EX's dialog), else checks again. A TF.NX addition: TF.EX has no manual download (the user, 2026-11-07).
        public static void CreateEntry(List<OptionsButton> buttons)
        {
            var button = new OptionsButton("TF.NX " + CurrentText());
            entry = button;
            shownUi = -1;
            button.SetCallbacks(() => { button.State = StateText(); }, null, null, () =>
            {
                int ui = UiState();
                if (ui == 3)
                {
                    AutoStarted();
                    StartUpdate(button.MainMenu);
                    return true;
                }
                if (ui == 2 || ui == 5 || ui == 6) CheckNow();
                return true;
            });
            buttons.Add(button);
            DynamicData.For(button).Set("fortrise_description",
                "A NEW TF.NX VERSION FROM GITHUB SHOWS HERE: PRESS \n TO DOWNLOAD IT, THEN RESTART THE GAME");
        }

        // short: the game centres a state 30 px right of the name, so about 10 characters fit (the description says the rest)
        static string StateText()
        {
            switch (UiState())
            {
                case 0: return "DEV SERVER";
                case 1: return "CHECKING";
                case 2: return "UP TO DATE";
                case 3: return "V" + LatestText();
                case 4: return "LOADING";
                case 5: return "RESTART";
                default: return "FAILED";
            }
        }

        // each frame on the main menu (tfnxupdate.cpp): the entry's state, live (an OptionsButton redraws its State only after a press)
        static int shownUi = -1;
        public static void Refresh()
        {
            if (entry == null) return;
            int ui = UiState();
            if (ui == shownUi) return;   // (no string made each frame)
            shownUi = ui;
            entry.State = StateText();
        }
    }
}
