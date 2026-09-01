#pragma once

#include <string>

#include "deki-nodegraph/DekiNode.h"
#include "reflection/PropertyRef.h"   // PropertyRef (picked component field)
#include "assets/AssetRef.h"          // AssetRef<Scene> (Spawn Scene)
#include "Scene.h"                   // Scene::AssetTypeName
#include "deki-tween/Easing.h"        // Deki::EaseType (Tween Property easing)

// The action library: the data side. Each action is a plain reflected struct in
// category "Fsm/Actions" — FsmStateNode's SUBGRAPH category, so these are
// authored on the canvas INSIDE a state (double-click a state to descend into
// its action flow), never at the graph root. Runtime behavior is registered
// separately in FsmActionLibrary.cpp via REGISTER_FSM_ACTION; project DLLs add
// game-specific actions the same way.
//
// PINS. Every action has one input ("in") and one or more outputs. A state's
// flow starts at its Entry node and follows the wires: when an action finishes
// it hands control to whatever its finishing pin is wired to. Most actions have
// a single "done" pin; a BRANCHING action has one pin per outcome (Compare
// Property: "true" and "false"), which is how a graph makes a decision without
// inventing event names for it. Reaching an unwired pin ends the flow and
// raises FINISHED for that state.
//
// The flow is a graph, not a list: wiring an action back to an earlier one is a
// legal loop, and re-entering an action resets its runtime state exactly as if
// it had been entered for the first time.
//
// An action that never finishes (Watch Button, anything with everyFrame on)
// parks the flow on itself, so nothing downstream of it runs. That is how a
// per-frame watcher is expressed: park on it and let it raise events.
//
// EVENTS. Actions do not carry event names. Send Event is the one action that
// raises one, so "what raises this event" is always answerable by looking for
// Send Event nodes. Branch with pins; raise an event when you want to leave the
// state (only an event moves a track from one state to another).
//
// Object targeting convention: `targetObject` empty = the FSM's owner object,
// else the name of an object in the same scene. A name that resolves to
// nothing is a graph error at runtime (no fallback). DEKI_OBJECT_NAME gives
// those fields the editor's object picker (browse the open scene's hierarchy
// instead of typing the name); the stored value stays the plain name, so one
// graph still drives every scene that uses the same object names.

// Comparison operator for Compare Property.
enum class FsmCompareOp : uint8_t
{
    Equals = 0,
    NotEquals,
    Less,
    Greater,
};

// Arithmetic for Modify Property. Divide by zero fails the machine.
enum class FsmMathOp : uint8_t
{
    Add = 0,
    Subtract,
    Multiply,
    Divide,
    Min,
    Max,
};

// Do nothing for a fixed number of seconds, then continue. The classic timed
// step: Wait 2s wired onward is "two seconds later, ...".
struct FsmWaitAction
{
    DEKI_NODE(FsmWaitAction, "FsmWait", "Fsm/Actions")
    static constexpr const char* StaticNodeDisplayName = "Wait";
    static constexpr const char* StaticNodeDescription = "Do nothing for a set number of seconds, then continue.";
    DEKI_NODE_INPUTS("in")
    DEKI_NODE_OUTPUTS("done")
public:
    DEKI_EXPORT float seconds = 1.0f;
};

// Raise an event on this FSM (optionally after a delay), then continue. The
// event is matched against the ACTIVE state's transitions when processed, so
// this is how a state's action flow ends up leaving the state.
struct FsmSendEventAction
{
    DEKI_NODE(FsmSendEventAction, "FsmSendEvent", "Fsm/Actions")
    static constexpr const char* StaticNodeDisplayName = "Send Event";
    static constexpr const char* StaticNodeDescription = "Raise an event on this FSM, optionally after a delay.";
    DEKI_NODE_INPUTS("in")
    DEKI_NODE_OUTPUTS("done")
public:
    DEKI_EXPORT std::string eventName = "EVENT";
    DEKI_EXPORT float delaySec = 0.0f;
};

