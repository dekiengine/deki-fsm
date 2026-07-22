#pragma once

#include <string>

#include "reflection/DekiNode.h"
#include "deki-tween/Easing.h"   // deki::EaseType (Move To easing dropdown)

// Milestone-1 action library: the data side. Each action is a plain reflected
// struct (category "Fsm/Actions" = FsmStateNode's child category, so these are
// authored inside a state's action stack, never on the canvas). Runtime
// behavior is registered separately in FsmActionLibrary.cpp via
// REGISTER_FSM_ACTION; project DLLs add game-specific actions the same way.
//
// Object targeting convention: `targetObject` empty = the FSM's owner object,
// else the name of an object in the same prefab. A name that resolves to
// nothing is a graph error at runtime (no fallback).

// Comparison operator for Compare Property.
enum class FsmCompareOp : uint8_t
{
    Equals = 0,
    NotEquals,
    Less,
    Greater,
};

// Do nothing for a random-free, fixed number of seconds, then finish. The
// classic FINISHED driver: Wait 2s + transitions ["FINISHED"] = a timed state.
struct FsmWaitAction
{
    DEKI_NODE(FsmWaitAction, "FsmWait", "Fsm/Actions")
    static constexpr const char* StaticNodeDisplayName = "Wait";
public:
    DEKI_EXPORT float seconds = 1.0f;
};

// Raise an event on this FSM (optionally after a delay), then finish. The
// event is matched against the ACTIVE state's transitions when processed.
struct FsmSendEventAction
{
    DEKI_NODE(FsmSendEventAction, "FsmSendEvent", "Fsm/Actions")
    static constexpr const char* StaticNodeDisplayName = "Send Event";
public:
    DEKI_EXPORT std::string eventName = "EVENT";
    DEKI_EXPORT float delaySec = 0.0f;
};

// Write any DEKI_EXPORT property of any component through reflection:
// component = class name ("TextComponent"), property = field name ("text"),
// value = parsed to the field's type (numbers, true/false, enum value name,
// or raw string). everyFrame re-applies each frame and never finishes.
struct FsmSetPropertyAction
{
    DEKI_NODE(FsmSetPropertyAction, "FsmSetProperty", "Fsm/Actions")
    static constexpr const char* StaticNodeDisplayName = "Set Property";
public:
    DEKI_EXPORT std::string targetObject;
    DEKI_EXPORT std::string component;
    DEKI_EXPORT std::string property;
    DEKI_EXPORT std::string value;
    DEKI_EXPORT bool everyFrame = false;
};

// Read a component property each frame and raise `eventIfTrue` when the
// comparison holds (re-armed when it stops holding). The reflection-driven
// "if" of the FSM: branch states on any component's live data.
struct FsmComparePropertyAction
{
    DEKI_NODE(FsmComparePropertyAction, "FsmCompareProperty", "Fsm/Actions")
    static constexpr const char* StaticNodeDisplayName = "Compare Property";
public:
    DEKI_EXPORT std::string targetObject;
    DEKI_EXPORT std::string component;
    DEKI_EXPORT std::string property;
    DEKI_EXPORT FsmCompareOp compare = FsmCompareOp::Equals;
    DEKI_EXPORT std::string value;
    DEKI_EXPORT std::string eventIfTrue = "EVENT";
    DEKI_EXPORT bool everyFrame = true;
};

// Ease the target object to (x, y) meters over `duration` seconds, then
// finish. relative = destination is an offset from where the object was when
// the state entered. Easing curves come from deki-tween.
struct FsmMoveToAction
{
    DEKI_NODE(FsmMoveToAction, "FsmMoveTo", "Fsm/Actions")
    static constexpr const char* StaticNodeDisplayName = "Move To";
public:
    DEKI_EXPORT std::string targetObject;
    DEKI_EXPORT DEKI_UNIT(Distance) float x = 0.0f;
    DEKI_EXPORT DEKI_UNIT(Distance) float y = 0.0f;
    DEKI_EXPORT float duration = 1.0f;
    DEKI_EXPORT deki::EaseType ease = deki::EaseType::Linear;
    DEKI_EXPORT bool relative = false;
};

// Raise an event whenever the target's ButtonComponent is clicked. Never
// finishes — it keeps watching while the state is active (clicks in other
// states are dropped). This is the input->transition bridge: UI flows become
// states wired by button events.
struct FsmWatchButtonAction
{
    DEKI_NODE(FsmWatchButtonAction, "FsmWatchButton", "Fsm/Actions")
    static constexpr const char* StaticNodeDisplayName = "Watch Button";
public:
    DEKI_EXPORT std::string buttonObject;
    DEKI_EXPORT std::string eventName = "CLICKED";
};

#include "generated/FsmWaitAction.gen.h"
#include "generated/FsmSendEventAction.gen.h"
#include "generated/FsmSetPropertyAction.gen.h"
#include "generated/FsmComparePropertyAction.gen.h"
#include "generated/FsmMoveToAction.gen.h"
#include "generated/FsmWatchButtonAction.gen.h"
