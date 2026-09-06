#pragma once

#include <string>
#include <vector>

#include "deki-nodegraph/DekiNode.h"

// Node vocabulary for the state-machine graph ("Fsm" domain).
//
// The graph mirrors a script's lifecycle with PARALLEL TRACKS: Awake, Start
// and Update are pure ENTRY nodes — lifecycle launch points, exactly like the
// Awake()/Start()/Update() hooks of a Deki::Behaviour — and each one's wired
// output begins its own track: an independent state flow with its own active
// state, all running alongside each other on one FsmComponent. ALL actions
// live in States (an entry node has no behavior of its own); a track's active
// state runs its action flow every frame, so a flow parked in a terminal
// state is per-frame code that runs forever. Custom events broadcast to every
// track (each track's active state decides via its transitions); FINISHED is
// raised and matched per-track.
//
// TWO LEVELS. The root canvas holds the flow: states, groups and the wires
// between them. Double-click a State to descend into its ACTION FLOW (the
// actions are nodes there, wired one to the next), and double-click a Group to
// descend into the states it contains. The breadcrumb walks back out. Nothing
// is hidden in a list: if it runs, it is a node on some canvas.

// The three entries are PERMANENT: exactly like the hooks of a Deki::Behaviour,
// they are always part of every FSM graph — seeded on creation, restored on
// open, absent from the add-node menu, not deletable. An unwired output is
// simply an unused hook, the same as a lifecycle method you didn't override.

// Main flow entry, mirroring Start(). Wire its output to the first state of
// the machine's main flow.
struct FsmStartNode
{
    DEKI_NODE(FsmStartNode, "FsmStart", "Fsm/Flow")
    static constexpr const char* StaticNodeDisplayName = "Start";
    static constexpr const char* StaticNodeDescription = "Main flow entry, like Start(). Wire it to the machine's first state.";
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
    static constexpr const char* StaticNodeDescription = "Setup flow entry, like Awake(). Its track runs before Start and Update.";
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
    static constexpr const char* StaticNodeDescription = "Per-frame flow entry, like Update(). Where watcher states live.";
    DEKI_NODE_OUTPUTS("start")
    DEKI_NODE_PERMANENT()
};

// One state: a named node holding an ACTION FLOW (its own inner graph, opened
// by double-clicking it) plus one output pin per transition event. The flow
// starts at the state's Entry node and follows the wires, one action at a
// time; FINISHED fires when it runs off the end. The canvas shows the state's
// `name` as its title.
struct FsmStateNode
{
    DEKI_NODE(FsmStateNode, "FsmState", "Fsm/Flow")
    static constexpr const char* StaticNodeDisplayName = "State";
    static constexpr const char* StaticNodeDescription = "One state. Holds its actions inside, and one output per transition event.";
    DEKI_NODE_INPUTS("in")
    DEKI_NODE_DYNAMIC_OUTPUTS("transitions")
    DEKI_NODE_SUBGRAPH("Fsm/Actions", "FsmActionEntry")
    DEKI_NODE_TITLE_PROPERTY("name")
public:
    DEKI_EXPORT std::string name = "State";

    // Event names this state listens for, one output pin each. A state with no
    // transitions is terminal: its track parks there — on the action that
    // never finishes, or idle once the flow has run out. An event that fires
    // with no matching entry is ignored; a matching entry whose pin is unwired
    // is a graph error (no fallback).
    DEKI_EXPORT std::vector<std::string> transitions = { "FINISHED" };
};

// Where a state's action flow begins. Seeded inside every state (it is that
// node type's declared subgraph entry), so descending into a fresh state shows
// an Entry waiting to be wired to the first action. Permanent: never in the
// add menu, never deletable. An Entry with nothing wired to it is a state that
// does nothing and finishes immediately, which is a legitimate thing to be.
struct FsmActionEntryNode
{
    DEKI_NODE(FsmActionEntryNode, "FsmActionEntry", "Fsm/Actions")
    static constexpr const char* StaticNodeDisplayName = "Entry";
    static constexpr const char* StaticNodeDescription = "Where a state's action flow starts.";
    DEKI_NODE_OUTPUTS("run")
    DEKI_NODE_PERMANENT()
};

// ---------------------------------------------------------------------------
// Groups
// ---------------------------------------------------------------------------
// A group is a state-shaped box holding a whole sub-flow: it takes one input
// like a state and has one output pin per EXIT, and double-clicking it
// descends into the states inside. Purely organizational — a group runs no
// actions of its own and costs nothing at runtime; entering one immediately
// continues to whatever its Group In node points at, and reaching an Exit node
// inside continues from the matching pin OUTSIDE. That is the whole contract,
// so "collapse this part of the machine" never changes what the machine does.
//
// Groups nest: a group's contents are ordinary Fsm/Flow nodes, groups included.
struct FsmGroupNode
{
    DEKI_NODE(FsmGroupNode, "FsmGroup", "Fsm/Flow")
    static constexpr const char* StaticNodeDisplayName = "Group";
    static constexpr const char* StaticNodeDescription = "A box holding a sub-flow of states. Tidies the canvas, changes nothing at runtime.";
    DEKI_NODE_INPUTS("in")
    DEKI_NODE_DYNAMIC_OUTPUTS("exits")
    DEKI_NODE_SUBGRAPH("Fsm/Flow", "FsmGroupIn")
    DEKI_NODE_TITLE_PROPERTY("name")
public:
    DEKI_EXPORT std::string name = "Group";