// Write any DEKI_EXPORT field of any component. `target` is picked in the
// inspector (object -> component -> field) and resolved ONCE when the action
// starts, so the per-frame cost is a memcpy; `value` is authored typed to
// whatever the reference points at. everyFrame re-applies each frame and never
// finishes, which parks the flow — nothing wired after it will run.
struct FsmSetPropertyAction
{
    DEKI_NODE(FsmSetPropertyAction, "FsmSetProperty", "Fsm/Actions")
    static constexpr const char* StaticNodeDisplayName = "Set Property";
    static constexpr const char* StaticNodeDescription = "Write a value into any component field or variable.";
    DEKI_NODE_INPUTS("in")
    DEKI_NODE_OUTPUTS("done")
public:
    DEKI_EXPORT PropertyRef target;
    DEKI_EXPORT DEKI_VALUE_OF(target) std::string value;
    DEKI_EXPORT bool everyFrame = false;
};

// The "if" of the graph: read the picked field, compare it, and continue down
// the TRUE or the FALSE pin. Resolved once when the action starts, like Set
// Property.
//
// waitUntilTrue turns it from a branch into a gate: the action does not finish
// while the comparison is false (re-tested every frame), so the flow parks here
// until the world says yes and then continues down "true". The "false" pin is
// unreachable in that mode — wire it only in the default one-shot mode.
struct FsmComparePropertyAction
{
    DEKI_NODE(FsmComparePropertyAction, "FsmCompareProperty", "Fsm/Actions")
    static constexpr const char* StaticNodeDisplayName = "Compare Property";
    static constexpr const char* StaticNodeDescription = "The if: compare a field, then continue down true or false.";
    DEKI_NODE_INPUTS("in")
    DEKI_NODE_OUTPUTS("true", "false")
public:
    DEKI_EXPORT PropertyRef target;
    DEKI_EXPORT FsmCompareOp compare = FsmCompareOp::Equals;
    DEKI_EXPORT DEKI_VALUE_OF(target) std::string value;
    DEKI_EXPORT bool waitUntilTrue = false;
};

// Ease ANY numeric field from its current value to `to` over `duration`
// seconds, then continue. This is the generic form of "move to": point `target`
// at Transform / Position for a move, Transform / Rotation to spin, Transform /
// Scale to grow, or any float or Vector2 field of any component to animate that
// instead. A Vector2 target (position, scale) moves both axes in this ONE
// action. relative = `to` is an offset from wherever the field was when the
// action started. Easing curves come from deki-tween.
struct FsmTweenPropertyAction
{
    DEKI_NODE(FsmTweenPropertyAction, "FsmTweenProperty", "Fsm/Actions")
    static constexpr const char* StaticNodeDisplayName = "Tween Property";
    static constexpr const char* StaticNodeDescription = "Ease a numeric field to a new value over time.";
    DEKI_NODE_INPUTS("in")
    DEKI_NODE_OUTPUTS("done")
public:
    DEKI_EXPORT PropertyRef target;
    DEKI_EXPORT DEKI_VALUE_OF(target) std::string to;
    DEKI_EXPORT float duration = 1.0f;
    DEKI_EXPORT Deki::EaseType ease = Deki::EaseType::Linear;
    DEKI_EXPORT bool relative = false;
};

// Arithmetic on any numeric field or variable: target = target op operand.
// The counter/score/health workhorse — "Score += 10" is this action pointed at a
// graph variable, "Health -= 1" the same pointed at a component field. Finishes
// immediately unless everyFrame is on (which parks the flow).
struct FsmModifyPropertyAction
{
    DEKI_NODE(FsmModifyPropertyAction, "FsmModifyProperty", "Fsm/Actions")
    static constexpr const char* StaticNodeDisplayName = "Modify Property";
    static constexpr const char* StaticNodeDescription = "Do arithmetic on a number field or variable. The score and health workhorse.";
    DEKI_NODE_INPUTS("in")
    DEKI_NODE_OUTPUTS("done")
public:
    DEKI_EXPORT PropertyRef target;
    DEKI_EXPORT FsmMathOp operation = FsmMathOp::Add;
    DEKI_EXPORT DEKI_VALUE_OF(target) std::string operand;
    DEKI_EXPORT bool everyFrame = false;
};

