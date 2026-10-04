#include "FsmInit.h"
#include "FsmActionRegistry.h"
#include "FsmGraph.h"

// Global scope, matching FsmInit.h - see the comment there.
void DekiFsmInitSystem()
{
    DekiFsm::RegisterGraphLoader();
    DekiFsm::RegisterActionLibrary();
}

void DekiFsmShutdownSystem()
{
}
