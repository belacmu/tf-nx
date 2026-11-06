import os, sys
# import metaresolve from this script's own dir (robust when run from elsewhere).
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import metaresolve as m   # NOTE: metaresolve opens "main.flat" from the CURRENT dir —
                          # run this from a dir containing main.flat (see tools/re/README.md).

targets = [
    # --- validation (known from TowerBall) ---
    ("TowerFall.VersusModeButton","Update"),      # expect ~0xbb1e68
    ("TowerFall.Level","GetXMLPositions"),        # expect 0xa7e8c0
    # --- the cap / join gate (raise to 8) ---
    ("TowerFall.RollcallElement","get_MaxPlayers"),
    ("TowerFall.RollcallElement","get_MinPlayers"),
    ("TowerFall.RollcallElement","GetPosition"),      # static 2-row layout
    ("TowerFall.RollcallElement","get_CanChangeSelection"),
    # --- rollcall build (creates N elements; the <N loops) ---
    ("TowerFall.MainMenu","CreateRollcall"),
    # --- team + spawn (reused by 4v4) ---
    ("TowerFall.MatchSettings","GetMaxTeamSize"),
    ("TowerFall.RoundLogic","SpawnPlayersTeams"),
    ("TowerFall.RoundLogic","SpawnPlayersFFA"),
    # --- player count + tower/level set ---
    ("TowerFall.TFGame","get_PlayerAmount"),
    ("TowerFall.GameData","Load"),
    ("TowerFall.VersusTowerData","GetLevelSet"),
]
print(f"{'method':44} {'kind':8} {'addr/slot':12} {'callers'}")
for typ, meth in targets:
    status, ttok, res = m.type_resolve(typ, meth)
    if status != "OK":
        print(f"{typ+'.'+meth:44} {status}"); continue
    if not res:
        print(f"{typ+'.'+meth:44} no-store"); continue
    for ts, ms, wrapper, kind, val, callers in res:
        vstr = (f"0x{val:x}" if kind=="DIRECT" else f"slot 0x{val:x}") if val is not None else "None"
        print(f"{typ+'.'+meth:44} {kind:8} {vstr:12} {callers}")