// Write a random number into any numeric field or variable. `wholeNumbers`
// rounds to an integer, so "pick a room 1..5" and "jitter a position by 0.1m"
// are the same action.
struct FsmRandomPropertyAction
{
    DEKI_NODE(FsmRandomPropertyAction, "FsmRandomProperty", "Fsm/Actions")
    static constexpr const char* StaticNodeDisplayName = "Random Property";
    static constexpr const char* StaticNodeDescription = "Write a random number into a number field or variable.";
    DEKI_NODE_INPUTS("in")
    DEKI_NODE_OUTPUTS("done")
public:
    DEKI_EXPORT PropertyRef target;
    DEKI_EXPORT float min = 0.0f;
    DEKI_EXPORT float max = 1.0f;
    DEKI_EXPORT bool wholeNumbers = false;
};

// Instantiate a scene into the running scene, at (x, y) meters — relative to
// the spawner's position when `relative` is on. `spawnedName` renames the new
// root so later actions (and other states) can find it by name; empty keeps the
// scene's own name.
//
// NOTE: the graph window has no asset picker yet, so `scene` is authored as a
// GUID string there (the value round-trips and loads correctly either way).
struct FsmSpawnSceneAction
{
    DEKI_NODE(FsmSpawnSceneAction, "FsmSpawnScene", "Fsm/Actions")
    static constexpr const char* StaticNodeDisplayName = "Spawn Scene";
    static constexpr const char* StaticNodeDescription = "Instantiate a scene into the running scene.";
    DEKI_NODE_INPUTS("in")
    DEKI_NODE_OUTPUTS("done")
public:
    DEKI_EXPORT Deki::AssetRef<Scene> scene;
    DEKI_EXPORT DEKI_UNIT(Distance) float x = 0.0f;
    DEKI_EXPORT DEKI_UNIT(Distance) float y = 0.0f;
    DEKI_EXPORT bool relative = false;
    DEKI_EXPORT std::string spawnedName;
};

// Remove an object (and its children) from the running scene. An empty
// targetObject destroys the object this FSM is on, which also stops the machine
// — put it at the end of a flow.
struct FsmDestroyObjectAction
{
    DEKI_NODE(FsmDestroyObjectAction, "FsmDestroyObject", "Fsm/Actions")
    static constexpr const char* StaticNodeDisplayName = "Destroy Object";
    static constexpr const char* StaticNodeDescription = "Remove an object and its children from the scene.";
    DEKI_NODE_INPUTS("in")
    DEKI_NODE_OUTPUTS("done")
public:
    DEKI_EXPORT DEKI_OBJECT_NAME() std::string targetObject;
};

// Reparent an object. An empty newParent moves it to the scene root, which is
// how you detach a picked-up item from the hand that carried it.
struct FsmSetParentAction
{
    DEKI_NODE(FsmSetParentAction, "FsmSetParent", "Fsm/Actions")
    static constexpr const char* StaticNodeDisplayName = "Set Parent";
    static constexpr const char* StaticNodeDescription = "Move an object under a new parent, or out to the root.";
    DEKI_NODE_INPUTS("in")
    DEKI_NODE_OUTPUTS("done")
public:
    DEKI_EXPORT DEKI_OBJECT_NAME() std::string targetObject;
    DEKI_EXPORT DEKI_OBJECT_NAME() std::string newParent;
};

