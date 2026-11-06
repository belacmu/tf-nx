// The TF.State plan's spawn scene (docs/tfstate-rollback-measurement.md, step A), TEST ONLY: hw.txt "statespawn <tick>" makes the game
// create, at that tick of a lobby match, the gameplay entities TF.State handles, through the game's own factories, so one run's layout
// dump sees every class. OUR OWN CODE; never in a real match.
using Microsoft.Xna.Framework;
using TowerFall;

namespace TfexGlue
{
    public static class StateSpawn
    {
        // pilot: one arrow of each type, owned by the first player, falling from the top of the screen
        public static int Arrows(Level level)
        {
            Player owner = null;
            foreach (var e in level[Monocle.GameTags.Player]) { owner = e as Player; if (owner != null) break; }
            if (owner == null) return 0;
            int n = 0;
            foreach (ArrowTypes t in System.Enum.GetValues(typeof(ArrowTypes)))
            {
                var arrow = Arrow.Create(t, owner, new Vector2(40f + 16f * n, 30f), 1.5707964f);
                level.Add(arrow);
                n++;
            }
            return n;
        }
    
        static Player FirstPlayer(Level level)
        {
            foreach (var e in level[Monocle.GameTags.Player]) { var p = e as Player; if (p != null) return p; }
            return null;
        }

        // the rest of the gameplay entities: a pickup of each type, a chest, a free orb, a hat, lava, miasma, an explosion and a super
        // explosion on the first player (a corpse, and a ghost with RETURN AS GHOSTS)
        public static int Entities(Level level)
        {
            var owner = FirstPlayer(level);
            int n = 0;
            foreach (Pickups k in System.Enum.GetValues(typeof(Pickups)))
            {
                if (k == Pickups.Gem) continue;   // (Pickup.CreatePickup has no case for it: a Dark World gem)
                var at = new Vector2(30f + 12f * (n % 22), 50f + 20f * (n / 22));
                level.Add(Pickup.CreatePickup(at, at, k, 0));
                n++;
            }
            level.Add(new TreasureChest(new Vector2(160f, 60f), (TreasureChest.Types)0, (TreasureChest.AppearModes)0, Pickups.Arrows, 0)); n++;
            level.Add(new Orb(new Vector2(200f, 60f), false)); n++;
            foreach (Player.HatStates hs in System.Enum.GetValues(typeof(Player.HatStates)))   // a hat of each kind (normal, crown, steel...)
            {
                var hat = Hat.CreateHat(ArcherData.Archers[0], Allegiance.Neutral, hs, new Vector2(220f + 10f * n % 60, 60f), null, false, 0);
                if (hat != null) { level.Add(hat); n++; }
            }
            level.Add(new LavaControl((LavaControl.LavaMode)0, 0)); n++;
            level.Add(new Miasma((Miasma.Modes)0)); n++;
            if (owner != null)
            {
                Explosion.Spawn(level, owner.Position, 1, false, false, false); n++;
                Explosion.SpawnSuper(level, owner.Position + new Vector2(30f, 0f), 1, false); n++;
            }
            return n;
        }

        // later: a team reviver on each corpse
        public static int Revivers(Level level)
        {
            // a corpse of our own (no player may have died yet): the last player dies, a team reviver on its corpse
            Player last = null;
            foreach (var e in level[Monocle.GameTags.Player]) { var p = e as Player; if (p != null) last = p; }
            if (last == null) return 0;
            PlayerCorpse c = last.Die((DeathCause)0, -1, false, false);
            if (c == null) return 0;
            level.Add(new TeamReviver(c, (TeamReviver.Modes)1));
            return 1;
        }
    }
}