    // One output pin per exit, matched BY NAME to the Exit nodes inside. A
    // group with no exits is a one-way door: the flow enters and never leaves
    // (fine for a terminal branch of the machine).
    DEKI_EXPORT std::vector<std::string> exits = { "out" };
};

// The inside of a group's "in" pin: wire it to the first state of the group.
// Seeded in every group, permanent, exactly like a state's Entry.
struct FsmGroupInNode
{
    DEKI_NODE(FsmGroupInNode, "FsmGroupIn", "Fsm/Flow")
    static constexpr const char* StaticNodeDisplayName = "Group In";
    static constexpr const char* StaticNodeDescription = "The inside of a group's input: wire it to the group's first state.";
    DEKI_NODE_OUTPUTS("in")
    DEKI_NODE_PERMANENT()
};

// The inside of one of a group's output pins: wire a state's transition to it
// and the flow leaves the group through the pin with the same `name`. Add one
// per exit you declared on the group. A name matching no pin on the enclosing
// group, or an Exit sitting at the graph root where there is no group to leave,
// is a graph error at runtime (no fallback).
struct FsmGroupExitNode
{
    DEKI_NODE(FsmGroupExitNode, "FsmGroupExit", "Fsm/Flow")
    static constexpr const char* StaticNodeDisplayName = "Group Exit";
    static constexpr const char* StaticNodeDescription = "Leaves the group through the output pin with this name.";
    DEKI_NODE_INPUTS("in")
    DEKI_NODE_TITLE_PROPERTY("name")
public:
    DEKI_EXPORT std::string name = "out";
};

// ---------------------------------------------------------------------------
// Variables
// ---------------------------------------------------------------------------
// One permanent node holds the graph's variables as a child stack, authored in
// its inspector. Any PropertyRef can point at a variable (component "Variable",
// field = the name), so Set / Compare / Modify / Random / Tween Property all
// work on them with no variable-specific actions: a score is Modify Property on
// a variable, and "is the score 10?" is Compare Property on the same one.
//
// Variables belong to the whole document, so they are reachable from inside
// every state's action flow and every group, not redeclared per level.
//
// Values here are the INITIAL values. Each FsmComponent gets its own live copy,
// so two objects running the same graph do not share state.
struct FsmVariablesNode
{
    DEKI_NODE(FsmVariablesNode, "FsmVariables", "Fsm/Flow")
    static constexpr const char* StaticNodeDisplayName = "Variables";
    static constexpr const char* StaticNodeDescription = "The graph's variables and their starting values.";
    DEKI_NODE_CHILDREN("Fsm/Variables")
    DEKI_NODE_VARIABLES()
    DEKI_NODE_PERMANENT()
};

// The variable declarations. Convention (see DEKI_NODE_VARIABLES): the title
// property is the NAME, the other exported property is the initial VALUE and its
// type is the variable's type.
struct FsmNumberVariable
{
    DEKI_NODE(FsmNumberVariable, "FsmNumberVar", "Fsm/Variables")
    static constexpr const char* StaticNodeDisplayName = "Number";
    static constexpr const char* StaticNodeDescription = "A number variable.";
    DEKI_NODE_TITLE_PROPERTY("name")
public:
    DEKI_EXPORT std::string name = "number";
    DEKI_EXPORT float value = 0.0f;
};

struct FsmBoolVariable
{
    DEKI_NODE(FsmBoolVariable, "FsmBoolVar", "Fsm/Variables")
    static constexpr const char* StaticNodeDisplayName = "Bool";
    static constexpr const char* StaticNodeDescription = "A true or false variable.";
    DEKI_NODE_TITLE_PROPERTY("name")
public:
    DEKI_EXPORT std::string name = "flag";
    DEKI_EXPORT bool value = false;
};

struct FsmTextVariable
{
    DEKI_NODE(FsmTextVariable, "FsmTextVar", "Fsm/Variables")
    static constexpr const char* StaticNodeDisplayName = "Text";
    static constexpr const char* StaticNodeDescription = "A text variable.";
    DEKI_NODE_TITLE_PROPERTY("name")
public:
    DEKI_EXPORT std::string name = "text";
    DEKI_EXPORT std::string value;
};

#include "generated/FsmStartNode.gen.h"
#include "generated/FsmVariablesNode.gen.h"
#include "generated/FsmNumberVariable.gen.h"
#include "generated/FsmBoolVariable.gen.h"
#include "generated/FsmTextVariable.gen.h"
#include "generated/FsmAwakeNode.gen.h"
#include "generated/FsmUpdateNode.gen.h"
#include "generated/FsmStateNode.gen.h"
#include "generated/FsmActionEntryNode.gen.h"
#include "generated/FsmGroupNode.gen.h"
#include "generated/FsmGroupInNode.gen.h"
#include "generated/FsmGroupExitNode.gen.h"