// Drive the target's AnimationComponent: pick a sequence and play it. When
// `waitForFinish` is on the action finishes with the animation (so the flow
// continues after it), otherwise it finishes immediately and the animation
// keeps running on its own. `loop` off plays once.
struct FsmPlayAnimationAction
{
    DEKI_NODE(FsmPlayAnimationAction, "FsmPlayAnimation", "Fsm/Actions")
    static constexpr const char* StaticNodeDisplayName = "Play Animation";
    static constexpr const char* StaticNodeDescription = "Play an animation on the target, optionally waiting for it to finish.";
    DEKI_NODE_INPUTS("in")
    DEKI_NODE_OUTPUTS("done")
public:
    DEKI_EXPORT DEKI_OBJECT_NAME(AnimationComponent) std::string targetObject;
    DEKI_EXPORT int32_t sequence = 0;
    DEKI_EXPORT bool loop = true;
    DEKI_EXPORT bool waitForFinish = false;
};

// Raise an event on ANOTHER object's FsmComponent (an empty targetObject means
// this one). The machine-to-machine wire: a door's FSM telling the room's FSM
// that it opened, without either knowing the other's states.
struct FsmSendEventToAction
{
    DEKI_NODE(FsmSendEventToAction, "FsmSendEventTo", "Fsm/Actions")
    static constexpr const char* StaticNodeDisplayName = "Send Event To";
    static constexpr const char* StaticNodeDescription = "Raise an event on another object's FSM.";
    DEKI_NODE_INPUTS("in")
    DEKI_NODE_OUTPUTS("done")
public:
    DEKI_EXPORT DEKI_OBJECT_NAME() std::string targetObject;
    DEKI_EXPORT std::string eventName = "EVENT";
};

// Write a line to the console. The print-debugging of graphs: what ran, when.
struct FsmLogAction
{
    DEKI_NODE(FsmLogAction, "FsmLog", "Fsm/Actions")
    static constexpr const char* StaticNodeDisplayName = "Log";
    static constexpr const char* StaticNodeDescription = "Write a line to the console.";
    DEKI_NODE_INPUTS("in")
    DEKI_NODE_OUTPUTS("done")
public:
    DEKI_EXPORT std::string message;
};

// Park here until the target's ButtonComponent is clicked, then continue down
// "clicked". Never finishes otherwise, so it keeps watching for as long as its
// state is active (clicks while another state is active are dropped) and
// nothing downstream runs until one lands.
//
// This is the input-to-transition bridge: wire "clicked" to a Send Event action
// and a button press becomes a state transition. Give each watched button its
// own state (its own track wired from Update) rather than chaining watchers,
// since a parked flow only ever watches one.
struct FsmWatchButtonAction
{
    DEKI_NODE(FsmWatchButtonAction, "FsmWatchButton", "Fsm/Actions")
    static constexpr const char* StaticNodeDisplayName = "Watch Button";
    static constexpr const char* StaticNodeDescription = "Wait here until the target button is clicked.";
    DEKI_NODE_INPUTS("in")
    DEKI_NODE_OUTPUTS("clicked")
public:
    DEKI_EXPORT DEKI_OBJECT_NAME(ButtonComponent) std::string buttonObject;
};

#include "generated/FsmWaitAction.gen.h"
#include "generated/FsmSendEventAction.gen.h"
#include "generated/FsmSetPropertyAction.gen.h"
#include "generated/FsmComparePropertyAction.gen.h"
#include "generated/FsmModifyPropertyAction.gen.h"
#include "generated/FsmRandomPropertyAction.gen.h"
#include "generated/FsmTweenPropertyAction.gen.h"
#include "generated/FsmSpawnSceneAction.gen.h"
#include "generated/FsmDestroyObjectAction.gen.h"
#include "generated/FsmSetParentAction.gen.h"
#include "generated/FsmPlayAnimationAction.gen.h"
#include "generated/FsmSendEventToAction.gen.h"
#include "generated/FsmLogAction.gen.h"
#include "generated/FsmWatchButtonAction.gen.h"
