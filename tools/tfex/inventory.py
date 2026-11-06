#!/usr/bin/env python3
"""inventory.py: writes docs/HAND-CODE-INVENTORY.md from the code (evaluation rec. 12), so it never goes stale.

Every function / hook in hw-probe/exlaunch/source/program/tfex/*.cpp, with its lines and a group:
  D  no caller left anywhere in the module (program/**, generated code included): delete;
  L  a TF.EX look-alike (the rule says which TF.EX code replaces it and what blocks that);
  S  a real Switch difference or glue (keep);
  T  a test helper (keep).
The group comes from RULES below (file, name regex) first, then from the function's own markers (a TFEX-DIFF / S-<key> comment: S;
hw.txt / test driver: T); anything left is '?', listed for a rule to be added. Owners per AGENTS.md. The hand-written part of the
doc (from MANUAL_MARK on) is kept as it is.
Usage: tools/tfex/inventory.py [--check]   (--check: exit 1 when the doc would change, e.g. in CI or after a merge)"""
import collections, glob, os, re, subprocess, sys
from pathlib import Path

repo = Path(__file__).resolve().parents[2]
prog = repo / "hw-probe/exlaunch/source/program"
doc = repo / "docs/HAND-CODE-INVENTORY.md"
MANUAL_MARK = "## TF.EX features on the Switch"

OWN = {"MainMenuPatch": "A", "MatchmakingService": "A", "Json": "A", "Notification": "A", "NetplayScreens": "translator", "Brute": "translator",
       "NetplayManager": "B", "EndOfMatch": "B", "MapScenePatch": "B", "MatchPause": "B", "InputLock": "B", "PlayerPatch": "B", "BetweenTicks": "B"}

