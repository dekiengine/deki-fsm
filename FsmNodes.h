#pragma once

#include <string>
#include <vector>

#include "reflection/DekiNode.h"

// Node vocabulary for the state-machine graph ("Fsm" domain).
//
// A graph is states connected by event wires. Exactly one state is active per
// FsmComponent; its action stack (child instances, category "Fsm/Actions")
// runs every frame. Each entry in a state's `transitions` list is one output
// pin, labeled with the event name it listens for; when that event fires the
// machine follows the wire to the next state. "FINISHED" is the built-in
// event raised when every action in the state has finished.

// Where the machine starts. Exactly one per graph; its single output points at
// the initial state.
struct FsmStartNode
{
    DEKI_NODE(FsmStartNode, "FsmStart", "Fsm/Flow")
    static constexpr const char* StaticNodeDisplayName = "Start";
    DEKI_NODE_OUTPUTS("start")
};

// Always-on action stack: the FSM's Update() lifecycle, mirroring a script's.
// A standalone node (no pins, park it anywhere on the canvas) whose enabled
// actions run every frame for the life of the machine, regardless of which
// state is active. Use it for global watchers: a Compare Property or Watch
// Button here raises events that transition the main machine from ANY state
// (the event is still matched against the active state's transitions). Actions
// that finish (a one-shot Set Property, a Wait) simply stop; there is no
// FINISHED here because there is nothing to transition. A graph may hold any
// number of Update nodes; all their stacks run.
struct FsmUpdateNode
{
    DEKI_NODE(FsmUpdateNode, "FsmUpdate", "Fsm/Flow")
    static constexpr const char* StaticNodeDisplayName = "Update";
    DEKI_NODE_CHILDREN("Fsm/Actions")
};

// One state: a named node holding an ordered stack of actions (authored in the
// inspector, PlayMaker-style) plus one output pin per transition event. The
// canvas shows the state's `name` as its title.
struct FsmStateNode
{
    DEKI_NODE(FsmStateNode, "FsmState", "Fsm/Flow")
    static constexpr const char* StaticNodeDisplayName = "State";
    DEKI_NODE_INPUTS("in")
    DEKI_NODE_DYNAMIC_OUTPUTS("transitions")
    DEKI_NODE_CHILDREN("Fsm/Actions")
    DEKI_NODE_TITLE_PROPERTY("name")
public:
    DEKI_EXPORT std::string name = "State";

    // Event names this state listens for, one output pin each. A state with no
    // transitions is terminal (the machine parks there). An event that fires
    // with no matching entry is ignored; a matching entry whose pin is unwired
    // is a graph error (no fallback).
    DEKI_EXPORT std::vector<std::string> transitions = { "FINISHED" };
};

#include "generated/FsmStartNode.gen.h"
#include "generated/FsmUpdateNode.gen.h"
#include "generated/FsmStateNode.gen.h"
