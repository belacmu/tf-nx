/* TF.EX's wide set: WiderSet 0.9.0's Content/WideLevels/Versus (docs/ws-wide.md). WiderSet registers each folder as a FortRise versus
 * tower, in folder order (IDs 16..31 after the 16 official towers), with its .oel files in FortRise's order (zip entries sorted by name),
 * which is the order VersusTowerData.Levels has on the PC and the order GenLevels draws from. tools/pc-levels.sh copies the files to the
 * romfs at Content/Levels/PCWide/<folder>/<name>.oel and checks them against this table. The folder names are also the Switch's
 * Content/Levels/6P-Versus tower folders. */
#pragma once
namespace widelevels {
constexpr int kTowers = 16, kMaxLevels = 15;
struct Tower { const char* folder; const char* levels[kMaxLevels + 1]; };
constexpr Tower kTable[kTowers] = {
    { "00 - Sacred Ground", { "00", "01", "02", "03", "04", "05", "06", "07", "08", "09", "10", "a00" } },
    { "01 - Twilight Spire", { "00", "01", "02", "03", "04", "05", "06", "07", "08", "09", "10", "11" } },
    { "02 - Backfire", { "00", "01", "02", "03", "04", "05", "06", "07", "08", "09" } },
    { "03 - Flight", { "00", "01", "02", "03", "04", "05", "06", "07", "08", "09", "10", "11" } },
    { "04 - Mirage", { "00", "01", "02", "03", "04", "05", "06", "07", "08", "09", "10", "11", "12" } },
    { "05 - Thornwood", { "00", "01", "02", "03", "04", "05", "06", "07", "08", "09" } },
    { "06 - Frostfang Keep", { "00", "01", "02", "03", "04", "05", "06", "07", "08", "09", "10", "11", "12" } },
    { "07 - Kings Court", { "00", "01", "02", "03", "04", "05", "06", "07", "08", "09", "10", "11" } },
    { "08 - Sunken City", { "00", "01", "02", "03", "04", "05", "06", "07", "08", "09", "10", "11" } },
    { "09 - Moonstone", { "00", "01", "02", "03", "04", "05", "06", "07", "08", "09", "10", "11", "12" } },
    { "10 - TowerForge", { "00", "01", "02", "03", "04", "05", "06", "07", "08", "09", "10" } },
    { "11 - Ascension", { "00", "01", "02", "03", "04", "05", "06", "07", "08", "09", "10", "11" } },
    { "12 - The Amaranth", { "00", "01", "02", "03", "04", "05", "06", "07", "08", "09", "10", "11", "12" } },
    { "13 - Dreadwood", { "00", "01", "02", "03", "04", "05", "06", "07", "08", "09", "10", "11" } },
    { "14 - Darkfang", { "00", "01", "02", "03", "04", "05", "06", "07", "08", "09", "10", "11" } },
    { "15 - Cataclysm", { "00", "01", "02", "03", "04", "05", "06", "07", "08", "09", "10", "11", "12", "13", "14" } },
};
}
