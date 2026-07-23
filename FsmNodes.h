#pragma once

#include <string>
#include <vector>

#include "reflection/DekiNode.h"

// Node vocabulary for the state-machine graph ("Fsm" domain).
//
// The graph mirrors a script's lifecycle with PARALLEL TRACKS: Awake, Start
// and Update are pure ENTRY nodes — lifecycle launch points, exactly like the
// Awake()/Start()/Update() hooks of a DekiBehaviour — and each one's wired
// output begins its own track: an independent state flow with its own active
// state, all running alongside each other on one FsmComponent. ALL actions
// live in States (an entry node has no behavior of its own); a track's active
// state runs its action stack every frame, so a flow parked in a terminal
// state is per-frame code that runs forever. Custom events broadcast to every
// track (each track's active state decides via its transitions); FINISHED is
// raised and matched per-track.

// The three entries are PERMANENT: exactly like the hooks of a DekiBehaviour,
// they are always part of every FSM graph — seeded on creation, restored on
// open, absent from the add-node menu, not deletable. An unwired output is
// simply an unused hook, the same as a lifecycle method you didn't override.

// Main flow entry, mirroring Start(). Wire its output to the first state of
// the machine's main flow.
struct FsmStartNode
{
    DEKI_NODE(FsmStartNode, "FsmStart", "Fsm/Flow")
    static constexpr const char* StaticNodeDisplayName = "Start";
    DEKI_NODE_OUTPUTS("start")
    DEKI_NODE_PERMANENT()
};

// Setup flow entry, mirroring Awake(). Its track enters its first state
// BEFORE the Start and Update tracks (the lifecycle ordering), so put
// birth-time setup states here: a "Setup" state full of Set Property actions
// that ends terminal, or one that FINISHED-chains onward.
struct FsmAwakeNode
{
    DEKI_NODE(FsmAwakeNode, "FsmAwake", "Fsm/Flow")
    static constexpr const char* StaticNodeDisplayName = "Awake";
    DEKI_NODE_OUTPUTS("start")
    DEKI_NODE_PERMANENT()
};

// Per-frame flow entry, mirroring Update(). Its track starts at
// initialization and runs alongside the main flow — the natural home for
// watcher states: a terminal "Watch" state whose Compare Property / Watch
// Button actions run every frame forever, raising events that can transition
// EVERY track.
struct FsmUpdateNode
{
    DEKI_NODE(FsmUpdateNode, "FsmUpdate", "Fsm/Flow")
    static constexpr const char* StaticNodeDisplayName = "Update";
    DEKI_NODE_OUTPUTS("start")
    DEKI_NODE_PERMANENT()
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
    // transitions is terminal (its track parks there, running its actions
    // every frame). An event that fires with no matching entry is ignored; a
    // matching entry whose pin is unwired is a graph error (no fallback).
    DEKI_EXPORT std::vector<std::string> transitions = { "FINISHED" };
};

#include "generated/FsmStartNode.gen.h"
#include "generated/FsmAwakeNode.gen.h"
#include "generated/FsmUpdateNode.gen.h"
#include "generated/FsmStateNode.gen.h"
