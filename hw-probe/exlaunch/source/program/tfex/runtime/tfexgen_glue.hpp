/* Hooks the generated TF.EX code into the module (hw-probe/exlaunch/source/program/tfex/runtime, compiled in place). xplay.cpp calls HwLine for
 * each hw.txt line and Install once with the other hooks. */
#pragma once
namespace tfex::gen {
bool HwLine(const char* line);   /* true if the line was ours: "genpatch <Class>", "gennotiftest <frame>" */
void Install();
}
