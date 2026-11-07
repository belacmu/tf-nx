# TF.NX updates from GitHub releases (2026-11-07: built; the plan, then what was done)

The user's ask: TF.NX updates itself from its GitHub releases (belacmu/tf-nx), shown the way TF.EX shows its updates; an AUTO UPDATE
setting that downloads a new release so a restart of the game installs it; with it off, a DOWNLOAD UPDATE entry. Decided: the game is
restarted by the player (a notice says so); devserver.txt stays a developer override (GitHub updates off while it is there).

## Today
- TF.NX's only updater is `devupdate.cpp`: with `sd:/xplay/devserver.txt` it polls our Mac's `tools/devserver.py` (plain HTTP, a
  manifest of SD files with CRC-32), swaps changed files in, and the main menu's bottom line says "restart". Release players never have it.
- TF.EX's updater is off on the Switch: `AutoUpdater.cs` and `UpdaterDialog.cs` untranslated, `IAutoUpdater.*` overridden to
  "up to date" (overrides.toml S-update). Its AUTO UPDATE switch is still on the OPTIONS screen (NetplaySettings.Create) and does nothing.

## TF.EX's updater (v0.29.0)
- `AutoUpdater.CheckForUpdate`: HEAD `github.com/FCornaire/TF.EX/releases/latest` without following the redirect; the version from the
  Location (`/tag/vX.Y.Z`); newer than its own = UpdateAvailable. A check stays fresh 5 minutes.
