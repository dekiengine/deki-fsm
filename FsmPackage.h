#pragma once

// Main header of the FSM package.
//
// PlayMaker-style state machines: an FsmComponent on any object runs a state
// machine graph asset (an "FsmGraph" .asset made in the editor's Node Graph
// window). States are nodes holding an ordered stack of Actions, small
// reusable code units with parameters. Transitions are wires labeled with
// event names. When every action in the active state finishes, the built-in
// FINISHED event fires; actions and game code raise their own events with
// FsmComponent::SendEvent().
//
// To add a game action, declare a DEKI_NODE struct with category
// "Fsm/Actions" (see FsmActions.h) and register its runtime behavior with
// REGISTER_FSM_ACTION (see FsmActionRegistry.h), in this package or in any
// project DLL.

#include "FsmApi.h"

#ifdef DEKI_PACKAGE_FSM

#include "FsmGraph.h"
#include "FsmNodes.h"
#include "FsmActions.h"
#include "FsmActionRegistry.h"
#include "FsmComponent.h"

#endif  // DEKI_PACKAGE_FSM
