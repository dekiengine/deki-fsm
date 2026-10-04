#pragma once

#include <string>
#include <vector>

#include "deki-nodegraph/DekiNode.h"

namespace DekiFsm
{

// Node vocabulary for the state-machine graph ("Fsm" domain).
//
// The graph follows a script's lifecycle with parallel tracks. Awake, Start
// and Update are entry nodes, like the Awake()/Start()/Update() hooks of a
// Deki::Component, and each one's wired output starts its own track: an
// independent state flow with its own active state, all running side by side
// on one FsmComponent. All actions live in States; an entry node does nothing
// itself. A track's active state runs its action flow every frame, so a flow
// stopped in a terminal state is per-frame code that runs forever. Custom
// events go to every track (each track's active state decides through its
// transitions); FINISHED is raised and matched per track.
//
// Two levels. The root canvas holds the flow: states, groups and the wires
// between them. Double-click a State to open its action flow (the actions are
// nodes there, wired one to the next), and double-click a Group to open the
// states it holds. The breadcrumb leads back out. Nothing is hidden in a list:
// if it runs, it is a node on some canvas.

// The three entries are permanent, like the hooks of a Deki::Component: every
// FSM graph has them. They are added on creation, restored on open, missing
// from the add-node menu and cannot be deleted. An unwired output is an unused
// hook, like a lifecycle method you did not override.

/// Main flow entry, like Start(). Wire its output to the first state of the
/// machine's main flow.
struct FsmStartNode
{
    DEKI_NODE(FsmStartNode, "FsmStart", "Fsm/Flow")
    static constexpr const char* kStaticNodeDisplayName = "Start";
    static constexpr const char* kStaticNodeDescription =
        "Main flow entry, like Start(). Wire it to the machine's first state.";
    DEKI_NODE_OUTPUTS("start")
    DEKI_NODE_PERMANENT()
};

/// Setup flow entry, like Awake(). Its track enters its first state before the
/// Start and Update tracks, as in the lifecycle, so put setup states here: a
/// "Setup" state full of Set Property actions that ends there, or one that
/// moves on through FINISHED.
struct FsmAwakeNode
{
    DEKI_NODE(FsmAwakeNode, "FsmAwake", "Fsm/Flow")
    static constexpr const char* kStaticNodeDisplayName = "Awake";
    static constexpr const char* kStaticNodeDescription =
        "Setup flow entry, like Awake(). Its track runs before Start and Update.";
    DEKI_NODE_OUTPUTS("start")
    DEKI_NODE_PERMANENT()
};

/// Per-frame flow entry, like Update(). Its track starts at initialization and
/// runs beside the main flow. It is the place for watcher states: a terminal
/// "Watch" state whose Compare Property or Watch Button actions run every frame
/// forever, raising events that can move every track.
struct FsmUpdateNode
{
    DEKI_NODE(FsmUpdateNode, "FsmUpdate", "Fsm/Flow")
    static constexpr const char* kStaticNodeDisplayName = "Update";
    static constexpr const char* kStaticNodeDescription =
        "Per-frame flow entry, like Update(). Where watcher states live.";
    DEKI_NODE_OUTPUTS("start")
    DEKI_NODE_PERMANENT()
};

/// One state: a named node holding an action flow (its own inner graph, opened
/// by double-clicking it) and one output pin per transition event. The flow
/// starts at the state's Entry node and follows the wires, one action at a
/// time; FINISHED fires when it runs off the end. The canvas shows the state's
/// `name` as its title.
struct FsmStateNode
{
    DEKI_NODE(FsmStateNode, "FsmState", "Fsm/Flow")
    static constexpr const char* kStaticNodeDisplayName = "State";
    static constexpr const char* kStaticNodeDescription =
        "One state. Holds its actions inside, and one output per transition event.";
    DEKI_NODE_INPUTS("in")
    DEKI_NODE_DYNAMIC_OUTPUTS("transitions")
    DEKI_NODE_SUBGRAPH("Fsm/Actions", "FsmActionEntry")
    DEKI_NODE_TITLE_PROPERTY("name")
public:
    DEKI_EXPORT std::string name = "State";

