#include "FsmInit.h"
#include "FsmActionRegistry.h"
#include "FsmGraph.h"

// Global scope, matching FsmInit.h - see the comment there.
void DekiFsm_InitSystem()
{
    DekiFsm::RegisterGraphLoader();
    DekiFsm::RegisterActionLibrary();
}

void DekiFsm_ShutdownSystem()
{
}
