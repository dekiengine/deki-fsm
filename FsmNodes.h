#pragma once

#include <string>
#include <vector>

#include "reflection/DekiNode.h"

// Node vocabulary for the state-machine graph ("Fsm" domain).
//
// A graph mirrors a script's lifecycle with PARALLEL TRACKS, because a
// DekiBehaviour can do independent things in Awake/Start/Update and the FSM
// keeps that power: every WIRED lifecycle output (Start "start", Awake
// "done", Update "flow") begins its own track — an independent state flow
// with its own active state, all running alongside each other on one
// FsmComponent. Custom events broadcast to every track (each track's active
// state decides via its transitions); FINISHED is raised and matched
// per-track. Within one track exactly one state is active; its action stack
// runs every frame; transitions are wires labeled with event names.

// Conventional main flow entry. Its single output MUST be wired to the first
// state. Optional since tracks can also begin at Awake/Update outputs — a
// graph that is only an Update stack (a pure per-frame behaviour) is legal.
struct FsmStartNode
{
    DEKI_NODE(FsmStartNode, "FsmStart", "Fsm/Flow")
    static constexpr const char* StaticNodeDisplayName = "Start";
    DEKI_NODE_OUTPUTS("start")
};

// One-shot setup: the FSM's Awake() lifecycle. Its enabled actions run
// exactly ONCE, in a single pass, when the machine initializes — before the
// Update stacks arm and before any track enters its first state. Because the
// pass is instantaneous, every action in it must finish immediately;
// frame-based actions (Wait, Move To, Watch Button, everyFrame) are a graph
// error here. The optional "done" output begins a parallel track right after
// setup. Events queued by Awake actions are processed as soon as tracks are
// live, so Awake can also redirect flows on frame one.
struct FsmAwakeNode
{
    DEKI_NODE(FsmAwakeNode, "FsmAwake", "Fsm/Flow")
    static constexpr const char* StaticNodeDisplayName = "Awake";
    DEKI_NODE_OUTPUTS("done")
    DEKI_NODE_CHILDREN("Fsm/Actions")
};

// Always-on stack: the FSM's Update() lifecycle. Its enabled actions run
// every frame for the life of the machine, regardless of any track's state —
// the home for global watchers (a Compare Property or Watch Button here
// raises events that can transition EVERY track). Finished actions simply
// stop; there is no FINISHED from this stack. The optional "flow" output
// begins a parallel track at initialization — a state flow running next to
// the main Start flow. A graph may hold any number of Update nodes.
struct FsmUpdateNode
{
    DEKI_NODE(FsmUpdateNode, "FsmUpdate", "Fsm/Flow")
    static constexpr const char* StaticNodeDisplayName = "Update";
    DEKI_NODE_OUTPUTS("flow")
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
    // transitions is terminal (its track parks there). An event that fires
    // with no matching entry is ignored; a matching entry whose pin is unwired
    // is a graph error (no fallback).
    DEKI_EXPORT std::vector<std::string> transitions = { "FINISHED" };
};

#include "generated/FsmStartNode.gen.h"
#include "generated/FsmAwakeNode.gen.h"
#include "generated/FsmUpdateNode.gen.h"
#include "generated/FsmStateNode.gen.h"
