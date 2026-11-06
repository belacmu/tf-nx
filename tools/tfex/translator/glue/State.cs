// TF.State's state path on the Switch, for the rollback measurement (docs/tfstate-rollback-measurement.md). OUR OWN CODE, where TF.State's
// code meets what the Switch already has; TF.State's GetState / LoadState / TfStateApi are TF.State's code, translated.
using TF.State.Domain.Models;
using TF.State.Domain.Models.Component;
using TF.State.Domain.Models.Entity.LevelEntity.Player;

namespace TfexGlue
{
    public static class StateGlue
    {
        // BitConverter.ToInt32(ReadOnlySpan<byte>) (translate.toml [[rewrite]]): not in the Switch's Mono; little-endian, as on the PC
        public static int ToInt32(byte[] b) => b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24);

        // TF.State's SFXExtensions.ToSoundEffectInstance (translate.toml [[replace]]), TFEX-DIFF S-native: TF.State builds the instance by
        // reflection through FNA's internal SoundEffectInstance(SoundEffect) constructor; the Switch's audio is MonoGame's (tools/re classmeta:
        // SoundEffectInstance has only its parameterless constructors, and ConstructorInfo.Invoke isn't in the AOT build), whose public
        // SoundEffect.CreateInstance (+0x46340) makes the same unpooled instance of the effect (new SoundEffectInstance, PlatformSetupInstance,
        // _effect = the effect). Volume, pitch and pan: TF.State's.
        public static Microsoft.Xna.Framework.Audio.SoundEffectInstance SfxInstance(SFX self)
        {
            Microsoft.Xna.Framework.Audio.SoundEffectInstance soundEffectInstance = self.Data.CreateInstance();
            soundEffectInstance.Volume = System.Math.Clamp(self.Volume * Monocle.Audio.MasterVolume, 0f, 1f);
            soundEffectInstance.Pitch = System.Math.Clamp(self.Pitch + (self.ObeysMasterPitch ? Monocle.Audio.MasterPitch : 0f), -1f, 1f);
            soundEffectInstance.Pan = self.Pan;
            return soundEffectInstance;
        }

        // TF.State's LevelExtensions.GetAll<T> / Get<T> / DeleteAll<T> / Delete<T> (translate.toml [[replace]], TFEX-DIFF S-performance): the same
        // results over a per-layer entity index (statert::EntGetAll ...); the generic copy passes T's runtime type as the last argument
        [Cpp("tfex::gen::statert::EntGetAll($0, $1)", Include = "tfexgen_state.hpp")] public static extern System.Collections.Generic.IEnumerable<Monocle.Entity> GetAll(TowerFall.Level level, System.Type t);
        [Cpp("tfex::gen::statert::EntGet($0, $1)", Include = "tfexgen_state.hpp")] public static extern Monocle.Entity Get(TowerFall.Level level, System.Type t);
        [Cpp("tfex::gen::statert::EntDeleteAll($0, $1)", Include = "tfexgen_state.hpp")] public static extern void DeleteAll(TowerFall.Level level, System.Type t);
        [Cpp("tfex::gen::statert::EntDelete($0, $1)", Include = "tfexgen_state.hpp")] public static extern void Delete(TowerFall.Level level, System.Type t);

        // ActionExtensions.ToNames (the Player scheduler's actions by their method names, x.Method.Name): Delegate.Method is reflection the
        // Switch's runtime doesn't have; the names from the delegates' bodies instead (statert::ActionName: the Switch's constructor lambdas
        // under the names TF.State gives the PC's, and the delegates the generated code made, by name)
        [Cpp("tfex::gen::statert::ActionName($0)", Include = "tfexgen_state.hpp")] static extern string ActionName(System.Action a);
        public static System.Collections.Generic.List<string> ActionNames(System.Collections.Generic.List<System.Action> actions)
        {
            var names = new System.Collections.Generic.List<string>();
            foreach (var a in actions) names.Add(ActionName(a));
            return names;
        }
    
        // TF.State's entity cache (ServiceCollections.AddEntityToCache / AddToCache / GetCachedEntity / GetCached: LazyCache's CachingService,
        // 5-minute expiry, a reset token), TFEX-DIFF S-native: LazyCache isn't on the Switch. A plain dictionary with the same keys (the depth
        // as its string, or the caller's key) and the expiry TF.State relies on: every entry goes when PurgeCache cancels the reset token
        // (ClearCache, overrides.toml [after]), the pickups' at each round's ResetState (PurgeCachedPickup). Not the 5-minute expiry.
        static System.Collections.Generic.Dictionary<string, object> cache = new System.Collections.Generic.Dictionary<string, object>();
        static System.Collections.Generic.HashSet<double> pickups = new System.Collections.Generic.HashSet<double>();
        public static void AddEntityToCache(double actualDepth, Monocle.Entity entity)
        {
            cache[actualDepth.ToString()] = entity;
            if (entity is TowerFall.Pickup) pickups.Add(actualDepth);
        }
        public static void PurgeCachedPickup() { foreach (var d in pickups) cache.Remove(d.ToString()); }
        public static void ClearCache() { cache.Clear(); pickups.Clear(); }
        public static void AddToCache(string key, object value, System.TimeSpan expires) { cache[key] = value; }
        public static Monocle.Entity GetCachedEntity(double actualDepth) => cache.TryGetValue(actualDepth.ToString(), out var e) ? e as Monocle.Entity : null;
        public static bool GetCached(string key, out object cached) { if (cache.TryGetValue(key, out var c)) { cached = c; return true; } cached = null; return false; }
    
    }
}
