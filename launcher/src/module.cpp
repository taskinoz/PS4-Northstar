#include <orbis/libkernel.h>
#include "northstar_ps4/runtime.h"

// Entered from DT_INIT, .init_array and GoldHEN's plugin_load. A PS4 calls
// DT_INIT with the stack 8 bytes off the ABI's alignment (shadPS4 does not), and
// the first aligned SSE store in vfprintf faulted (2026-10-07), so the entry
// points realign it.
extern "C" __attribute__((constructor, force_align_arg_pointer)) void NorthstarPs4Init() {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;
    sceKernelDebugOutText(0, "[NorthstarPS4] Stage 2 PoC initializer executed\n");
    northstar::ps4::Initialize(northstar::ps4::InitStage::ModuleLoaded);
}

extern "C" __attribute__((destructor)) void NorthstarPs4Fini() {
    sceKernelDebugOutText(0, "[NorthstarPS4] Stage 2 PoC finalizer executed\n");
}

extern "C" int NorthstarPs4PocVersion() {
    return 1;
}

// GoldHEN's plugin loader, on a PS4: plugins.ini lists this PRX under
// [CUSA04013], and GoldHEN loads it into the game and calls plugin_load. The
// constructor has usually run by then; this covers a loader that skips it.
extern "C" const char* g_pluginName = "northstar_ps4";
extern "C" const char* g_pluginDesc = "Northstar for the PS4 version of Titanfall 2";
extern "C" const char* g_pluginAuth = "PS4 Northstar";
extern "C" unsigned int g_pluginVersion = 0x00010000;

extern "C" __attribute__((force_align_arg_pointer)) int plugin_load(int, const char**) {
    NorthstarPs4Init();
    return 0;
}

// The runtime's hooks stay in the game, so it is never unloaded.
extern "C" __attribute__((force_align_arg_pointer)) int plugin_unload(int, const char**) {
    return 0;
}
