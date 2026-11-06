// The Archer mod's profile names API on the Switch (what ebe1kenobi's Archer returns from GetApi on the PC: its IProfilesModApi, the
// player names on archer select). The Switch doesn't carry Archer (profiles and the Forge: the user's decision, 2026-11-05), so this
// stands in for it: a name per player slot, kept as Archer keeps it, not drawn anywhere (the game's own roll call draws no names).
// translate.toml [[interop]] maps every mod's copy of the interface onto this class (LoaderAI's TFModFortRiseProfiles.IProfilesModApi).
namespace TfexGlue
{
    public class ProfilesApi
    {
        // (no state of its own: GetApi's object is never constructed on the Switch, so the names live in a static class, made at first use)
        public void SetPlayerName(int playerIndex, string playerName) => ProfileNames.Set(playerIndex, playerName);
        public string GetPlayerName(int playerIndex) => ProfileNames.Get(playerIndex);
    }

    public static class ProfileNames
    {
        static string[] names = new string[8];

        public static void Set(int playerIndex, string playerName)
        {
            if (playerIndex >= 0 && playerIndex < names.Length) names[playerIndex] = playerName;
        }

        public static string Get(int playerIndex)
        {
            if (playerIndex < 0 || playerIndex >= names.Length || names[playerIndex] == null) return "P" + (playerIndex + 1);
            return names[playerIndex];
        }
    }
}
