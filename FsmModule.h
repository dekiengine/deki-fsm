#pragma once

/**
 * @file FsmModule.h
 * @brief Central header for the Deki FSM Module.
 *
 * PlayMaker-style finite state machines: an FsmComponent on any object runs a
 * state-machine graph asset (an "FsmGraph" .asset authored in the editor's
 * Node Graph window). States are canvas nodes carrying an ordered stack of
 * Actions (small reusable parameterized code units); transitions are wires
 * labeled with event names. When every action in the active state finishes,
 * the built-in FINISHED event fires; actions and game code raise custom events
 * via FsmComponent::SendEvent().
 *
 * Add a game-specific action by declaring a DEKI_NODE struct with category
 * "Fsm/Actions" (see FsmActions.h) and registering its runtime behavior with
 * REGISTER_FSM_ACTION (see FsmActionRegistry.h) — from this module or any
 * project DLL.
 */

// DLL export macro
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

// Include all module headers when module is enabled
#ifdef DEKI_MODULE_FSM

#include "FsmGraph.h"
#include "FsmNodes.h"
#include "FsmActions.h"
#include "FsmActionRegistry.h"
#include "FsmComponent.h"

#endif // DEKI_MODULE_FSM
