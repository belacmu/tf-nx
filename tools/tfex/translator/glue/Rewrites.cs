// Translator hand rules written as C# (translate.toml [[rewrite]]): calls TF.EX makes that have no code on the Switch, replaced by
// what that code does. Each is listed in tfexgen_report.txt.
using System;
using System.Collections.Generic;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;
using Monocle;
using TF.EX.Domain;
using TF.EX.Domain.CustomComponent;
using TF.EX.Domain.Extensions;
using TF.EX.Domain.Ports;
using TowerFall;

namespace TfexGlue
{
    public static class Rewrites
    {
        // TF.EX v0.28.0's InputService._extraLocalInputs (the guests' inputs handed to ggrs_ffi each tick, Input[MAX_SEATS]): InputService
        // is hosted on the Switch (its object a token), so its one field lives here (a GC root)
        // TFEX-DIFF S-input: PlayerInput.Icon, the PC's small picture of a controller (26x18, "controls/xb360/icon"...: TF.EX v0.28.0's
        // LocalPlayersPanel draws each local player's beside its P label). The Switch's PlayerInput has no Icon; every PlayerInput there is
        // a JoyConInput, whose UpdateStyle (+0xa61a10) keeps the same kind of picture for its controller: MenuAtlas
        // "controls/<nspro|nsdual|nsjoycon>/player<seat>" (26x18, its style and seat), stored at +0x38 (`str x0,[x19,#0x38]`, +0xa61aec).
        // (Before 2026-10-31 this was controls/gamepad, the 198x128 controller layout: a big outline over the online menu's left side.)
        public static Subtexture ControllerIcon(PlayerInput input) => input != null ? JoyConStyleIcon(input) : null;
        [Cpp("gen::Fld<void*>($0, 0x38)")] static extern Subtexture JoyConStyleIcon(PlayerInput input);

        // TFEX-DIFF S-input: LocalPlayersPanel's controller pictures (TF.EX v0.28.0 draws PlayerInput.Icon: on the PC one picture of the
        // controller kind) in the Joy-Con's own colours, as the Switch's archer select draws its players' (JoyConInput.DrawIcon +0xa62300):
        // the style picture (+0x38), then its leftBlend (+0x98) tinted with the left Joy-Con's colour (+0xa8) and its rightBlend (+0xa0)
        // with the right's (+0xac); the panel's fade on all three. The panel hands only the picture: its owner is the local JoyConInput
        // whose picture it is (svc.AsPad: not a remote seat's stand-in over the same Joy-Con). The user, 2026-11-02: the netplay menu's
        // players in grey while archer select showed the real colours.
        public static void DrawControllerIcon(Subtexture icon, Vector2 at, Color color)
        {
            Draw.TextureCentered(icon, at, color);
            PlayerInput owner = null;
            foreach (var input in TFGame.PlayerInputs)
            {
                if (input != null && AsPad(input) != null && JoyConStyleIcon(input) == icon)
                {
                    owner = input;
                    break;
                }
            }
            if (owner == null) return;
            float alpha = color.A / 255f;
            var left = JoyConLeftBlend(owner);
            if (left != null) Draw.TextureCentered(left, at, JoyConLeftColor(owner) * alpha);
            var right = JoyConRightBlend(owner);
            if (right != null) Draw.TextureCentered(right, at, JoyConRightColor(owner) * alpha);
        }
        [Cpp("tfex::gen::svc::AsPad($0)", Include = "tfexgen_services.hpp")] static extern PlayerInput AsPad(PlayerInput input);
        [Cpp("gen::Fld<void*>($0, 0x98)")] static extern Subtexture JoyConLeftBlend(PlayerInput input);
        [Cpp("gen::Fld<void*>($0, 0xa0)")] static extern Subtexture JoyConRightBlend(PlayerInput input);
        [Cpp("gen::Fld<gen::Color>($0, 0xa8)")] static extern Color JoyConLeftColor(PlayerInput input);
        [Cpp("gen::Fld<gen::Color>($0, 0xac)")] static extern Color JoyConRightColor(PlayerInput input);

