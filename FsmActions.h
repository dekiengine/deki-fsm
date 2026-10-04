#pragma once

#include <string>

#include "deki-nodegraph/DekiNode.h"
#include <deki/reflection/PropertyRef.h>  // PropertyRef (picked component field)
#include <deki/assets/AssetRef.h>         // AssetRef<Scene> (Spawn Scene)
#include <deki/Scene.h>                   // Scene::kAssetTypeName
#include "deki-tween/Easing.h"            // DekiTween::EaseType (Tween Property easing)

namespace DekiFsm
{

// The action library, data side. Each action is a plain reflected struct in
// category "Fsm/Actions", which is FsmStateNode's subgraph category, so these
// are placed on the canvas inside a state (double-click a state to open its
// action flow), never at the graph root. Runtime behavior is registered
// separately, by RegisterActionLibrary() in FsmActionLibrary.cpp; project DLLs
// add game-specific actions with REGISTER_FSM_ACTION.
//
// Pins. Every action has one input ("in") and one or more outputs. A state's
// flow starts at its Entry node and follows the wires: when an action finishes
// it hands control to whatever its finishing pin is wired to. Most actions
// have a single "done" pin; a branching action has one pin per outcome
// (Compare Property: "true" and "false"), so a graph can decide without
// inventing event names. Reaching an unwired pin ends the flow and raises
// FINISHED for that state.
//
// The flow is a graph, not a list: wiring an action back to an earlier one is
// a valid loop, and entering an action again resets its runtime state as on
// its first entry.
//
// An action that never finishes (Watch Button, anything with everyFrame on)
// keeps the flow on itself, so nothing after it runs. That is how to write a
// per-frame watcher: stop on it and let it raise events.
//
// Events. Actions do not carry event names. Send Event is the one action that
// raises one, so looking for Send Event nodes always shows what raises an
// event. Branch with pins; raise an event to leave the state (only an event
// moves a track from one state to another).
//
// Object targets: an empty `targetObject` means the FSM's owner object,
// anything else names an object in the same scene. A name that matches
// nothing is a graph error at runtime that stops the machine.
// DEKI_OBJECT_NAME gives those fields the editor's object picker (browse the
// open scene's hierarchy instead of typing the name); the stored value stays
// the plain name, so one graph still drives every scene that uses the same
// object names.

// Variables as parameters. A number an action takes (a Wait's seconds, a
// tween's duration and amount, a Set's value, a Modify's operand) can come
// from a graph variable instead: its "...Variable" field names a Number
// variable, and when set, that variable's value, read as the action starts,
// replaces the typed literal. So one graph serves many objects, each giving it
// different numbers through FsmComponent::variableOverrides. An unknown name
// stops the machine. Empty means the literal is used.

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

/// Does nothing for a set number of seconds, then continues. Wait 2s wired
/// onward means "two seconds later, ...".
struct FsmWaitAction
{
    DEKI_NODE(FsmWaitAction, "FsmWait", "Fsm/Actions")
    static constexpr const char* kStaticNodeDisplayName = "Wait";
    static constexpr const char* kStaticNodeDescription = "Do nothing for a set number of seconds, then continue.";
    DEKI_NODE_INPUTS("in")
    DEKI_NODE_OUTPUTS("done")
public:
    DEKI_EXPORT float seconds = 1.0f;
    DEKI_EXPORT std::string secondsVariable;
};

/// Raises an event on this FSM (optionally after a delay), then continues. The
/// event is matched against the active state's transitions when handled, so
/// this is how a state's action flow leaves the state.
struct FsmSendEventAction
{
    DEKI_NODE(FsmSendEventAction, "FsmSendEvent", "Fsm/Actions")
    static constexpr const char* kStaticNodeDisplayName = "Send Event";
    static constexpr const char* kStaticNodeDescription = "Raise an event on this FSM, optionally after a delay.";
    DEKI_NODE_INPUTS("in")
    DEKI_NODE_OUTPUTS("done")
public:
    DEKI_EXPORT std::string eventName = "EVENT";
    DEKI_EXPORT float delaySec = 0.0f;
};

/// Writes any DEKI_EXPORT field of any component. `target` is picked in the
/// inspector (object -> component -> field) and resolved once when the action
/// starts, so each frame costs only a memcpy; `value` is typed to match the
/// field. everyFrame writes again each frame and never finishes, which keeps
/// the flow here: nothing wired after it runs.
struct FsmSetPropertyAction
{
    DEKI_NODE(FsmSetPropertyAction, "FsmSetProperty", "Fsm/Actions")
    static constexpr const char* kStaticNodeDisplayName = "Set Property";
    static constexpr const char* kStaticNodeDescription = "Write a value into any component field or variable.";
    DEKI_NODE_INPUTS("in")
    DEKI_NODE_OUTPUTS("done")
public:
    DEKI_EXPORT Deki::PropertyRef target;
    DEKI_EXPORT DEKI_VALUE_OF(target) std::string value;
    DEKI_EXPORT std::string valueVariable;
    DEKI_EXPORT bool everyFrame = false;
};

/// The graph's "if": reads the picked field, compares it, and continues down
/// the true or the false pin. Resolved once when the action starts, like Set
/// Property.
///
/// waitUntilTrue makes it a gate instead of a branch: the action does not
/// finish while the comparison is false (tested again every frame), so the
/// flow waits here until it is true and then continues down "true". The
/// "false" pin is never taken in that mode; wire it only in the default
/// one-shot mode.
struct FsmComparePropertyAction
{
    DEKI_NODE(FsmComparePropertyAction, "FsmCompareProperty", "Fsm/Actions")
    static constexpr const char* kStaticNodeDisplayName = "Compare Property";
    static constexpr const char* kStaticNodeDescription = "The if: compare a field, then continue down true or false.";
    DEKI_NODE_INPUTS("in")
    DEKI_NODE_OUTPUTS("true", "false")
public:
    DEKI_EXPORT Deki::PropertyRef target;
    DEKI_EXPORT FsmCompareOp compare = FsmCompareOp::Equals;
    DEKI_EXPORT DEKI_VALUE_OF(target) std::string value;
    DEKI_EXPORT bool waitUntilTrue = false;
};

/// Eases any numeric field from its current value to `to` over `duration`
/// seconds, then continues. The general form of "move to": point `target` at
/// Transform / Position to move, Transform / Rotation to spin, Transform /
/// Scale to grow, or any float or Vector2 field of any component to animate
/// that. A Vector2 target (position, scale) moves both axes in this one action.
/// With `relative`, `to` is an offset from where the field was when the action
/// started. Easing curves come from deki-tween.
struct FsmTweenPropertyAction
{
    DEKI_NODE(FsmTweenPropertyAction, "FsmTweenProperty", "Fsm/Actions")
    static constexpr const char* kStaticNodeDisplayName = "Tween Property";
    static constexpr const char* kStaticNodeDescription = "Ease a numeric field to a new value over time.";
    DEKI_NODE_INPUTS("in")
    DEKI_NODE_OUTPUTS("done")
public:
    DEKI_EXPORT Deki::PropertyRef target;
    DEKI_EXPORT DEKI_VALUE_OF(target) std::string to;
    DEKI_EXPORT std::string toVariable;
    DEKI_EXPORT float duration = 1.0f;
    DEKI_EXPORT std::string durationVariable;
    DEKI_EXPORT DekiTween::EaseType ease = DekiTween::EaseType::Linear;
    DEKI_EXPORT bool relative = false;
};

/// Arithmetic on any numeric field or variable: target = target op operand.
/// For counters, scores and health: "Score += 10" is this action on a graph
/// variable, "Health -= 1" the same on a component field. Finishes at once
/// unless everyFrame is on, which keeps the flow here.
struct FsmModifyPropertyAction
{
    DEKI_NODE(FsmModifyPropertyAction, "FsmModifyProperty", "Fsm/Actions")
    static constexpr const char* kStaticNodeDisplayName = "Modify Property";
    static constexpr const char* kStaticNodeDescription =
        "Do arithmetic on a number field or variable. The score and health workhorse.";
    DEKI_NODE_INPUTS("in")
    DEKI_NODE_OUTPUTS("done")
public:
    DEKI_EXPORT Deki::PropertyRef target;
    DEKI_EXPORT FsmMathOp operation = FsmMathOp::Add;
    DEKI_EXPORT DEKI_VALUE_OF(target) std::string operand;
    DEKI_EXPORT std::string operandVariable;
    DEKI_EXPORT bool everyFrame = false;
};

/// Writes a random number into any numeric field or variable. `wholeNumbers`
/// rounds to an integer, so "pick a room 1..5" and "jitter a position by 0.1m"
/// are the same action.
struct FsmRandomPropertyAction
{
    DEKI_NODE(FsmRandomPropertyAction, "FsmRandomProperty", "Fsm/Actions")
    static constexpr const char* kStaticNodeDisplayName = "Random Property";
    static constexpr const char* kStaticNodeDescription = "Write a random number into a number field or variable.";
    DEKI_NODE_INPUTS("in")
    DEKI_NODE_OUTPUTS("done")
public:
    DEKI_EXPORT Deki::PropertyRef target;
    DEKI_EXPORT float min = 0.0f;
    DEKI_EXPORT float max = 1.0f;
    DEKI_EXPORT bool wholeNumbers = false;
};

/// Instantiates a scene into the running scene at (x, y) meters, relative to
/// the spawner's position when `relative` is on. `spawnedName` renames the new
/// root so later actions and other states can find it by name; empty keeps the
/// scene's own name.
///
/// The graph window has no asset picker, so `scene` is entered there as a GUID
/// string; it saves and loads correctly either way.
struct FsmSpawnSceneAction
{
    DEKI_NODE(FsmSpawnSceneAction, "FsmSpawnScene", "Fsm/Actions")
    static constexpr const char* kStaticNodeDisplayName = "Spawn Scene";
    static constexpr const char* kStaticNodeDescription = "Instantiate a scene into the running scene.";
    DEKI_NODE_INPUTS("in")
    DEKI_NODE_OUTPUTS("done")
public:
    DEKI_EXPORT Deki::AssetRef<Deki::Scene> scene;
    DEKI_EXPORT DEKI_UNIT(Distance) float x = 0.0f;
    DEKI_EXPORT DEKI_UNIT(Distance) float y = 0.0f;
    DEKI_EXPORT bool relative = false;
    DEKI_EXPORT std::string spawnedName;
};

/// Removes an object and its children from the running scene. An empty
/// targetObject destroys the object this FSM is on, which also stops the
/// machine, so put it at the end of a flow.
struct FsmDestroyObjectAction
{
    DEKI_NODE(FsmDestroyObjectAction, "FsmDestroyObject", "Fsm/Actions")
    static constexpr const char* kStaticNodeDisplayName = "Destroy Object";
    static constexpr const char* kStaticNodeDescription = "Remove an object and its children from the scene.";
    DEKI_NODE_INPUTS("in")
    DEKI_NODE_OUTPUTS("done")
public:
    DEKI_EXPORT DEKI_OBJECT_NAME() std::string targetObject;
};

/// Moves an object under a new parent. An empty newParent moves it to the scene
/// root, for example to detach a picked-up item from the hand that carried it.
struct FsmSetParentAction
{
    DEKI_NODE(FsmSetParentAction, "FsmSetParent", "Fsm/Actions")
    static constexpr const char* kStaticNodeDisplayName = "Set Parent";
    static constexpr const char* kStaticNodeDescription = "Move an object under a new parent, or out to the root.";
    DEKI_NODE_INPUTS("in")
    DEKI_NODE_OUTPUTS("done")
public:
    DEKI_EXPORT DEKI_OBJECT_NAME() std::string targetObject;
    DEKI_EXPORT DEKI_OBJECT_NAME() std::string newParent;
};

/// Plays a sequence on the target's Deki2D::AnimationComponent. With
/// `waitForFinish` the action finishes with the animation, so the flow
/// continues after it; otherwise it finishes at once and the animation keeps
/// running by itself. With `loop` off it plays once.
struct FsmPlayAnimationAction
{
    DEKI_NODE(FsmPlayAnimationAction, "FsmPlayAnimation", "Fsm/Actions")
    static constexpr const char* kStaticNodeDisplayName = "Play Animation";
    static constexpr const char* kStaticNodeDescription =
        "Play an animation on the target, optionally waiting for it to finish.";
    DEKI_NODE_INPUTS("in")
    DEKI_NODE_OUTPUTS("done")
public:
    DEKI_EXPORT DEKI_OBJECT_NAME(Deki2D::AnimationComponent) std::string targetObject;
    DEKI_EXPORT int32_t sequence = 0;
    DEKI_EXPORT bool loop = true;
    DEKI_EXPORT bool waitForFinish = false;
};

/// Raises an event on another object's FsmComponent (an empty targetObject
/// means this one). This is how machines talk: a door's FSM tells the room's
/// FSM that it opened, without either knowing the other's states.
struct FsmSendEventToAction
{
    DEKI_NODE(FsmSendEventToAction, "FsmSendEventTo", "Fsm/Actions")
    static constexpr const char* kStaticNodeDisplayName = "Send Event To";
    static constexpr const char* kStaticNodeDescription = "Raise an event on another object's FSM.";
    DEKI_NODE_INPUTS("in")
    DEKI_NODE_OUTPUTS("done")
public:
    DEKI_EXPORT DEKI_OBJECT_NAME() std::string targetObject;
    DEKI_EXPORT std::string eventName = "EVENT";
};

/// Writes a line to the console, to see what ran and when.
struct FsmLogAction
{
    DEKI_NODE(FsmLogAction, "FsmLog", "Fsm/Actions")
    static constexpr const char* kStaticNodeDisplayName = "Log";
    static constexpr const char* kStaticNodeDescription = "Write a line to the console.";
    DEKI_NODE_INPUTS("in")
    DEKI_NODE_OUTPUTS("done")
public:
    DEKI_EXPORT std::string message;
};

/// Waits here until the target's Deki2D::ButtonComponent is clicked, then
/// continues down "clicked". It keeps watching as long as its state is active
/// (clicks while another state is active are dropped), and nothing after it
/// runs until a click comes.
///
/// Wire "clicked" to a Send Event action and a button press becomes a state
/// transition. Give each watched button its own state (its own track wired
/// from Update) rather than chaining watchers, since a waiting flow only
/// watches one.
struct FsmWatchButtonAction
{
    DEKI_NODE(FsmWatchButtonAction, "FsmWatchButton", "Fsm/Actions")
    static constexpr const char* kStaticNodeDisplayName = "Watch Button";
    static constexpr const char* kStaticNodeDescription = "Wait here until the target button is clicked.";
    DEKI_NODE_INPUTS("in")
    DEKI_NODE_OUTPUTS("clicked")
public:
    DEKI_EXPORT DEKI_OBJECT_NAME(Deki2D::ButtonComponent) std::string buttonObject;
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

}  // namespace DekiFsm
