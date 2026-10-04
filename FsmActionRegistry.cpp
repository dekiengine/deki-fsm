#include "FsmActionRegistry.h"

namespace DekiFsm
{

FsmActionRegistry& FsmActionRegistry::Instance()
{
    static FsmActionRegistry s_Instance;
    return s_Instance;
}

}  // namespace DekiFsm