# (file stem, name regex, group, TF.EX code / reason, generated, blocker): first match wins
RULES = [
    ("BetweenTicks", r".*", "S", "rollback glue: requests carried out between ticks (Scene.Update in resimulated ticks)", "", ""),
    ("Brute", r".*", "S", "runtime: BRUTE delegates for hand code", "", ""),
    ("Json", r".*", "S", "the native JSON reader / writer (NetplayPreferences file, telemetry)", "", ""),
    ("EndOfMatch", r"^(SetAutoVote|TestPress|TestVoteFrame|MatchEndChoicesRender|LightPauseMenuRender|TestButtons)$", "T", "the end-of-match test driver (hw.txt lobbyvote)", "", ""),
    ("EndOfMatch", r"^ReturnToMenu$", "L", "MatchmakingService.ReturnToMenu (TF.EX)", "generated in MatchmakingService", "the Switch teardown it adds is QuitTeardownFrame's; then use the generated one"),
    ("EndOfMatch", r"^DropMatchSettings$", "S", "TFEX-DIFF: PauseMenu_AddItem's rule on Update (the Switch's ctor inlines AddItem)", "", ""),
    ("EndOfMatch", r"^(AssignInputsHook|MenuUpdateInputsHook)$", "S", "TFEX-DIFF S-input: HUDPatch's input rebuild (xplay owns the controller slots)", "", ""),
    ("EndOfMatch", r"^Pause\w+Hook$", "S", "glue: one hook per PauseMenu address calling TF.EX's generated PauseMenuPatch", "", ""),
    ("EndOfMatch", r"^GenRemoteLeft$", "S", "TFEX-DIFF (parked by the user): every remote gone -> EndMatch", "", ""),
    ("EndOfMatch", r"^(AwaitingVote|EndgameFrame|EnsureMatchClasses|Frame|GenAdvanceError|GenEnd|GenNetplayManager|GenStart|SyncInputDelay|GenSyncAbort|InEndgame|Install|Log|MakeGenNetplayManager|MatchStarted|Matches|Nm|NmAny|OnLobbyResultsFinished|QuitTeardownFrame|RematchMapScene|SetMatchStarted)$", "S", "glue: xplay's end-of-match state / the generated NetplayManager (made on a menu, called from xplay's session events)", "", ""),
    ("InputLock", r"^(DisableAllControllers|EnableAllControllers|IsInputLocked|DisableAllControllerExceptLocal|Emptied)$", "L", "InputService.DisableAllControllers / EnableAllControllers / IsInputLocked / DisableAllControllerExceptLocal", "InputService generated; these not adopted", "their PC bodies empty the input lists (TFEX-DIFF S-input: the gate instead)"),
    ("InputLock", r"^(Base|GateConfirm|InLobby|Install|InstallLate|MenuPressed)$", "S", "S-input: the Switch's JoyCon controllers under TF.EX's generated XGamePadInputPatch", "", ""),
    ("MainMenuPatch", r"^(SetJoinMode|SetLobbyArcher|SetEditUi|SetQuickUi|SetQuickWait|SetPrivateUi|SetAutoUi|AutoUiButtons|Press|AutoScreens|AutoUi|SetLobby\w+|Count)$", "T", "the auto-UI test driver / hw.txt test settings", "", ""),
    ("MainMenuPatch", r"^(SetWide|IsWide|ModeButtonAction|StandardModeActionHook|WideModeActionHook)$", "S", "S-wide: the Switch's own STANDARD / WIDE screen (wide mode is native)", "", ""),
    ("MainMenuPatch", r"^ShowSeatLabel$", "L", "TF.EX's open-seat orb and creating Loader (RollCallElementPatch)", "", "TFEX-DIFF S-input: the orb sits on the PC's controlIcon, which the Switch lacks"),
    ("MainMenuPatch", r"^(NotJoinedUpdateHook|JoinedUpdateHook)$", "L", "RollCallElementPatch NotJoinedUpdate / JoinedUpdate (generated, called from these hooks)", "generated (g_genRollcall)", "the hand Body still decides foreign seats when TF.EX's prefix lets the original run"),
    ("MainMenuPatch", r"^StartVersusHook$", "S", "S-start: the native lobby match start replaces TF.EX's StartVersus prefix / postfix", "", ""),
    ("MainMenuPatch", r"^(ApplyNetplayMode|Base|ClearNetplayMode|CreateMainHook|CurrentScene|InLobbyScreen|Install|InstallLate|Is|IsNetplay|LiveString|Log|MakeString|OpenSeatLabels|Own|OwnEmpty|OwnPlayer|PlayerAtSeat|SceneIs|SetSwitchWideLayout|TypeName|Update|WarnUnportedVariants)$", "S", "glue / helpers: the generated lobby, the netplay mode (pcgameplay), hooks calling TF.EX's generated patches, logs", "", ""),
    ("MapScenePatch", r"^(TowerButton|RandomButton|SelectLobbyButton|InitVersusButtonsHook|GetRandomVersusTowerHook)$", "L", "MapScenePatch Begin_Postfix / InitVersusButtons / GetRandomVersusTower (generated, used in standard lobbies)", "generated (UseGen)", "the hand branch is WiderSet-wide lobbies' (S-wide)"),
    ("MapScenePatch", r"^(Active|BeginHook|ButtonCount|Enter|FinishTransitionHook|GotoVersusLevelSelect|InMap|Install|MapUpdateHook|Reset|StartSessionHook|StartedWithGen|UseGen)$", "S", "S-start / glue: the native lobby start's way into the map screen", "", ""),
    ("MatchPause", r"^(SetTest|Frame|TestButtons|tfex_MatchPauseTestButtons|tfex_MatchPauseSetTest|tfex_MatchPauseFrame)$", "T", "the pause test driver (hw.txt lobbypause)", "", ""),
    ("MatchPause", r"^(ForceClose|HandlePausing|IsOpen|Pressed|tfex_MatchPauseForceClose|tfex_MatchPauseHandlePausing|tfex_MatchPauseIsOpen)$", "S", "TFEX-DIFF S-rollback / S-input: LightPauseMenu.Pressed recorded per simulated frame; glue to the generated pause", "", ""),
    ("MatchmakingService", r"^(BackToNetplaySelect|CbRun|Delegate|Enqueue|Gen|GenDispatch|InUse|LoadSettingsFile|MakeGen|MatchmakingService::CanHostStart|MatchmakingService::ClearOwnLobby|MatchmakingService::CloseTransport|MatchmakingService::ConnectToServerAndListen|MatchmakingService::DisconnectFromServer|MatchmakingService::DrainGameThreadActions|MatchmakingService::GenObject|MatchmakingService::GetLocalSeat|MatchmakingService::GetRelayUrl|MatchmakingService::GetRelayedPeers|MatchmakingService::GetRoomPeerId|MatchmakingService::Instance|MatchmakingService::IsConnectedToServer|MatchmakingService::IsLobbyReady|MatchmakingService::LeaveLobby|MatchmakingService::ResetPeer|MatchmakingService::RunOnGameThread|MatchmakingService::SendJson|MatchmakingService::SetRoomPeerId|MatchmakingService::ShowMatchStartWarnings|MatchmakingService::SwitchPingMeasurementUrl|NetThread|NetplayPreferences::Load|NetplaySettingsFile::Apply|NetplaySettingsFile::Save|ParseNetplayServer|ParseServer|PathOf|Post|Queue|ReadSd|SendIdentity|StartsWithTag|SwitchPingMeasurement)$", "S", "the native transport, settings file and forwarders under TF.EX's generated MatchmakingService", "", ""),
    ("NetplayManager", r"^(SortedBySeat|SortBySeat|ResolveArchers)$", "L", "ArcherService.CompactSeatsToHandles / ApplyToGame", "generated (ArcherService)", "the native start's switch-over"),
    ("NetplayManager", r"^(StrEq|Variants|IsString|Find|FirstUnported|ApplyVariants|ApplyNetplayVariantRules)$", "L", "MatchVariantsExtensions.ApplyNetplayVariantRules, variants", "generated (VersusModeExtensions)", "the variants switch-over"),
    ("NetplayManager", r"^SelfTest$", "T", "a boot self-test against TF.EX's tower selection", "", ""),
    ("NetplayManager", r"^(ApplyRelay|ButtonIsTower|ClearNoRandom|GoalFor|InitVersusButtonsUnlocked|LastWasRandom|LevelSystem|NetplayManagerStartLobbyMatch|Select|SelectWide|Title|Towers|Unlocked|xplay::LobbyLevelSystem)$", "S", "S-wide towers / the game's own systems / the native GGRS start and relay", "", ""),
    ("NetplayScreens", r"^(ReadJoinCode|KeyboardCode|CodeEntryConfirm|LobbyCodeFrame|Str|Dyn|AddTextEntity|RemoveEntity)$", "S", "TFEX-DIFF S-keyboard: the software keyboard, the private code shown", "", ""),
    ("NetplayScreens", r"^(ApplyBuilderPresets|SetBuilderPreset|SetTestJoinCode|BrowserRows|IsName)$", "T", "test hooks (hw.txt lobbymode / lobbymap / lobbylength / lobbymax, joincode)", "", ""),
    ("NetplayScreens", r"^(CallStateFuncHook|Frame|Install|IsCustom)$", "S", "routing into TF.EX's generated screens (CallStateFunc hook, Frame)", "", ""),
    ("Notification", r"^(TestFrame|SetTest)$", "T", "the notification test driver", "", ""),
    ("Notification", r"^(Args|Clear|ClearDeferred|ClearDeferredThunk|ClearThunk|Create|CreateArgs|CreateOrDefer|CreateOrDeferThunk|CreateThunk|Enabled|EnsureClass|FlushDeferred|FlushThunk|Managed|RuntimeType|SetEnabled|SharedCanvas)$", "S", "glue: bootstraps TF.EX's generated Notification class; TFEX-DIFF S-rollback shared canvas", "", ""),
    ("PlayerPatch", r".*", "S", "glue: the Player.Update hook calling TF.EX's generated postfix", "", ""),
]
SKIP_DEAD = {"Install", "InstallLate", "Callback", "Log", "Body", "Base", "Frame", "Reset", "Update", "Is", "Str", "Find", "Select", "Title",
             "Count", "Enable", "Run", "Gen", "M", "Press"}

