/* System.Random on the Switch (.NET's algorithm in place of the runtime's JKISS32) and the route to TF.State's gameplay stream: TF.State's
 * StateContext / RngService / CalcPatch / DeterministicRandom are its own code, generated (tfex/gen); the Random bodies hooked here send
 * an object that is a DeterministicRandom to its generated methods (the AOT build calls them devirtualized). See rnd.cpp. */
#pragma once
#include <cstdint>

namespace rnd {
void* CalcRandomObject();   /* the current Monocle.Calc.Random object */
/* StateFlags.IsCaptureActive: a netplay session (or the rollback test) is running. */
void SetCapture(bool on);
bool Capture();
/* TF.EX's end-of-tick flush: Layer.UpdateEntityList on the gameplay and HUD layers, after every Level.Update while capture is on. */
/* The lobby match's seed, checked against TF.State's stream seed (RngService.GetSeed). */
void SetLobbySeed(int32_t seed);
void SetGameTrace(int n, int fromFrame);    /* debug: log n gameplay draws from frame fromFrame on */
void DumpDraws(void (*out)(const char*));   /* the last 256 gameplay draws (site, frame), one line each */
void SetTraceFrame(int f);   /* the frame label those lines carry */
/* Gameplay draws (calls on a DeterministicRandom) made so far, and the stream's four state words folded into one number (per-tick logs). */
uint64_t DrawCount();
uint64_t StateFold();

/* The managed type name of an object (via its class record), or "?". */
const char* TypeName(void* obj, char* out, int cap);
/* The code address in vtable slot `slot` (byte offset) of obj. */
uintptr_t VtableBody(void* obj, uint32_t slot);
}
