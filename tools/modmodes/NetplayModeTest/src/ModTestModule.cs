using System;
using FortRise;
using Microsoft.Extensions.Logging;
using Microsoft.Xna.Framework;
using Monocle;
using MonoMod.ModInterop;
using TowerFall;

namespace NetplayModeTest;

[ModImportName("TF.EX.API")]
public static class TfExAPIModImports
{
    public static Func<bool>? IsPlayingOnline;
    public static Action<Mod, string, Action<RoundLogic>>? RegisterNetplayVersusMode;
    public static Func<RoundLogic, bool>? OnNetplayLevelLoadFinish;
}

[ModImportName("TF.State.API")]
public static class TfStateAPIModImports
{
    public static Action<Mod, string, Func<byte[]>, Action<byte[]>>? RegisterStateEvents;
    public static Action<Mod, string, Func<Entity, bool>, Func<Entity, byte[]>, Action<Entity, byte[]>, Func<Entity>?>? RegisterEntityStateEvents;
}

internal sealed class ModTestModule : Mod
{
    public const string ModeName = "ModTest";

    public static ModTestModule Instance { get; private set; } = null!;

    public static ISubtextureEntry Icon { get; private set; } = null!;

    public ModTestModule(IModContent content, IModuleContext context, ILogger logger) : base(content, context, logger)
    {
        Instance = this;
        Icon = context.Registry.Subtextures.RegisterTexture("gameModes/modtest", content.Root.GetRelativePath("Content/gameModes/modtest.png"));
        context.Registry.GameModes.RegisterVersusGameMode(ModeName, new ModTestGameMode());

        typeof(TfExAPIModImports).ModInterop();
        TfExAPIModImports.RegisterNetplayVersusMode?.Invoke(this, ModeName, round => ((ModTestRoundLogic)round).SpawnPlayers());

#if !NO_STATE   // (a build without it: the sync test must then find the markers' state missing)
        // the round's marker counter, and the markers: entities of the mode's own, in TF.State's rollback state
        typeof(TfStateAPIModImports).ModInterop();
        TfStateAPIModImports.RegisterStateEvents?.Invoke(this, ModeName, ModTestRoundLogic.SaveState, ModTestRoundLogic.LoadState);
        TfStateAPIModImports.RegisterEntityStateEvents?.Invoke(this, "ModTestMarker", entity => entity is ModTestMarker, entity => ((ModTestMarker)entity).SaveState(),
            (entity, bytes) => ((ModTestMarker)entity).LoadState(bytes), () => new ModTestMarker());
#endif

        logger.LogInformation("[ModTest] ready, TF.EX mod modes {api}", TfExAPIModImports.RegisterNetplayVersusMode != null ? "found" : "missing");
    }

    public static void Log(string message) => Instance.Logger.LogInformation("[ModTest] {message}", message);
}

public sealed class ModTestGameMode : IVersusGameMode
{
    public string Name => "Mod Test";
    public Color NameColor => Color.Orange;
    public ISubtextureEntry Icon => ModTestModule.Icon;
    public bool IsTeamMode => false;

    public void OnStartGame(Session session) => ModTestModule.Log($"match start, online {TfExAPIModImports.IsPlayingOnline?.Invoke()}");

    public RoundLogic OnCreateRoundLogic(Session session) => new ModTestRoundLogic(session);
}

/// <summary>Last Man Standing's rules. The field names are the ones TF.State saves for a round logic.</summary>
public sealed class ModTestRoundLogic : RoundLogic
{
    private readonly RoundEndCounter roundEndCounter;
    private bool done;
    private bool wasFinalKill;
    private readonly Counter markerCounter = new Counter();
    private int markersMade;

    public ModTestRoundLogic(Session session) : base(session, canHaveMiasma: true)
    {
        roundEndCounter = new RoundEndCounter(session);
    }

    public override void OnLevelLoadFinish()
    {
        base.OnLevelLoadFinish();

        if (TfExAPIModImports.OnNetplayLevelLoadFinish?.Invoke(this) == true)
        {
            return;
        }

        Session.CurrentLevel.Add(new VersusStart(Session));
        SpawnPlayers();
    }

    public void SpawnPlayers()
    {
        Players = SpawnPlayersFFA();
        ModTestModule.Log($"round {Session.RoundIndex}: {Players} player(s) spawned");
    }

    public override void OnRoundStart()
    {
        base.OnRoundStart();
        SpawnTreasureChestsVersus();
    }

    public override void OnUpdate()
    {
        SessionStats.TimePlayed += Engine.DeltaTicks;
        base.OnUpdate();

        // a marker every 90 frames of the round, at a place of its own: the mode's own entities, made and removed mid-round
        if (RoundStarted && !markerCounter)
        {
            markersMade++;
            Session.CurrentLevel.Add(new ModTestMarker(new Vector2(40 + markersMade * 37 % 240, 60 + markersMade * 23 % 120), new Vector2(markersMade % 2 == 0 ? 0.5f : -0.5f, 0.25f)));
            markerCounter.Set(90);
        }
        markerCounter.Update();

        if (!RoundStarted || done || !Session.CurrentLevel.Ending || !Session.CurrentLevel.CanEnd)
        {
            return;
        }

        if (!roundEndCounter.Finished)
        {
            roundEndCounter.Update();
            return;
        }

        done = true;
        if (Session.CurrentLevel.Players.Count == 1)
        {
            AddScore(Session.CurrentLevel.Player.PlayerIndex, 1);
        }
        InsertCrownEvent();
        Session.EndRound();
        ModTestModule.Log($"round {Session.RoundIndex} over");
    }

    public override void OnPlayerDeath(Player player, PlayerCorpse corpse, int playerIndex, DeathCause deathType, Vector2 position, int killerIndex)
    {
        base.OnPlayerDeath(player, corpse, playerIndex, deathType, position, killerIndex);

        if (wasFinalKill && Session.CurrentLevel.LivingPlayers == 0)
        {
            CancelFinalKill();
            return;
        }

        if (!FFACheckForAllButOneDead())
        {
            return;
        }

        var survivor = -1;
        foreach (Player item in Session.CurrentLevel[GameTags.Player])
        {
            if (!item.Dead)
            {
                survivor = item.PlayerIndex;
                break;
            }
        }

        Session.CurrentLevel.Ending = true;
        if (survivor != -1 && Session.Scores[survivor] >= Session.MatchSettings.GoalScore - 1)
        {
            wasFinalKill = true;
            FinalKill(corpse, survivor);
        }
    }


    // the round logic's own fields in the rollback state (TF.State saves only the base RoundLogic's)
    internal static byte[] SaveState()
    {
        var roundLogic = (Engine.Instance.Scene as Level)?.Session.RoundLogic as ModTestRoundLogic;
        return roundLogic == null ? [] : ModTestMarker.Text(BitConverter.SingleToInt32Bits(ModTestMarker.GetCounter(roundLogic.markerCounter)), roundLogic.markersMade);
    }

    internal static void LoadState(byte[] bytes)
    {
        if (bytes.Length == 0 || (Engine.Instance.Scene as Level)?.Session.RoundLogic is not ModTestRoundLogic roundLogic)
        {
            return;
        }
        var values = ModTestMarker.Values(bytes);
        ModTestMarker.SetCounter(roundLogic.markerCounter, BitConverter.Int32BitsToSingle(values[0]));
        roundLogic.markersMade = values[1];
    }
}