def functions(path):
    pat = re.compile(r'^(?:static\s+|inline\s+)?(?:HOOK_DEFINE_TRAMPOLINE\((\w+)\)|(?:[\w:<>\*&,]+\s+)+\**&?(\w+(?:::\w+)*)\s*\([^;{]*\)\s*(?:const\s*)?\{)')
    L = path.read_text(errors="replace").split("\n")
    st = []
    for i, l in enumerate(L):
        if l.startswith((" ", "\t")): continue
        m = pat.match(l.strip())
        if m and not re.match(r'^(if|for|while|switch|return|else)\b', l.strip()): st.append((i, m.group(1) or m.group(2)))
    out = []
    for k, (i, n) in enumerate(st):
        end = st[k + 1][0] if k + 1 < len(st) else len(L)
        body = "\n".join(L[max(0, i - 3):end])
        out.append((n, i + 1, end - i, body))
    return out, len(L)

def main():
    files = sorted(glob.glob(str(prog / "tfex/*.cpp")))
    allsrc = {f: open(f, errors="replace").read() for f in glob.glob(str(prog / "**/*.[ch]pp"), recursive=True)}
    rows, totals = [], {}
    for f in files:
        p = Path(f); stem = p.stem
        fns, total = functions(p); totals[p.name] = total
        for name, line, span, body in fns:
            short = name.split("::")[-1]
            g = what = gen = blk = None
            if short not in SKIP_DEAD and "Hook" not in short:
                pat = re.compile(r'\b' + re.escape(short) + r'\b')
                if sum(len(pat.findall(t)) for t in allsrc.values()) <= 1 or \
                   (sum(len(pat.findall(t)) for k, t in allsrc.items() if not k.endswith(".hpp")) <= 1 and sum(len(pat.findall(t)) for t in allsrc.values()) <= 2):
                    g, what, gen, blk = "D", "no caller left in the module (declaration only)", "", "delete"
            if g is None:
                for rf, rx, rg, rw, rgen, rblk in RULES:
                    if rf == stem and re.search(rx, name): g, what, gen, blk = rg, rw, rgen, rblk; break
            if g is None:
                if re.search(r"TFEX-DIFF|\bS-[a-z0-9]+\b", body): g, what, gen, blk = "S", "marked TFEX-DIFF / S-<key> in its comments", "", ""
                elif re.search(r"hw\.txt|\btest\b", body, re.I): g, what, gen, blk = "T", "a test helper (hw.txt / test in its comments)", "", ""
                else: g, what, gen, blk = "?", "no rule or marker: add one to tools/tfex/inventory.py RULES", "", ""
            rows.append((p.name, name, line, span, g, OWN.get(stem, "?"), what, gen, blk))
    head = subprocess.run(["git", "-C", str(repo), "rev-parse", "--short", "HEAD"], capture_output=True, text=True).stdout.strip()
    G = ["L", "S", "T", "D", "?"]
    summ = collections.defaultdict(collections.Counter)
    for r in rows: summ[r[0]][r[4]] += r[3]
    md = ["# Hand-written TF.EX-side code: inventory\n",
          f"GENERATED by `tools/tfex/inventory.py` (at {head}); do not edit above \"{MANUAL_MARK}\". It covers every function or hook in "
          "`hw-probe/exlaunch/source/program/tfex/*.cpp`. A row spans from its definition to the next one, so file-level comments and globals "
          "fall into the row above or into the file's 'other' lines.\n",
          "Groups:",
          "- **L**: a TF.EX look-alike (the TF.EX code that replaces it, and what blocks that).",
          "- **S**: a real Switch difference or glue (keep).",
          "- **T**: a test helper (keep).",
          "- **D**: no caller left in the module (delete).",
          "- **?**: no rule or marker yet.\n",
          "The group comes from the script's rules, then from the function's markers (TFEX-DIFF / S-<key>, hw.txt / test). Owners follow AGENTS.md.\n",
          "## Summary: lines per group per file\n",
          "| file | total | L | S | T | D | ? | other | owner |", "|---|---|---|---|---|---|---|---|---|"]
    tot = collections.Counter()
    for f in sorted(totals, key=lambda x: -totals[x]):
        c = summ[f]; other = totals[f] - sum(c.values())
        for g in G: tot[g] += c[g]
        tot["other"] += other; tot["total"] += totals[f]
        md.append(f"| {f} | {totals[f]} | {c['L']} | {c['S']} | {c['T']} | {c['D']} | {c['?']} | {other} | {OWN.get(f[:-4], '?')} |")
    md.append(f"| **all** | **{tot['total']}** | **{tot['L']}** | **{tot['S']}** | **{tot['T']}** | **{tot['D']}** | **{tot['?']}** | **{tot['other']}** | |\n")
    md.append("History: 4715 lines (L 2402) in the first inventory (2026-10-18 morning), 4159 (L 2152) at d300d7a, 2873 (L 329) at 3e21ebc.\n")
    for title, grp in (("L rows left", "L"), ("D rows (no caller: delete, build only)", "D"), ("Rows with no rule (add one to inventory.py)", "?")):
        rs = [r for r in rows if r[4] == grp]
        md.append(f"## {title} ({len(rs)} rows, {sum(r[3] for r in rs)} lines)\n")
        if rs:
            md.append("| file | function / hook | lines | owner | TF.EX code / reason | generated | blocker |\n|---|---|---|---|---|---|---|")
            for r in sorted(rs, key=lambda r: (r[5], r[0], r[2])): md.append(f"| {r[0]} | {r[1]} | {r[3]} | {r[5]} | {r[6]} | {r[7]} | {r[8]} |")
        md.append("")
    for f in sorted(totals, key=lambda x: -totals[x]):
        md.append(f"## {f} ({totals[f]} lines, owner {OWN.get(f[:-4], '?')})\n\n| function / hook | line | lines | group | TF.EX code / reason | generated | blocker |\n|---|---|---|---|---|---|---|")
        for r in rows:
            if r[0] == f: md.append(f"| {r[1]} | {r[2]} | {r[3]} | {r[4]} | {r[6]} | {r[7]} | {r[8]} |")
        md.append("")
    old = doc.read_text() if doc.exists() else ""
    i = old.find("\n" + MANUAL_MARK)   # at a line start (the header quotes the mark too)
    manual = old[i + 1:] if i >= 0 else ""
    text = "\n".join(md) + ("\n" + manual if manual else "")
    # the commit line changes on every commit: compare without it
    norm = lambda t: re.sub(r"\(at [0-9a-f]+\)", "", t)
    if "--check" in sys.argv:
        if norm(text) != norm(old): print("inventory: docs/HAND-CODE-INVENTORY.md is stale: run tools/tfex/inventory.py", file=sys.stderr); return 1
        print("inventory: up to date"); return 0
    if norm(text) != norm(old): doc.write_text(text)
    print(f"inventory: {tot['total']} lines, L {tot['L']}, S {tot['S']}, T {tot['T']}, D {tot['D']}, ? {tot['?']} -> {doc.relative_to(repo)}")
    return 0

sys.exit(main())
