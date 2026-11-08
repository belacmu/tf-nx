// TFEX-DIFF S-native: FortRise's module settings page doesn't exist on the Switch. TF.EX's NetplaySettings.Create (its settings, TF.EX's
// own code) builds its entries through FortRise's ISettingsCreate; on the Switch those calls (translate.toml [[rewrite]]) make the same
// entries FortRise's OptionsCreate makes (the game's OptionsButton with SetCallbacks: the value as its State, Left / Right for numbers,
// Confirm cycles options / toggles on-off / opens the text input) into the list the game's OPTIONS screen lays out (MainMenu.InitOptions,
// tfex/NetplayOptions.cpp). Each entry's description as FortRise's DescribeLastEntry keeps it (OptionsButtonExt.Description: DynamicData
// "fortrise_description"), for FortRise's OptionDescriptionBox under the list (generated with its InitOptions).
// FortRise's UIInputText (a typed-text entity that calls back when done) -> the Switch's software keyboard (swkbd, as the join code):
// it blocks while open, so its text (or the cancel) is handed back one frame later, as the entity's callback would come.
using System;
using System.Collections.Generic;
using Microsoft.Xna.Framework;
using Monocle;
using MonoMod.Utils;
using TowerFall;

namespace TfexGlue
{
    public static class SettingsUi
    {
        // FortRise OptionsCreate.CreateNumber
        public static void CreateNumber(List<OptionsButton> buttons, string name, int initialValue, Action<int> onChanged, string description, int min, int max, int step)
        {
            var button = new OptionsButton(name.ToUpperInvariant());
            int value = initialValue;
            button.SetCallbacks(() =>
            {
                button.State = value.ToString();
                button.CanLeft = value > min;
                button.CanRight = value < max;
            }, () =>
            {
                value -= step;
                onChanged(value);
            }, () =>
            {
                value += step;
                onChanged(value);
            }, null);
            buttons.Add(button);
            DescribeLastEntry(buttons, description);
        }

        // FortRise OptionsCreate.CreateOptions: Confirm steps to the next selection
        public static void CreateOptions(List<OptionsButton> buttons, string name, string initialValue, string[] selections, Action<(string, int)> onSelect, string description)
        {
            var button = new OptionsButton(name.ToUpperInvariant());
            int index = 0;
            for (int i = 0; i < selections.Length; i++)
            {
                if (selections[i] == initialValue)
                {
                    index = i;
                    break;
                }
            }
            button.SetCallbacks(() =>
            {
                button.State = selections[index].ToUpperInvariant();
            }, null, null, () =>
            {
                index++;
                if (index >= selections.Length)
                {
                    index = 0;
                }
                onSelect((selections[index], index));
                return true;
            });
            buttons.Add(button);
            DescribeLastEntry(buttons, description);
        }

        // FortRise OptionsCreate.CreateOnOff (no restart prompt: TF.EX's entries don't ask for one)
        public static void CreateOnOff(List<OptionsButton> buttons, string name, bool initialValue, Action<bool> onPressed, string description)
        {
            var button = new OptionsButton(name.ToUpperInvariant());
            bool value = initialValue;
            button.SetCallbacks(() =>
            {
                button.State = value ? "ON" : "OFF";
            }, null, null, () =>
            {
                value = !value;
                onPressed(value);
                return value;
            });
            buttons.Add(button);
            DescribeLastEntry(buttons, description);
            Updates.NoteOnOff(name, button);   // (TF.EX's AUTO UPDATE: moved to the TF.NX section, glue SwitchMods.InitModOptions)
        }

        // FortRise OptionsCreate.CreateInput: Confirm opens the text input; the typed text is the value
        public static void CreateInput(List<OptionsButton> buttons, string name, string initialValue, Action<string> onInput, string description, int inputBehavior)
        {
            var button = new OptionsButton(name.ToUpperInvariant());
            string value = initialValue;
            button.SetCallbacks(() =>
            {
                button.State = (value ?? "").ToUpperInvariant();
            }, null, null, () =>
            {
                var input = KeyboardText(button, text =>
                {
                    onInput(text);
                    value = text;
                    button.Selected = true;
                    button.State = text.ToUpperInvariant();
                }, Vector2.Zero, -1, value);
                button.MainMenu.Add(input);
                button.Selected = false;
                return true;
            });
            buttons.Add(button);
            DescribeLastEntry(buttons, description);
        }

        // FortRise OptionsCreate.DescribeLastEntry
        private static void DescribeLastEntry(List<OptionsButton> buttons, string description)
        {
            if (!string.IsNullOrWhiteSpace(description) && buttons.Count > 0)
            {
                DynamicData.For(buttons[buttons.Count - 1]).Set("fortrise_description", description);
            }
        }

        // FortRise OptionsCreate.CreateCustomOptions: the entry TF.EX makes itself (ServerOptionsButton)
        // FortRise's shorter ISettingsCreate overloads (Core/Module/ModuleSettings.cs OptionsCreate): the full entries with their defaults
        public static void CreateOnOffPlain(List<OptionsButton> buttons, string name, bool initialValue, Action<bool> onPressed) => CreateOnOff(buttons, name, initialValue, onPressed, "");
        public static void CreateOnOffRestart(List<OptionsButton> buttons, string name, bool initialValue, Action<bool> onPressed, bool restartRequired) => CreateOnOff(buttons, name, initialValue, onPressed, "");
        public static void CreateNumberPlain(List<OptionsButton> buttons, string name, int initialValue, Action<int> onChanged, int min, int max, int step) => CreateNumber(buttons, name, initialValue, onChanged, "", min, max, step);
        public static void CreateOptionsPlain(List<OptionsButton> buttons, string name, string initialValue, string[] selections, Action<(string, int)> onSelect) => CreateOptions(buttons, name, initialValue, selections, onSelect, "");
        public static void CreateCustomOptions(List<OptionsButton> buttons, Func<OptionsButton> onCreate)
        {
            buttons.Add(onCreate());
        }

        // FortRise's `new UIInputText(backItem, finished, position, playerIndex, initialValue)`: the software keyboard once added
        public static Entity KeyboardText(MenuItem backItem, Action<string> finished, Vector2 position, int playerIndex, string initialValue)
        {
            return new KeyboardEntity(backItem, finished, initialValue);
        }

        [Cpp("tfex::NetplayOptions::ReadKeyboard($0)", Include = "../NetplayOptions.hpp")]
        internal static extern string ReadKeyboard(string initialValue);
    }

    // FortRise's UIInputText on the Switch: added to a scene, it opens the software keyboard (blocking, as the join code's); one frame later,
    // as UIInputText's own finish, backItem is selected again and finished(text) runs (not when the keyboard was cancelled). Added again,
    // it asks again (FortRise's UISearchPanel keeps one and adds it on each confirm).
    public class KeyboardEntity : Entity
    {
        private MenuItem backItem;
        private Action<string> finished;
        private string initialValue;

        public KeyboardEntity(MenuItem backItem, Action<string> finished, string initialValue)
        {
            this.backItem = backItem;
            this.finished = finished;
            this.initialValue = initialValue;
        }

        public override void Added()
        {
            base.Added();
            string text = SettingsUi.ReadKeyboard(initialValue ?? "");
            Alarm.Set(this, 1, () =>
            {
                backItem.Selected = true;
                RemoveSelf();
                if (text != null)
                {
                    finished(text);
                }
            });
        }
    }
}
