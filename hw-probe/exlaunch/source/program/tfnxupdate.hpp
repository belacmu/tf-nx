/* TF.NX's updater from its GitHub releases (tfnxupdate.cpp; docs/PLAN-UPDATER-2026-11-07.md): what TF.EX's IAutoUpdater does on the PC,
 * for the Switch's install (TFEX-DIFF S-update: the release zip unpacked onto the SD card, no FortRise ModUpdater, the game restarted by the
 * player). TF.EX's flow (TFGamePatch: the NETPLAY check, StartUpdate, UpdaterDialog) is translated and calls the IAutoUpdater part. */
#pragma once
#include <cstdint>

namespace tfnxupdate {
/* at boot, after the SD card is mounted and devupdate::Start: finishes an install a power cut interrupted, notes a version just installed,
 * and starts the check thread (off when sd:/xplay/devserver.txt is there: a developer's console keeps its dev builds) */
void Start();
void SetSource(const char* base);   /* hw.txt "updatesource <url>" (TEST ONLY): another releases URL in place of GitHub's (the test server) */
void SetTest();                     /* hw.txt "updatetest" (TEST ONLY): tools/update-test.sh walks the menus */
unsigned long TestButtons();        /* its presses (controller 1's npad buttons; 0 without updatetest) */
void Frame();                       /* each frame on the game thread: the "updated to" notice, the restart notice, the auto update's start */

/* TF.EX's IAutoUpdater (overrides.toml S-update) */
void CheckForUpdate();              /* blocking (TF.EX's worker): the latest release's version */
bool IsStatusFresh();
void Invalidate();
int32_t TfexStatus();               /* UpdateStatus for TF.EX's online gate: 0 Unknown, 1 UpToDate, 2 UpdateAvailable (a newer TF.EX only) */
int64_t Latest();                   /* the latest release's version (System.Version packed, gen::VersionString) */
int64_t Current();                  /* this build's TF.NX version */
void* FailureReason();              /* a managed string, or null */
bool DownloadAndApply(void* onPhase, void* onProgress);   /* TF.EX's worker: the result of the download StartDownload made, else blocking */
/* the download on the updater's thread (the Switch's managed Thread runs on the caller: glue Updates.Run / Pump) */
void StartDownload();
int DownloadState();                /* 0 none, 1 asked, 2 running, 3 done */
int PhaseSeq();                     /* + 1 at each new phase text */
const char* PhaseText();
int64_t DownloadedBytes();
int64_t TotalBytes();
void CheckInBackground();           /* a check on the updater's thread (TF.EX's NETPLAY check, glue Updates.Run) */
void SkipNextCheck();               /* that check is the one CheckForUpdate would make now */
void RestartNeeded();               /* TF.EX's HandleAutoUpdate when the update is in: the dialog's last phase, then the restart notice */

/* TF.NX's OPTIONS entry (glue Updates): its state and what Confirm does */
enum class Ui { Off, Checking, UpToDate, Available, Downloading, Downloaded, Failed };
Ui UiState();
const char* LatestText();           /* "0.29.0.1" */
const char* CurrentText();
bool Busy();                        /* a check or a download runs */
void CheckNow();                    /* Confirm with nothing to download: a check on the worker thread */
bool WantsAutoStart();              /* AUTO UPDATE is on and a newer TF.NX was found at boot: the dialog starts on the main menu (once) */
void AutoStarted();
}
