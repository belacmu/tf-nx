import metaresolve as m
req=[("Arrow","Drop"),("Arrow","EnterFallMode"),("ArrowCushion","AddArrow"),("PlayerCorpse","DropNextArrow"),("Player","LoseHat"),
("Crown","Update"),("SteelHat","Update"),("WoodenHat","Update"),("Chain",".ctor"),("Orb",".ctor"),("Spikeball",".ctor"),
("OrbLogic","Update"),("OrbLogic","DoSpaceOrb"),("OrbLogic","DoDarkOrb"),("OrbLogic","DoTimeOrb"),
("LavaControl",".ctor"),("LavaControl","Extend"),("LavaControl","Added"),("Lava","BubbleComplete"),
("TreasureSpawner","GetChestSpawnsForLevel"),("TreasureSpawner","GetRandomArrowType"),("TreasureSpawner",".ctor"),
("RoundLogic","SpawnPlayersFFA"),("RoundLogic","SpawnPlayersTeams"),("VersusLevelSystem","GenLevels"),
("Session","StartGame"),("LevelLoaderXML",".ctor"),("Arrow","EnforceLimit"),("MapScene","StartSession"),("MapScene","GetRandomVersusTower")]
for t,mm in req:
    try:
        r=m.type_resolve(t,mm)
    except Exception as e:
        print(f"{t}.{mm}: ERROR {e}"); continue
    if r[0]!="OK": print(f"{t}.{mm}: {r[0]}"); continue
    rows=[(hex(x[4]) if x[4] is not None else None, x[3], x[5]) for x in r[2]]
    print(f"{t}.{mm}: {rows}")