        // TFEX-DIFF S-input: the Switch's controller refresh in TF.EX's screens outside a lobby (the online menu, NETPLAY: tfex/InputLock.cpp
        // AfterMenuUpdate). The game's MainMenu.Update refreshes the controllers every 60 frames only in its own screens (2/3/5/6); TF.EX
        // wants them updated outside a lobby too (its MainMenu Update prefix: NoGamepadUpdates false), and on the Switch a controller
        // changes there in its own way: a Joy-Con pair split into two single Joy-Cons, a grip change, one attached or detached. The
        // game's own steps: UpdateJoysticks updates each controller's style (JoyConInput.UpdateStyle: the icon LocalPlayersPanel draws);
        // when one attached or detached (GamepadsChanged) AssignInputs rebuilds PlayerInputs, and TF.EX's InputService moves its local
        // player and guests to the new objects (RebindLocalInput, as TF.EX does after its own rebuilds). True when the list changed.
        public static bool RefreshControllers()
        {
            MInput.UpdateJoysticks();
            if (!MInput.GamepadsChanged) return false;
            PlayerInput.AssignInputs();
            ServiceCollections.ResolveInputService().RebindLocalInput();
            return true;
        }

        // Monocle.Atlas[key] = value (overrides.toml rule): FortRise's setter (patch_Atlas: its SubTextures dictionary)
        public static void AtlasSet(Atlas atlas, string key, Subtexture value) { atlas.SubTextures[key] = value; }

        static TF.EX.Domain.Models.Input[] extraLocalInputs;
        public static TF.EX.Domain.Models.Input[] ExtraLocalInputs() =>
            extraLocalInputs ??= new TF.EX.Domain.Models.Input[TF.EX.Domain.Models.Constants.MAX_SEATS];

        // .NET 6's Array.Clear(Array) (the whole array): not in the Switch's Mono, whose one Clear is Clear(Array, int, int)
        public static void ArrayClear(Array array) => Array.Clear(array, 0, array.Length);

        // Monocle's Entity.GetFirst<T>() / Remove<T>() over Tween (TF.State's BramblesExtensions): generic, no Switch instantiation for
        // TF.State's call; Monocle's bodies (Remove<T> steps on after a removal, as Monocle's does)
        public static Tween EntityGetFirstTween(Entity entity)
        {
            if (entity.Components == null) return null;
            for (int i = 0; i < entity.Components.Count; i++) if (entity.Components[i] is Tween t) return t;
            return null;
        }
        public static void EntityRemoveTweens(Entity entity)
        {
            if (entity.Components == null) return;
            for (int i = 0; i < entity.Components.Count; i++) if (entity.Components[i] is Tween) entity.Remove(i);
        }

        // TFEX-DIFF: S-input. InputService.EnsureRemoteControllers' `new KeyboardInput()` for a remote seat (its menu getters muted by
        // IsForeignSeat, its GetState the seat's GGRS input): the Switch has no keyboard, so a JoyConInput over the local JoyCon, whose
        // getters carry the same patches (tfexgen_services.cpp NewRemoteSeatInput)
        [Cpp("tfex::gen::svc::NewRemoteSeatInput()", Include = "tfexgen_services.hpp")] public static extern PlayerInput RemoteSeatInput();

        // Monocle.Cache.Create<T>() is generic; the game's own instantiations, found as the pool lookup (Cache's dictionary, GetTypeFromHandle,
        // pop or new T()) that Explosion.Spawn / SpawnSuper and EnemyAttack.Spawn call: TF.State's LoadExplosions / LoadEnemyAttacks take the
        // same pooled object the game would
        [Cpp("gen::Fn<void* (*)()>(gen::Base() + 0xa3e240)()")] public static extern Explosion CacheCreateExplosion();
        [Cpp("gen::Fn<void* (*)()>(gen::Base() + 0xa38680)()")] public static extern EnemyAttack CacheCreateEnemyAttack();

        // Scene.Add<T>(T entity) is a generic method: BRUTE compiled no instantiation over a TF.EX class (docs/ws-tfex-subclass.md
        // row 6). Its body for an Entity (Monocle's, which BRUTE inlines at every game call site):
        public static void SceneAdd(Scene scene, Entity entity)
        {
            scene.Layers[entity.LayerIndex].Add(entity, false);
        }

