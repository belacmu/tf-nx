/* TF.EX's scenario sweep on the Switch (the TF.State plan, TEST ONLY): what its map writer needs from the BCL, the Switch's way.
 *  - Regex: the subset TF.EX's ScenarioMapWriter uses (literals, `.`, [^...] / [...], `*` and lazy `*?` on one item, groups (...),
 *    RegexOptions.Singleline; Replace with $1..$9). An unsupported pattern is logged and matches nothing.
 *  - File.WriteAllText of a scenario map: the Switch's game folder is read-only (S-native), so the text is kept in memory under its
 *    path; Calc.LoadXML of that path (pcgameplay.cpp's LoadXML hook) gets it as an XmlDocument. 128 maps at most (PoolFull, logged).
 * overrides.toml [service] rules map the calls here. */
#pragma once
#include "tfexgen_code_rt.hpp"

namespace tfex::gen::scen {
void* RegexNew(void* pattern, int32_t options);                       /* new Regex(pattern, options) */
void* RegexReplace(void* rx, void* input, void* replacement, int32_t count);   /* rx.Replace(input, replacement, count) */
void MemFileWrite(void* path, void* text);                             /* File.WriteAllText(path, text) */
void* MemFileXml(void* path);                                          /* the map at path as an XmlDocument, or null (not one of ours) */
/* hw.txt "statescenario <name>" (TEST ONLY): a lobby match plays that ScenarioCatalog scenario's map (TF.EX's ScenarioMapWriter.Write,
 * kept in memory): pcgameplay.cpp's GetLevels hook lists only ScenarioLevelPath() (null: no scenario, or not found) */
void ConfigureScenario(const char* name);
void* ScenarioLevelPath();
}
