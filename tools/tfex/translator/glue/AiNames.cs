// LoaderAI's AI players' names on the round results: Archer's MyVersusRoundResults.Update_patch (the PC shows Archer's player names there),
// for the AI players only. Archer isn't on the Switch (the user, 2026-11-05: no profiles, no Forge); its names part for AIs is wanted
// (2026-11-06). The name is LoaderAI's (GetAIPlayerName: "JIMMY 3"), through tfex/AiSlotButton.cpp; a human keeps the game's "P1".
// Called by tfex/AiSlotButton.cpp's hook on VersusRoundResults.Update, as Archer's Harmony prefix runs there.
using Monocle;
using TowerFall;

namespace TfexGlue
{
    public static class AiNames
    {
        [Cpp("tfex::aislot::AiNameOf($0)", Include = "../AiSlotButton.hpp")] static extern string AiNameOf(int playerIndex);

        public static void RoundResults(VersusRoundResults results)
        {
            if (results.Components == null) return;
            for (int i = 0; i < results.Components.Count; i++)
            {
                if (results.Components[i] is not Text text) continue;
                string current = text.DrawText;   // (Archer reads and writes the private field through DynamicData; the property is the same text)
                if (string.IsNullOrEmpty(current) || current.Length < 2) continue;
                if (current[0] != 'P' || current[1] < '1' || current[1] > '8') continue;   // ("P1".."P8": Archer's char.IsDigit)
                int playerIndex = current[1] - '1';
                string name = AiNameOf(playerIndex);
                if (name == null) continue;
                text.DrawText = name;
                text.Position.X = 20;   // (Archer's: a name is longer than "P3")
            }
        }
    }
}
