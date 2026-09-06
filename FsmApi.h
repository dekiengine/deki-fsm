#pragma once

// DLL export macro, in its own header so intra-package headers can use it
// without pulling the FsmPackage.h aggregator (which includes everything and
// would create include cycles — e.g. FsmActionRegistry.h -> FsmPackage.h ->
// FsmComponent.h -> FsmActionRegistry.h left FsmActionOps undefined).
#ifdef DEKI_EDITOR
    #ifdef _WIN32
        #ifdef DEKI_FSM_EXPORTS
            #define DEKI_FSM_API __declspec(dllexport)
        #else
            #define DEKI_FSM_API __declspec(dllimport)
        #endif
    #else
        #define DEKI_FSM_API
    #endif
#else
    #define DEKI_FSM_API
#endif
