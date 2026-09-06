#include "FsmActionRegistry.h"

FsmActionRegistry& FsmActionRegistry::Instance()
{
    static FsmActionRegistry instance;
    return instance;
}
