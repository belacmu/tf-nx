// LoaderAI's AI players' names on the round results: Archer's MyVersusRoundResults.Update_patch (the PC shows Archer's player names there),
// for the AI players only. Archer isn't on the Switch (the user, 2026-11-05: no profiles, no Forge); its names part for AIs is wanted
// (2026-11-06). The name is LoaderAI's (GetAIPlayerName: "JIMMY 3"), through tfex/AiSlotButton.cpp; a human keeps the game's "P1".
// Placed in the game's layout, not Archer's X 20 (the 8-player board had every AI name off the screen's left edge, 2026-11-07).
// Called by tfex/AiSlotButton.cpp's hook on VersusRoundResults.Update, as Archer's Harmony prefix runs there.
using System;
using Microsoft.Xna.Framework;
using Monocle;
using TowerFall;

namespace TfexGlue
{
    public static class AiNames
    {
        [Cpp("tfex::aislot::AiNameOf($0)", Include = "../AiSlotButton.hpp")] static extern string AiNameOf(int playerIndex);
        [Cpp("tfex::aislot::BoardLaidOut($0, $1)", Include = "../AiSlotButton.hpp")] static extern void BoardLaidOut(int names, int labels);

        public static void RoundResults(VersusRoundResults results)
        {
            if (results.Components == null || !AnyToName(results)) return;
            Text[] labels = new Text[8];
            int count = 0;
            int names = 0;
            for (int i = 0; i < results.Components.Count; i++)
            {
                if (results.Components[i] is not Text text) continue;
                string current = text.DrawText;   // (Archer reads and writes the private field through DynamicData; the property is the same text)
                if (string.IsNullOrEmpty(current) || current.Length < 2) continue;
                if (current[0] != 'P' || current[1] < '1' || current[1] > '8') continue;   // ("P1".."P8": Archer's char.IsDigit)
                if (count < 8) labels[count++] = text;
                int playerIndex = current[1] - '1';
                string name = AiNameOf(playerIndex);
                if (name == null) continue;
                text.DrawText = name;   // (right-aligned: its right edge stays at the game's place, by the coins)
                names++;
            }
            if (names == 0) return;
            LayOutRows(results, labels, count);
            BoardLaidOut(names, count);
        }

        // a "P#" label of an AI still there (the first frame of the board): only then the names and the layout (no array every frame)
        static bool AnyToName(VersusRoundResults results)
        {
            for (int i = 0; i < results.Components.Count; i++)
            {
                if (results.Components[i] is not Text text) continue;
                string current = text.DrawText;
                if (string.IsNullOrEmpty(current) || current.Length < 2) continue;
                if (current[0] != 'P' || current[1] < '1' || current[1] > '8') continue;
                if (AiNameOf(current[1] - '1') != null) return true;
            }
            return false;
        }

        // TFEX-DIFF S-layout: Archer moves a name to X 20 (its own layout). The game right-aligns a row's "P#" labels 25 px apart (9 px
        // between them) and its crown 25 px left of the last (VersusRoundResults' ctor): with names, each row's labels are laid out again
        // right to left from the rightmost one's place, 9 px apart, and the row's crown moved to 9 px left of them. A row too wide for the
        // screen (a team of AIs) wraps onto more lines at the game's size, and the rows below it move down by those lines (their labels,
        // coins, crowns and counts), so nothing overlaps (the user, 2026-11-08: wrapping, no smaller names).
        const float MinLeft = 4f;
        const float Gap = 9f;

        static void LayOutRows(VersusRoundResults results, Text[] labels, int count)
        {
            bool[] done = new bool[8];
            while (true)
            {
                // the next row from the top: its labels, rightmost first (as the game placed them)
                int top = -1;
                for (int b = 0; b < count; b++)
                    if (labels[b] != null && !done[b] && (top < 0 || labels[b].Position.Y < labels[top].Position.Y)) top = b;
                if (top < 0) break;
                float rowY = labels[top].Position.Y;
                Text[] row = new Text[8];
                int inRow = 0;
                while (true)
                {
                    int next = -1;
                    for (int b = 0; b < count; b++)
                    {
                        if (labels[b] == null || done[b] || Math.Abs(labels[b].Position.Y - rowY) >= 3f) continue;
                        if (next < 0 || labels[b].Position.X > labels[next].Position.X) next = b;
                    }
                    if (next < 0) break;
                    done[next] = true;
                    row[inRow++] = labels[next];
                }
                float right = row[0].Position.X;
                float lineHeight = TFGame.Font.MeasureString("P").Y * row[0].Scale.Y + 2f;
                int lines = 1;
                float left = PlaceRow(row, inRow, rowY, right, lineHeight, ref lines);
                float oldNext = right - 25f * inRow;   // (where the game put the crown's right edge)
                float extra = (lines - 1) * lineHeight;
                for (int i = 0; i < results.Components.Count; i++)
                {
                    if (results.Components[i] is Image image && Math.Abs(image.Position.Y + 1f - rowY) < 3f && image.Position.X < right)
                        image.Position.X += left - Gap - oldNext;
                    // the rows below this one move down by its extra lines (this row's own labels are placed already)
                    else if (extra > 0f && results.Components[i] is GraphicsComponent g && g.Position.Y > rowY + 3f && !InRow(row, inRow, g))
                        g.Position.Y += extra;
                }
            }
        }

        static bool InRow(Text[] row, int inRow, GraphicsComponent g)
        {
            for (int b = 0; b < inRow; b++) if (row[b] == g) return true;
            return false;
        }

        // the row's labels (rightmost first) right to left from `right`, Gap px apart; a label that would pass MinLeft starts a new line at
        // `right`, lineHeight below. Returns the left edge of the widest line, the number of lines in `lines`
        static float PlaceRow(Text[] row, int inRow, float rowY, float right, float lineHeight, ref int lines)
        {
            float edge = right;
            float left = right;
            lines = 1;
            for (int b = 0; b < inRow; b++)
            {
                float width = row[b].Origin.X * row[b].Scale.X;
                if (edge < right && edge - width < MinLeft)
                {
                    lines++;
                    edge = right;
                }
                row[b].Position.X = edge;
                row[b].Position.Y = rowY + (lines - 1) * lineHeight;
                if (edge - width < left) left = edge - width;
                edge = edge - width - Gap;
            }
            return left;
        }
    }
}
