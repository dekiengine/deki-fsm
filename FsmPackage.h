#pragma once

/**
 * @file FsmPackage.h
 * @brief Central header for the Deki FSM Package.
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
 * REGISTER_FSM_ACTION (see FsmActionRegistry.h) — from this package or any
 * project DLL.
 */

// DLL export macro (own header so intra-package headers avoid this aggregator)
#include "FsmApi.h"

// Include all package headers when package is enabled
#ifdef DEKI_PACKAGE_FSM

#include "FsmGraph.h"
#include "FsmNodes.h"
#include "FsmActions.h"
#include "FsmActionRegistry.h"
#include "FsmComponent.h"

#endif // DEKI_PACKAGE_FSM
