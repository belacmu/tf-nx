// AINova's world capture on the Switch (TFEX-DIFF S-perf, OUR OWN CODE, translated). ebe1.kenobi's AINova (vendor/ai-nova, not ours) has
// each Nova AI read the whole screen every frame, WorldSnapshot.Capture: for each 10 px cell, Scene.CollideCheck with Solid, LavaCollider and
// HotCoals, and its Hazard (every PlayerCollider entity's CollideCheck). With 7 Nova AIs that is 7 identical passes a frame: the first
// seconds of an 8-player round took 4-11 ms a frame on the console in the AIs' decisions (2026-11-10 [perf] windows: the game at 74-90%
// speed), the most sampled stacks in Hazard. The answers depend only on the level in that frame, so the first AI to ask a cell computes it
// and the others read it: the same values, once a frame. Keyed by the scene and its FrameCounter (it moves every update); anything that isn't
// a cell centre of the uncamera'd grid is computed as asked. AINova's code is untouched ([[rewrite]] of those two calls in WorldSnapshot).
using System;
using Microsoft.Xna.Framework;
using Monocle;
using TowerFall;

namespace TfexGlue
{
    public static class NovaGrid
    {
        const int Cols = 64, Rows = 32, Kinds = 4;   // (the wide screen: 42 x 24 cells of 10 px)
        static Scene lastScene;
        static float lastTime = -1f;
        static sbyte[] cache = new sbyte[Kinds * Cols * Rows];   // 0 not asked this frame, 1 false, 2 true
        static int computed, reused, frames;

        [Cpp("tfex::nova::Hazard($0, $1)", Include = "../NovaGrid.hpp")] static extern bool NovaHazard(Level level, Vector2 at);
        [Cpp("tfex::gen::frmods::Log($0)", Include = "tfexgen_frmods.hpp")] static extern void Log(string line);

        static int Slot(Scene scene, Vector2 at, int kind)
        {
            if (scene != lastScene || scene.FrameCounter != lastTime)
            {
                if (lastScene != null && (++frames == 60 || frames % 3600 == 0))
                    Log("[nova] the AIs' world grid shared: " + computed + " cells computed, " + reused + " reused (" + frames + " frames)");
                Array.Clear(cache, 0, cache.Length);
                lastScene = scene;
                lastTime = scene.FrameCounter;
            }
            float fx = at.X - 5f, fy = at.Y - 5f;   // Capture's cell centres: x * 10 + 5 (the camera at the origin)
            int cx = (int)(fx / 10f), cy = (int)(fy / 10f);
            if (cx * 10f != fx || cy * 10f != fy || cx < 0 || cy < 0 || cx >= Cols || cy >= Rows) return -1;
            return (kind * Rows + cy) * Cols + cx;
        }

        static int KindOf(Monocle.GameTags tag) => tag == Monocle.GameTags.Solid ? 0 : tag == Monocle.GameTags.LavaCollider ? 1 : tag == Monocle.GameTags.HotCoals ? 2 : -1;

        // WorldSnapshot's `level.CollideCheck(at, tag)`
        public static bool Collide(Scene scene, Vector2 at, Monocle.GameTags tag)
        {
            int kind = KindOf(tag);
            int i = kind < 0 ? -1 : Slot(scene, at, kind);
            if (i < 0) return scene.CollideCheck(at, tag);
            if (cache[i] != 0) { reused++; return cache[i] == 2; }
            bool r = scene.CollideCheck(at, tag);
            cache[i] = (sbyte)(r ? 2 : 1);
            computed++;
            return r;
        }

        // WorldSnapshot's `Hazard(level, at)`: AINova's own Hazard, once a cell a frame
        public static bool Hazard(Level level, Vector2 at)
        {
            int i = Slot(level, at, 3);
            if (i < 0) return NovaHazard(level, at);
            if (cache[i] != 0) { reused++; return cache[i] == 2; }
            bool r = NovaHazard(level, at);
            cache[i] = (sbyte)(r ? 2 : 1);
            computed++;
            return r;
        }
    }
}
