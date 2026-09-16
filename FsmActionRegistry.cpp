#include "FsmActionRegistry.h"

namespace DekiFsm
{

FsmActionRegistry& FsmActionRegistry::Instance()
{
    static FsmActionRegistry instance;
    return instance;
}

}  // namespace DekiFsm
