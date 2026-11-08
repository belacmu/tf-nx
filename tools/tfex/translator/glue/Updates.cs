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
        static TF.EX.Domain.CustomComponent.UpdaterDialog dialog;
        static OptionsButton entry;

        [Cpp("tfex::gen::ffi::Log(\"[update] a check or download already runs: this one dropped\")", Include = "tfexgen_ffi.hpp")]
        static extern void LogBusy();
        [Cpp("tfnxupdate::RestartNeeded()", Include = "../../tfnxupdate.hpp")] static extern void RestartNeeded();
        [Cpp("(int32_t)tfnxupdate::UiState()", Include = "../../tfnxupdate.hpp")] static extern int UiState();
        [Cpp("gen::ManagedUtf8(tfnxupdate::LatestText(), (int)std::strlen(tfnxupdate::LatestText()))", Include = "../../tfnxupdate.hpp")] static extern string LatestText();
        [Cpp("gen::ManagedUtf8(tfnxupdate::CurrentText(), (int)std::strlen(tfnxupdate::CurrentText()))", Include = "../../tfnxupdate.hpp")] static extern string CurrentText();
        [Cpp("(void*)tfnxupdate::FailureReason()", Include = "../../tfnxupdate.hpp")] static extern string Failure();
        [Cpp("tfnxupdate::CheckNow()", Include = "../../tfnxupdate.hpp")] static extern void CheckNow();
        [Cpp("tfnxupdate::AutoStarted()", Include = "../../tfnxupdate.hpp")] static extern void AutoStarted();
        [Cpp("tfex::gen::NetplayEntry::StartUpdate($0, tfex::gen::svc::Service(), tfex::gen::svc::Service())", Include = "tfexgen_NetplayEntry.hpp")]
        static extern void StartUpdate(TowerFall.MainMenu menu);

        // `Task.Run(async () => { await autoUpdater.CheckForUpdate(); ... })` and StartUpdate's `Task.Run(async () => { ... DownloadAndApply ... })`.
        // TFEX-DIFF S-native: the Switch build's managed Thread runs its work on the caller (glue Ffi.cs), so a Thread here ran the download on
        // the game thread: the main menu froze for the whole download (the console, 2026-11-08). The network part runs on the updater's own
        // thread (tfnxupdate.cpp) instead: the download (StartUpdate's dialog came just before: NewDialog) or a check that isn't fresh; TF.EX's
        // work runs on the game thread once it's done (Pump, every frame), where DownloadAndApply / CheckForUpdate answer at once. One at a time.
        static Func<Task> pending;
        static bool pendingDownload, downloadNext;
        static TF.EX.Domain.CustomComponent.UpdaterDialog progressDialog;
        static int shownSeq;
        [Cpp("tfnxupdate::StartDownload()", Include = "../../tfnxupdate.hpp")] static extern void StartDownload();
        [Cpp("tfnxupdate::DownloadState()", Include = "../../tfnxupdate.hpp")] static extern int DownloadState();
        [Cpp("tfnxupdate::PhaseSeq()", Include = "../../tfnxupdate.hpp")] static extern int PhaseSeq();
        [Cpp("gen::ManagedUtf8(tfnxupdate::PhaseText(), (int)std::strlen(tfnxupdate::PhaseText()))", Include = "../../tfnxupdate.hpp")] static extern string PhaseText();
        [Cpp("tfnxupdate::DownloadedBytes()", Include = "../../tfnxupdate.hpp")] static extern long DownloadedBytes();
        [Cpp("tfnxupdate::TotalBytes()", Include = "../../tfnxupdate.hpp")] static extern long TotalBytes();
        [Cpp("tfnxupdate::IsStatusFresh()", Include = "../../tfnxupdate.hpp")] static extern bool IsFresh();
        [Cpp("tfnxupdate::Busy()", Include = "../../tfnxupdate.hpp")] static extern bool UpdaterBusy();
        [Cpp("tfnxupdate::CheckInBackground()", Include = "../../tfnxupdate.hpp")] static extern void CheckInBackground();
        [Cpp("tfnxupdate::SkipNextCheck()", Include = "../../tfnxupdate.hpp")] static extern void SkipNextCheck();

        // StartUpdate's `new UpdaterDialog(...)`: the Task.Run after it is the download
        public static TF.EX.Domain.CustomComponent.UpdaterDialog NewDialog(string title)
        {
            downloadNext = true;
            progressDialog = new TF.EX.Domain.CustomComponent.UpdaterDialog(title);
            return progressDialog;
        }

        public static void Run(Func<Task> work)
        {
            if (pending != null) { LogBusy(); return; }
            if (downloadNext)
            {
                downloadNext = false;
                pending = work;
                pendingDownload = true;
                shownSeq = PhaseSeq();
                StartDownload();
                return;
            }
            if (IsFresh()) { work(); return; }
            pending = work;
            pendingDownload = false;
            CheckInBackground();
        }

        // every frame (tfnxupdate::Frame): the download's phase and bytes into TF.EX's dialog, then TF.EX's work once the updater's part is done
        public static void Pump()
        {
            if (pending == null) return;
            if (pendingDownload)
            {
                if (progressDialog != null)
                {
                    int seq = PhaseSeq();
                    if (seq != shownSeq) { shownSeq = seq; progressDialog.SetPhase(PhaseText()); }
                    progressDialog.Report(DownloadedBytes(), TotalBytes());
                }
                if (DownloadState() != 3) return;
            }
            else
            {
                if (UpdaterBusy()) return;
                SkipNextCheck();
            }
            var work = pending;
            pending = null;
            work();
        }

        // StartUpdate's `dialog.SetPhase("RESTARTING")`: TFEX-DIFF S-update: the console can't relaunch the game for us (the user chose a
        // notice): the dialog says to restart and stays until the game is restarted (tfnxupdate.cpp RestartNeeded; the test dismisses it).
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

        // HandleAutoUpdate's `_updaterDialog?.RemoveSelf()` when the update failed: a download that fails in the frame it started (GitHub
        // answering 504 minutes after a release was made, 2026-11-08: the console crashed) finds the dialog not yet in the scene (Monocle
        // adds an entity at the next frame), and RemoveSelf's Scene.Remove dereferences a null Scene (the PC's too). TFEX-DIFF S-native: such a
        // dialog is hidden now and removed once the scene has it (Refresh, every main-menu frame); one in the scene goes as TF.EX's does
        static Entity removeWhenAdded;
        public static void RemoveDialog(Entity dialog)
        {
            if (dialog.Scene != null) { dialog.RemoveSelf(); return; }
            dialog.Visible = false;
            removeWhenAdded = dialog;
            FailedEarly();
        }
        [Cpp("tfex::gen::ffi::Log(\"[update] the update failed before its dialog was on the screen: the dialog hidden, removed at the next frame\")", Include = "tfexgen_ffi.hpp")]
        static extern void FailedEarly();

        // MatchmakingService's `?version={autoUpdater.GetCurrentVersion()}`: TF.EX's version (the server knows TF.EX's versions)
        [Cpp("tfex::gen::svc::TfexVersion()", Include = "tfexgen_services.hpp")]
        public static extern Version ServerVersion(TF.EX.Common.IAutoUpdater updater);

        // OPTIONS' TF.NX section (glue SwitchMods.InitModOptions): TF.EX's AUTO UPDATE, made by TF.EX's NetplaySettings.Create (its
        // CreateOnOff noted here), moved from TF.EX's list to TF.NX's with a description for what it downloads on the Switch
        static OptionsButton autoUpdate, moved;
        public static void BeginOptions() { autoUpdate = null; moved = null; }
        public static void NoteOnOff(string name, OptionsButton button) { if (name == "AUTO UPDATE") autoUpdate = button; }
        public static OptionsButton TakeAutoUpdate()
        {
            moved = autoUpdate;
            if (moved != null) DynamicData.For(moved).Set("fortrise_description",
                "DOWNLOAD NEW TF.NX VERSIONS FROM GITHUB AUTOMATICALLY. \n THEY BRING TF.EX'S UPDATES: RESTART THE GAME TO FINISH");
            return moved;
        }
        public static bool IsMoved(OptionsButton button) => button == moved;

        // TF.NX's OPTIONS entry (the TF.NX section's first): "TF.NX UPDATE" and what pressing it does now (DOWNLOAD, RETRY...), the
        // version and the rest in its description; Confirm downloads a newer release (TF.EX's dialog), else checks again. A TF.NX addition:
        // TF.EX has no manual download (the user, 2026-11-07; the wording 2026-11-08: a version then "RESTART" said nothing).
        public static void CreateEntry(List<OptionsButton> buttons)
        {
            var button = new OptionsButton("TF.NX UPDATE");
            entry = button;
            shownUi = -1;
            button.SetCallbacks(() => { button.State = StateText(UiState()); }, null, null, () =>
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
            Describe(button, UiState());
        }

        // short: the game centres a state 30 px right of the name, so about 10 characters fit (the description says the rest)
        static string StateText(int ui)
        {
            switch (ui)
            {
                case 0: return "DEV BUILD";
                case 1: return "CHECKING";
                case 2: return "UP TO DATE";
                case 3: return "DOWNLOAD";
                case 4: return "LOADING";
                case 5: return "RESTART";
                default: return "RETRY";
            }
        }

        // the description box under the list (FortRise's OptionDescriptionBox re-wraps it when it changes): the versions, and what to do
        static void Describe(OptionsButton button, int ui)
        {
            string you = CurrentText();
            string text;
            switch (ui)
            {
                case 0: text = "YOU HAVE TF.NX " + you + ". THIS CONSOLE GETS DEVELOPMENT BUILDS \n (XPLAY/DEVSERVER.TXT), NOT GITHUB'S RELEASES"; break;
                case 1: text = "YOU HAVE TF.NX " + you + ". \n LOOKING FOR A NEWER VERSION ON GITHUB..."; break;
                case 2: text = "YOU HAVE THE LATEST TF.NX (" + you + "). \n PRESS TO CHECK GITHUB AGAIN"; break;
                case 3: text = "TF.NX " + LatestText() + " IS OUT (YOU HAVE " + you + "). \n PRESS TO DOWNLOAD IT"; break;
                case 4: text = "DOWNLOADING TF.NX " + LatestText() + "..."; break;
                case 5: text = "TF.NX " + LatestText() + " IS DOWNLOADED. \n RESTART THE GAME TO START USING IT"; break;
                default:
                    string why = Failure();
                    text = "THE TF.NX UPDATE FAILED" + (why == null ? "" : ": " + why.ToUpperInvariant()) + ". \n PRESS TO TRY AGAIN";
                    break;
            }
            DynamicData.For(button).Set("fortrise_description", text);
        }

        // each frame on the main menu (tfnxupdate.cpp): the entry's state and description, live (an OptionsButton redraws its State only
        // after a press)
        static int shownUi = -1;
        public static void Refresh()
        {
            if (removeWhenAdded != null && removeWhenAdded.Scene != null) { removeWhenAdded.RemoveSelf(); removeWhenAdded = null; }
            if (entry == null) return;
            int ui = UiState();
            if (ui == shownUi) return;   // (no string made each frame)
            shownUi = ui;
            entry.State = StateText(ui);
            Describe(entry, ui);
        }
    }
}
