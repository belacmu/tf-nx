// translate.toml [[rewrite]] glue as FortRise's compilation sees it: FortRise's code is compiled apart from TF.EX's, so the rules'
// TfexGlue.* names resolve here. Each forwards to the TF.EX glue's generated code (translator/glue/Rewrites.cs, SettingsUi.cs).
using System;
using System.Collections.Generic;
using Microsoft.Xna.Framework;
using Monocle;
using TowerFall;

namespace TfexGlue
{
    public static class Rewrites
    {
        [Cpp("tfex::gen::GlueRewrites::SceneAdd($0, $1)", Include = "tfexgen_GlueRewrites.hpp")] public static extern void SceneAdd(Scene scene, Entity entity);
        public static void SceneAddList<T>(Scene scene, List<T> entities) where T : Entity   // (generic: copied per element type)
        {
            foreach (T entity in entities) SceneAdd(scene, entity);
        }
    }

    public static class SettingsUi
    {
        // FortRise's UIInputText (both constructors): the software keyboard once added (glue SettingsUi.cs KeyboardEntity)
        [Cpp("tfex::gen::GlueSettingsUi::KeyboardText($0, $1, $2, $3, $4)", Include = "tfexgen_GlueSettingsUi.hpp")]
        public static extern Entity KeyboardText(MenuItem backItem, Action<string> finished, Vector2 position, int playerIndex, string initialValue);
        [Cpp("tfex::gen::GlueSettingsUi::KeyboardText($0, $1, $2, -1, $3)", Include = "tfexgen_GlueSettingsUi.hpp")]
        public static extern Entity KeyboardTextAny(MenuItem backItem, Action<string> finished, Vector2 position, string initialValue);
    }
}