        // TFEX-DIFF: S-keyboard. MainMenuPatch.CopyPrivateCodeToClipboard without the clipboard (the Switch has none): the code in TF.EX's
        // notification, so the host can read it out; the rest is TF.EX's body
        public static void ShowPrivateCode(MainMenu self, IMatchmakingService matchmakingService)
        {
            var ownLobby = matchmakingService.GetOwnLobby();
            if (!ownLobby.IsPrivate || string.IsNullOrEmpty(ownLobby.JoinCode)) return;
            Sounds.ui_click.Play();
            Notification.Create(self, "LOBBY CODE: " + ownLobby.JoinCode);
        }

        // TFEX-DIFF: S-wide. InputDelayAdvisor.Render, shifted onto the Switch's wide roll call's right edge (tfex/MainMenuPatch.hpp)
        public static void InputDelayAdvisorRender()
        {
            bool shifted = AdvisorShiftBegin();
            TF.EX.Patchs.InputDelayAdvisor.Render();
            if (shifted) AdvisorShiftEnd();
        }
        [Cpp("tfex::MainMenuPatch::AdvisorShiftBegin()", Include = "../MainMenuPatch.hpp")] static extern bool AdvisorShiftBegin();
        [Cpp("tfex::MainMenuPatch::AdvisorShiftEnd()", Include = "../MainMenuPatch.hpp")] static extern void AdvisorShiftEnd();

        // TFEX-DIFF: S-wide. TF.EX's main-menu entities with a MenuButtonGuide go on the Switch's guide layer (0): its MenuButtonGuide
        // places itself in screen coordinates there (360, or 410 in the wide layout), where the PC's sits on the UI layer (-1) at 310 and
        // takes that layer's camera; on the Switch's -1 (camera +50) TF.EX's guides landed past the screen's right edge
        public static void SceneAddMenu(Scene scene, Entity entity)
        {
            if (entity.LayerIndex == -1 && HasButtonGuide(entity)) entity.LayerIndex = 0;
            SceneAdd(scene, entity);
        }
        static bool HasButtonGuide(Entity entity)
        {
            if (entity.Components == null) return false;
            for (int i = 0; i < entity.Components.Count; i++) if (entity.Components[i] is MenuButtonGuide) return true;
            return false;
        }

        // TFEX-DIFF: S-wide. A TF.EX menu entity drawn at a 320 screen's positions on layer 0 (LocalPlayersPanel): on the Switch's UI layer
        // (-1), whose camera (+50) centres the PC's 320 screen on the main menu's 420 one, as TF.EX's menu buttons sit
        public static void SceneAddUi(Scene scene, Entity entity)
        {
            entity.LayerIndex = -1;
            SceneAdd(scene, entity);
        }

        // Scene.Add<T>(params T[]) / Add<T>(List<T>): Monocle's bodies (one Add per entity), no instantiation over TF.EX's classes
        public static void SceneAddArray(Scene scene, Entity[] entities)
        {
            foreach (Entity entity in entities) SceneAdd(scene, entity);
        }
        public static void SceneAddList<T>(Scene scene, List<T> entities) where T : Entity   // (generic: copied per element type, UnitEmitter.Rewrite)
        {
            foreach (T entity in entities) SceneAdd(scene, entity);
        }

        // Scene.Remove<T>(T entity) / (params T[]) / (List<T>): generic too, and the Switch's one non-generic Remove is Remove(GameTags)
        // (Integration A, 2026-10-17: the binder had paired them). Monocle's body: the entity's layer removes it.
        public static Entity SceneRemove(Scene scene, Entity entity)
        {
            scene.Layers[entity.LayerIndex].Remove(entity);
            return entity;
        }
        public static void SceneRemoveArray(Scene scene, Entity[] entities)
        {
            foreach (Entity entity in entities) SceneRemove(scene, entity);
        }
        public static void SceneRemoveList<T>(Scene scene, List<T> entities) where T : Entity   // (generic: copied per element type)
        {
            foreach (T entity in entities) SceneRemove(scene, entity);
        }

        // Calc.Shuffle<T>(List<T>, Random): Monocle's body (Random.Next(num + 1) from the top), the same draws in the same order as the PC.
        // Generic: the game has no instantiation for TF.EX's element types (MapScene's buttons, TF.State's spawn indexes and positions), so
        // the translator copies it per type argument (UnitEmitter.Rewrite). It was List<MapButton> only, and a List<int> was shuffled
        // through MapButton's accessors: team spawns differed from the PC's (the parity sweep, 2026-10-24)
        public static void Shuffle<T>(List<T> list, System.Random random)
        {
            int num = list.Count;
            while (--num > 0)
            {
                T value = list[num];
                int index = random.Next(num + 1);
                list[num] = list[index];
                list[index] = value;
            }
        }

