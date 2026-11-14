/* AINova's Hazard for glue NovaGrid (translator/glue/NovaGrid.cs: the AIs' world grid once a frame). AINova is a code mod with no licence
 * to distribute (translate.toml public = false): without it in the build (the public tree) nothing calls this. */
#pragma once
#include "gen/tfexgen_codemods.hpp"
#include "runtime/tfexgen_code_rt.hpp"
#ifdef TFEX_CODEMOD_AINova
#include "gen/tfexgen_Auto_WorldSnapshot.hpp"
#endif
namespace tfex::nova {
inline bool Hazard(void* level, tfex::gen::Vector2 at) {
#ifdef TFEX_CODEMOD_AINova
    return tfex::gen::Auto_WorldSnapshot::Hazard(level, at);
#else
    (void)level; (void)at;
    return false;
#endif
}
}  // namespace tfex::nova
