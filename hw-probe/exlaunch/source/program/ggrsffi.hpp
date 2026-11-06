#pragma once
/* TF.EX's ggrs_ffi on the Switch (docs/ws-ggrs-ffi.md): its socket thread (net.cpp ffisock) and its per-frame pump. Always on
 * (the path, no setting); idle until TF.EX's netplay code opens a ggrs_ffi session or ping measurement. */
namespace ggrsffi {
void Frame();          /* every frame, before TF.EX's netplay update (xplay FrameShell): netplay_switch_pump + its log lines */
}