    // Event names this state listens for, one output pin each. A state with no
    // transitions is terminal: its track stays there, on the action that never
    // finishes, or idle once the flow has run out. An event with no matching
    // entry is ignored; a matching entry whose pin is unwired is a graph error
    // that stops the machine.
    DEKI_EXPORT std::vector<std::string> transitions = { "FINISHED" };
};

/// Where a state's action flow begins. Every state has one (it is the State
/// node's declared subgraph entry), so opening a new state shows an Entry
/// ready to be wired to the first action. Permanent: never in the add menu,
/// cannot be deleted. An Entry with nothing wired to it makes a state that
/// does nothing and finishes at once, which is allowed.
struct FsmActionEntryNode
{
    DEKI_NODE(FsmActionEntryNode, "FsmActionEntry", "Fsm/Actions")
    static constexpr const char* kStaticNodeDisplayName = "Entry";
    static constexpr const char* kStaticNodeDescription = "Where a state's action flow starts.";
    DEKI_NODE_OUTPUTS("run")
    DEKI_NODE_PERMANENT()
};

// ---------------------------------------------------------------------------
// Groups
// ---------------------------------------------------------------------------
// A group is a state-shaped box holding a whole sub-flow: it takes one input
// like a state, has one output pin per exit, and double-clicking it opens the
// states inside. It only organizes: a group runs no actions and costs nothing
// at runtime. Entering one continues at once to whatever its Group In node
// points at, and reaching an Exit node inside continues from the matching pin
// outside. So grouping part of the machine never changes what it does.
//
// Groups nest: a group's contents are ordinary Fsm/Flow nodes, groups included.
struct FsmGroupNode
{
    DEKI_NODE(FsmGroupNode, "FsmGroup", "Fsm/Flow")
    static constexpr const char* kStaticNodeDisplayName = "Group";
    static constexpr const char* kStaticNodeDescription =
        "A box holding a sub-flow of states. Tidies the canvas, changes nothing at runtime.";
    DEKI_NODE_INPUTS("in")
    DEKI_NODE_DYNAMIC_OUTPUTS("exits")
    DEKI_NODE_SUBGRAPH("Fsm/Flow", "FsmGroupIn")
    DEKI_NODE_TITLE_PROPERTY("name")
public:
    DEKI_EXPORT std::string name = "Group";

    // One output pin per exit, matched by name to the Exit nodes inside. A
    // group with no exits is a one-way door: the flow enters and never leaves,
    // which suits a terminal branch of the machine.
    DEKI_EXPORT std::vector<std::string> exits = { "out" };
};

/// The inside of a group's "in" pin: wire it to the group's first state.
/// Every group has one, and it is permanent, like a state's Entry.
struct FsmGroupInNode
{
    DEKI_NODE(FsmGroupInNode, "FsmGroupIn", "Fsm/Flow")
    static constexpr const char* kStaticNodeDisplayName = "Group In";
    static constexpr const char* kStaticNodeDescription =
        "The inside of a group's input: wire it to the group's first state.";
    DEKI_NODE_OUTPUTS("in")
    DEKI_NODE_PERMANENT()
};

/// The inside of one of a group's output pins: wire a state's transition to it
/// and the flow leaves the group through the pin with the same `name`. Add one
/// per exit declared on the group. A name matching no pin on the enclosing
/// group, or an Exit at the graph root with no group to leave, is a graph
/// error at runtime that stops the machine.
struct FsmGroupExitNode
{
    DEKI_NODE(FsmGroupExitNode, "FsmGroupExit", "Fsm/Flow")
    static constexpr const char* kStaticNodeDisplayName = "Group Exit";
    static constexpr const char* kStaticNodeDescription = "Leaves the group through the output pin with this name.";
    DEKI_NODE_INPUTS("in")
    DEKI_NODE_TITLE_PROPERTY("name")
public:
    DEKI_EXPORT std::string name = "out";
};

// ---------------------------------------------------------------------------
// Variables
// ---------------------------------------------------------------------------
// One permanent node holds the graph's variables as a child stack, edited in
// its inspector. Any PropertyRef can point at a variable (component
// "Variable", field = the name), so Set, Compare, Modify, Random and Tween
// Property all work on them without variable-specific actions: a score is
// Modify Property on a variable, and "is the score 10?" is Compare Property on
// the same one.
//
// Variables belong to the whole document, so every state's action flow and
// every group can reach them; they are not declared again per level.
//
// Values here are the initial values. Each FsmComponent gets its own live
// copy, so two objects running the same graph do not share state.
struct FsmVariablesNode
{
    DEKI_NODE(FsmVariablesNode, "FsmVariables", "Fsm/Flow")
    static constexpr const char* kStaticNodeDisplayName = "Variables";
    static constexpr const char* kStaticNodeDescription = "The graph's variables and their starting values.";
    DEKI_NODE_CHILDREN("Fsm/Variables")
    DEKI_NODE_VARIABLES()
    DEKI_NODE_PERMANENT()
};

// The variable declarations. As DEKI_NODE_VARIABLES expects, the title
// property is the name, and the other exported property is the initial value,
// whose type is the variable's type.
struct FsmNumberVariable
{
    DEKI_NODE(FsmNumberVariable, "FsmNumberVar", "Fsm/Variables")
    static constexpr const char* kStaticNodeDisplayName = "Number";
    static constexpr const char* kStaticNodeDescription = "A number variable.";
    DEKI_NODE_TITLE_PROPERTY("name")
public:
    DEKI_EXPORT std::string name = "number";
    DEKI_EXPORT float value = 0.0f;
};

struct FsmBoolVariable
{
    DEKI_NODE(FsmBoolVariable, "FsmBoolVar", "Fsm/Variables")
    static constexpr const char* kStaticNodeDisplayName = "Bool";
    static constexpr const char* kStaticNodeDescription = "A true or false variable.";
    DEKI_NODE_TITLE_PROPERTY("name")
public:
    DEKI_EXPORT std::string name = "flag";
    DEKI_EXPORT bool value = false;
};

struct FsmTextVariable
{
    DEKI_NODE(FsmTextVariable, "FsmTextVar", "Fsm/Variables")
    static constexpr const char* kStaticNodeDisplayName = "Text";
    static constexpr const char* kStaticNodeDescription = "A text variable.";
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

}  // namespace DekiFsm
