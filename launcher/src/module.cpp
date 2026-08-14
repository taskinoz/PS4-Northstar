#include <orbis/libkernel.h>
#include "northstar_ps4/runtime.h"

extern "C" __attribute__((constructor)) void NorthstarPs4Init() {
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