        // `new Coroutine(Seq())` over a TF.EX iterator: the iterator's coroutine twin (gen::CoGen), stepped by a looping 1-frame Tween as
        // Coroutine.Update steps its enumerator (tfexgen_code_rt.hpp CoStep), removed when it ends
        [Cpp("tfex::gen::CoStep($0)", Include = "tfexgen_code_rt.hpp")]
        static extern bool CoStep(long run);
        public static Component CoroutineOf(long run)
        {
            Tween tween = Tween.Create(Tween.TweenMode.Looping, null, 1, true);
            tween.OnUpdate = t => { if (!CoStep(run)) t.RemoveSelf(); };
            return tween;
        }

        // TF.EX's InputService.GetInputIndex: TFGame.PlayerInputs.Select((x, i) => new { Value = x, Index = i }).FirstOrDefault(x => x.Value
        // == input)?.Index ?? -1 (an anonymous type: not translated). The same search:
        public static int InputIndex(PlayerInput input)
        {
            for (int i = 0; i < TFGame.PlayerInputs.Length; i++)
            {
                if (TFGame.PlayerInputs[i] == input) return i;
            }
            return -1;
        }

        // TF.EX.Domain.Utils.VersusGraphics.GetVersusGraphics is decompiler output of `[image, image2]` (CollectionsMarshal.SetCount +
        // AsSpan: no Span on the Switch's runtime). The same list, built with Add:
        public static List<Image> VersusGraphics(int levelID)
        {
            TowerTheme towerTheme = GameData.VersusTowers[levelID].Theme;
            Image image = new Image(MapButton.GetBlockTexture(towerTheme.TowerType));
            image.CenterOrigin();
            Image image2 = new Image(towerTheme.Icon);
            image2.CenterOrigin();
            image2.Color = MapButton.GetTint(towerTheme.TowerType);
            List<Image> list = new List<Image>(2);
            list.Add(image);
            list.Add(image2);
            return list;
        }

        // TFEX-DIFF: S-native. SpriteBatch.Begin(..., Effect, Matrix): the Switch's MonoGame takes a Matrix? (the runtime's SpriteBatchBegin
        // passes it so); the PC's FNA a Matrix
        [Cpp("tfex::gen::SpriteBatchBegin($0, (int32_t)$1, $2, $3, $4, $5, $6, $7)")]
        public static extern void SpriteBatchBegin(SpriteBatch batch, SpriteSortMode sortMode, BlendState blend, SamplerState sampler,
            DepthStencilState depthStencil, RasterizerState rasterizer, Effect effect, Matrix transform);

        // TFEX-DIFF: D (spectators, step 4). Spectators aren't on the Switch yet (backlog step 4: no rollback / input path for a session without local
        // players), so the lobby list's SPECTATE (MainMenuPatch: Alt2 -> JoinLobby(roomId, false, ...)) can't start one. The join as a
        // player goes through; a spectator join undoes what the button did before it (controllers off, the loader) and says why. Its
        // button guide is hidden (SpectateGuide). Remove both rules with spectators.
        public static void JoinLobbyNoSpectate(IMatchmakingService service, string roomId, bool isPlayer, Action onSuccess, Action onFail)
        {
            if (isPlayer) { service.JoinLobby(roomId, isPlayer, onSuccess, onFail); return; }
            LogSpectateRefused();
            ServiceCollections.ResolveInputService().EnableAllControllers();
            Scene scene = Engine.Instance.Scene;
            scene.RemoveLoader();
            Notification.Create(scene, "SPECTATING ISN'T ON THE SWITCH YET");
            Sounds.ui_invalid.Play();
        }
        [Cpp("tfex::gen::ffi::Log(\"[ui] SPECTATE refused: spectators aren't on the Switch yet (the notification shown)\")", Include = "tfexgen_ffi.hpp")]
        static extern void LogSpectateRefused();
        public static void SpectateGuide(MenuButtonGuide guide, MenuButtonGuide.ButtonModes mode, string text)
        {
            guide.SetDetails(mode, text == "COPY CODE" ? "SHOW CODE" : text);   // (no clipboard: ShowPrivateCode)
            if (text == "SPECTATE") guide.Visible = false;
        }
    }
}