- `DownloadAndApply`: GET `releases/download/vX/DShad.TF.EX-vX.zip` (redirects to GitHub's asset host), with progress; `Validate` reads
  the zip's meta.json (version, FortRise needed); `StageVersion` hands the zip to FortRise's ModUpdater (applied at the next start).
- The flow (`TFGame.cs`): checked when entering NETPLAY on the official server ("CHECKING VERSION", controllers held) and when the server
  says an update is required. AUTO UPDATE on: `StartUpdate` puts `UpdaterDialog` over the main menu ("NEW VERSION VX", the phase,
  a progress bar; controllers held), then "RESTARTING" and FortRise restarts the game. Off: NETPLAY is refused with "Online play
  requires version X". A failure: a refusal / notification with the reason.

## Plan: TF.EX's updater, translated, with the Switch underneath (TFEX-DIFF S-update, each with its reason)
1. **Translate `AutoUpdater` and the flow** (HandleAutoUpdate, StartUpdate, RequestNetplayEntry's check, `UpdaterDialog`): the overrides
   that stub `IAutoUpdater` go.
2. **Where it looks**: TF.NX's releases (`belacmu/tf-nx`), its 4-part version (`v0.29.0.1`: TF.EX's + TF.NX's build) against the
   module's TFNX_VERSION. The release asset gets a name made from the version alone (`TF.NX-v<version>.zip`, tools/release.sh; today it
   carries the commit, which the console can't know), so TF.EX's URL rule works unchanged.
3. **HTTPS**: `HttpClient` GET / HEAD through netws with TLS, the redirect to `release-assets.githubusercontent.com` followed, the body
   streamed to the SD (the existing HttpGet is plain HTTP, whole-body; the S-native HttpClient map has POST only). One TLS slot while it runs.
4. **Validate**: the release zip carries a `meta.json` with TF.NX's version (release.sh), read with the zip reader (the BCL map has
   ZipArchive for writing; reading entries + `tfx_inflate`, the Rust inflate already in ggrs-switch, is added).
5. **Apply** (instead of FortRise's ModUpdater, which the Switch doesn't have): each zip entry unpacked to `<path>.dl`, checked (size,
   CRC-32), then all swapped in (devupdate's way: the module and the mods are read at launch, so the running game is unaffected).
   Only the release's own paths (`atmosphere/contents/0100A1C00359C000/exefs/`, `xplay/tfex/`, `xplay/mods/<shipped mod>/`).
6. **Restart**: the dialog's last phase is "RESTART THE GAME TO FINISH" instead of "RESTARTING"; the controllers come back and a
   notice stays on the main menu (the console can't relaunch TowerFall for us; the user chose a notice).
7. **When it checks**: TF.EX's two moments, plus once at the main menu after boot (TF.NX's updates also bring local-play features, not
   only online versions). With AUTO UPDATE on, a found update starts TF.EX's dialog there (main menu only, never in a match; 3.4 MB).
8. **Online refusal**: TF.EX refuses NETPLAY when outdated and AUTO UPDATE is off. TF.NX refuses only when the newer release carries a
   newer TF.EX (the official server refuses old TF.EX versions itself); a TF.NX-only build bump never blocks online play.
9. **AUTO UPDATE off**: a DOWNLOAD UPDATE entry under the same settings, showing "VX AVAILABLE" when there is one; pressing it starts
   the same dialog (a TF.NX addition).
10. **devserver.txt**: when present, the GitHub check is off (`[update] dev server set: GitHub updates off`), so a dev build is never
    replaced by a release.

Logs (one line each, no repeats): the check (current, latest, status), the download (size, time), each phase, the apply (files
swapped), a failure with its reason. Checked in the emulator against a test release (a draft or a fork's release; never a real one
without the user's OK).

## Also asked for (the user, 2026-11-07: items 1-6 of the suggestions)
1. Safe install: every file unpacked and checked before any is replaced; a journal (sd:/xplay/update/install.txt) lists the swap; the
   old files kept as `<path>.old` (an update undone over FTP or the SD card); a power cut mid-swap is finished at the next start.
2. The game check: meta.json's `game` must be the TowerFall update this module is for (1.4.1.6), else refused with the reason.
3. Tested without publishing: hw.txt `updatesource <url>` + `updatetest`; tools/update-test.sh with tools/update-test/server.py.
4. "TF.NX UPDATED TO V..." on the first start of the new version, and meta.json's one-line `notes` (release.sh's RELEASE_NOTE).
5. The OPTIONS entry `TF.NX <version>`: UP TO DATE / V<new> / LOADING / RESTART / FAILED / CHECKING / DEV SERVER; Confirm downloads
   a new version, else checks again (also with AUTO UPDATE on).
6. Only the release's folders written (exefs, xplay/tfex, xplay/mods, xplay/tfnx; the zip's root files skipped); 24 MB free checked
   before the download and the unpacked size before unpacking; a failure retries at the next start (or on Confirm).

## Built (where)
- program/tfnxupdate.cpp: the Switch's IAutoUpdater (overrides.toml S-update), the boot check, the install, the notices, the test walk.
- net.cpp netws::HttpRequest: HTTPS GET / HEAD, redirects, a streamed body. ggrs-switch deflate.rs: a streaming inflate.
- translate.toml: TFGamePatch's update flow and UpdaterDialog translated; Task.Run -> glue Updates.Run (a game Thread);
  `SetPhase("RESTARTING")` -> "RESTART THE GAME TO FINISH"; `Exit()` -> the restart notice; the dialog on the menu's UI layer -1 (the
  Switch's 420 menu); MatchmakingService's ?version= stays TF.EX's.
- glue Updates.cs: the worker, the dialog's last phase and dismissal, TF.NX's OPTIONS entry.
- tools/release.sh: TF.NX-v<version>.zip, xplay/tfnx/meta.json.

## Emulator check (tools/update-test.sh, 2026-11-07)
PASS: the check (HEAD /releases/latest, its 302), the download (two redirects, 3.5 MB), the install (4 files in, the README skipped,
the bytes on the SD card the zip's), the dialog ("NEW VERSION V0.29.0.1", "RESTART THE GAME TO FINISH"), dismissed after 3 s, the top
notice on the main menu, OPTIONS' entry RESTART. Found on the way: an SD read at module start (before the SD is mounted) and a 16-byte
aligned thread stack crashed the boot; the dialog drawn on layer 0 sat left on the 420 menu.

## Not checked yet
- On a console: the TLS request to github.com and its asset host (Ryujinx's network is the Mac's), the swap of the running module's
  file on a real SD card, a power cut mid-swap.
- A real release: the first one made with the new release.sh (asset name, meta.json).
