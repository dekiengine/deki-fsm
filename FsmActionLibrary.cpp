/**
 * @file FsmActionLibrary.cpp
 * @brief Runtime behavior (FsmActionOps) for the milestone-1 action set.
 *
 * Data structs live in FsmActions.h; this file supplies each one's callbacks
 * and registers them keyed by node-name hash (REGISTER_FSM_ACTION). Per-run
 * state goes in the interpreter's zero-initialized blob (ops.stateSize), never
 * in the shared data struct — many FsmComponents may run the same graph asset.
 *
 * Failure policy is the module's: anything unresolvable (object name, unknown
 * component/property, unparsable value) calls ctx.Fail once and the machine
 * latches off. No fallbacks.
 */

#include "FsmActions.h"
#include "FsmActionRegistry.h"
#include "FsmComponent.h"

#include "deki-2d/ButtonComponent.h"
#include "deki-tween/Easing.h"

#include "DekiComponent.h"   // DekiHashString
#include "DekiObject.h"
#include "DekiLogSystem.h"
#include "reflection/ComponentRegistry.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace
{

// ---------------------------------------------------------------------------
// Reflection plumbing shared by Set Property / Compare Property.
// ---------------------------------------------------------------------------

// Resolve (targetObject, component, property) to a live field pointer. Any
// miss fails the FSM (logged with the offending name) and returns false.
struct ResolvedField
{
    void* field = nullptr;
    const DekiPropertyInfo* info = nullptr;
};

bool ResolveField(FsmContext& ctx, const std::string& targetObject,
                  const std::string& componentName, const std::string& propertyName,
                  ResolvedField& out)
{
    DekiObject* target = ctx.ResolveTarget(targetObject);
    if (!target)
        return false;   // already failed

    char buf[224];
    const DekiComponentMeta* meta =
        ComponentRegistry::Instance().GetMetaByClassName(componentName);
    if (!meta)
    {
        std::snprintf(buf, sizeof(buf), "unknown component type '%s'", componentName.c_str());
        ctx.Fail(buf);
        return false;
    }

    DekiComponent* comp = target->GetComponentByTypeId(meta->typeId);
    if (!comp)
    {
        std::snprintf(buf, sizeof(buf), "object '%s' has no %s component",
                      target->GetName().c_str(), componentName.c_str());
        ctx.Fail(buf);
        return false;
    }

    for (int i = 0; i < meta->propertyCount; ++i)
    {
        if (std::strcmp(meta->properties[i].name, propertyName.c_str()) == 0)
        {
            out.info = &meta->properties[i];
            out.field = reinterpret_cast<char*>(comp) + out.info->offset;
            return true;
        }
    }
    std::snprintf(buf, sizeof(buf), "component '%s' has no property '%s'",
                  componentName.c_str(), propertyName.c_str());
    ctx.Fail(buf);
    return false;
}

// Write `value` (authored as text) into a resolved field, parsed per the
// property's reflected type. Unsupported types fail the FSM.
bool WriteFieldValue(FsmContext& ctx, const ResolvedField& f, const std::string& value)
{
    const DekiPropertyInfo& p = *f.info;
    switch (p.type)
    {
        case DekiPropertyType::Float:
        {
            const float v = std::strtof(value.c_str(), nullptr);
            std::memcpy(f.field, &v, sizeof v);
            return true;
        }
        case DekiPropertyType::Double:
        {
            const double v = std::strtod(value.c_str(), nullptr);
            std::memcpy(f.field, &v, sizeof v);
            return true;
        }
        case DekiPropertyType::Bool:
        {
            const bool v = value == "true" || value == "1";
            std::memcpy(f.field, &v, sizeof v);
            return true;
        }
        case DekiPropertyType::String:
        {
            *static_cast<std::string*>(f.field) = value;
            return true;
        }
        case DekiPropertyType::Enum:
        {
            // Enum value name first ("Happy"); a plain number also works.
            long v = -1;
            for (int e = 0; e < p.enumCount; ++e)
            {
                if (value == p.enumValues[e]) { v = e; break; }
            }
            if (v < 0)
            {
                char* end = nullptr;
                v = std::strtol(value.c_str(), &end, 10);
                if (end == value.c_str())
                {
                    char buf[192];
                    std::snprintf(buf, sizeof(buf), "'%s' is not a value of enum property '%s'",
                                  value.c_str(), p.name);
                    ctx.Fail(buf);
                    return false;
                }
            }
            switch (p.enumSize)
            {
                case 1: { uint8_t s = (uint8_t)v;  std::memcpy(f.field, &s, 1); break; }
                case 2: { uint16_t s = (uint16_t)v; std::memcpy(f.field, &s, 2); break; }
                default: { uint32_t s = (uint32_t)v; std::memcpy(f.field, &s, 4); break; }
            }
            return true;
        }
        case DekiPropertyType::Int8:   { int8_t v = (int8_t)std::strtol(value.c_str(), nullptr, 10);    std::memcpy(f.field, &v, sizeof v); return true; }
        case DekiPropertyType::Int16:  { int16_t v = (int16_t)std::strtol(value.c_str(), nullptr, 10);  std::memcpy(f.field, &v, sizeof v); return true; }
        case DekiPropertyType::Int32:  { int32_t v = (int32_t)std::strtol(value.c_str(), nullptr, 10);  std::memcpy(f.field, &v, sizeof v); return true; }
        case DekiPropertyType::Int64:  { int64_t v = (int64_t)std::strtoll(value.c_str(), nullptr, 10); std::memcpy(f.field, &v, sizeof v); return true; }
        case DekiPropertyType::UInt8:  { uint8_t v = (uint8_t)std::strtoul(value.c_str(), nullptr, 10);   std::memcpy(f.field, &v, sizeof v); return true; }
        case DekiPropertyType::UInt16: { uint16_t v = (uint16_t)std::strtoul(value.c_str(), nullptr, 10); std::memcpy(f.field, &v, sizeof v); return true; }
        case DekiPropertyType::UInt32: { uint32_t v = (uint32_t)std::strtoul(value.c_str(), nullptr, 10); std::memcpy(f.field, &v, sizeof v); return true; }
        case DekiPropertyType::UInt64: { uint64_t v = (uint64_t)std::strtoull(value.c_str(), nullptr, 10); std::memcpy(f.field, &v, sizeof v); return true; }
        default:
        {
            char buf[192];
            std::snprintf(buf, sizeof(buf), "property '%s' has a type Set Property cannot write",
                          p.name);
            ctx.Fail(buf);
            return false;
        }
    }
}

// Read a resolved field as a double for numeric comparison. Returns false
// (without failing) when the type is not numeric — the caller decides.
bool ReadFieldNumeric(const ResolvedField& f, double& out)
{
    const DekiPropertyInfo& p = *f.info;
    switch (p.type)
    {
        case DekiPropertyType::Float:  { float v;   std::memcpy(&v, f.field, sizeof v); out = v; return true; }
        case DekiPropertyType::Double: { double v;  std::memcpy(&v, f.field, sizeof v); out = v; return true; }
        case DekiPropertyType::Bool:   { bool v;    std::memcpy(&v, f.field, sizeof v); out = v ? 1.0 : 0.0; return true; }
        case DekiPropertyType::Int8:   { int8_t v;  std::memcpy(&v, f.field, sizeof v); out = v; return true; }
        case DekiPropertyType::Int16:  { int16_t v; std::memcpy(&v, f.field, sizeof v); out = v; return true; }
        case DekiPropertyType::Int32:  { int32_t v; std::memcpy(&v, f.field, sizeof v); out = v; return true; }
        case DekiPropertyType::Int64:  { int64_t v; std::memcpy(&v, f.field, sizeof v); out = (double)v; return true; }
        case DekiPropertyType::UInt8:  { uint8_t v;  std::memcpy(&v, f.field, sizeof v); out = v; return true; }
        case DekiPropertyType::UInt16: { uint16_t v; std::memcpy(&v, f.field, sizeof v); out = v; return true; }
        case DekiPropertyType::UInt32: { uint32_t v; std::memcpy(&v, f.field, sizeof v); out = v; return true; }
        case DekiPropertyType::UInt64: { uint64_t v; std::memcpy(&v, f.field, sizeof v); out = (double)v; return true; }
        case DekiPropertyType::Enum:
        {
            switch (p.enumSize)
            {
                case 1: { uint8_t v;  std::memcpy(&v, f.field, 1); out = v; break; }
                case 2: { uint16_t v; std::memcpy(&v, f.field, 2); out = v; break; }
                default: { uint32_t v; std::memcpy(&v, f.field, 4); out = v; break; }
            }
            return true;
        }
        default:
            return false;
    }
}

// Parse the authored comparison value for a field (enum names resolve to
// their index; anything else parses as a number).
double ParseNumericValue(const DekiPropertyInfo& p, const std::string& value)
{
    if (p.type == DekiPropertyType::Enum)
    {
        for (int e = 0; e < p.enumCount; ++e)
            if (value == p.enumValues[e])
                return (double)e;
    }
    if (p.type == DekiPropertyType::Bool)
        return (value == "true" || value == "1") ? 1.0 : 0.0;
    return std::strtod(value.c_str(), nullptr);
}

// ---------------------------------------------------------------------------
// Wait
// ---------------------------------------------------------------------------

struct WaitState { float elapsed; };

bool Wait_Update(const void* data, void* state, FsmContext& ctx)
{
    const auto* d = static_cast<const FsmWaitAction*>(data);
    auto* s = static_cast<WaitState*>(state);
    s->elapsed += ctx.dt;
    return s->elapsed >= d->seconds;
}

const FsmActionOps kWaitOps = { sizeof(WaitState), nullptr, &Wait_Update, nullptr };

// ---------------------------------------------------------------------------
// Send Event
// ---------------------------------------------------------------------------

struct SendEventState { float elapsed; uint8_t sent; };

bool SendEvent_Update(const void* data, void* state, FsmContext& ctx)
{
    const auto* d = static_cast<const FsmSendEventAction*>(data);
    auto* s = static_cast<SendEventState*>(state);
    if (s->sent)
        return true;
    if (d->eventName.empty())
    {
        ctx.Fail("Send Event action has an empty event name");
        return true;
    }
    s->elapsed += ctx.dt;
    if (s->elapsed >= d->delaySec)
    {
        ctx.SendEvent(d->eventName);
        s->sent = 1;
        return true;
    }
    return false;
}

const FsmActionOps kSendEventOps = { sizeof(SendEventState), nullptr, &SendEvent_Update, nullptr };

// ---------------------------------------------------------------------------
// Set Property
// ---------------------------------------------------------------------------

struct SetPropertyState { uint8_t applied; };

bool SetProperty_Update(const void* data, void* state, FsmContext& ctx)
{
    const auto* d = static_cast<const FsmSetPropertyAction*>(data);
    auto* s = static_cast<SetPropertyState*>(state);
    if (!d->everyFrame && s->applied)
        return true;

    ResolvedField f;
    if (!ResolveField(ctx, d->targetObject, d->component, d->property, f))
        return true;   // FSM latched
    if (!WriteFieldValue(ctx, f, d->value))
        return true;   // FSM latched
    s->applied = 1;
    return !d->everyFrame;   // everyFrame keeps running (never finishes)
}

const FsmActionOps kSetPropertyOps = { sizeof(SetPropertyState), nullptr, &SetProperty_Update, nullptr };

// ---------------------------------------------------------------------------
// Compare Property
// ---------------------------------------------------------------------------

struct CompareState { uint8_t fired; };

bool Compare_Update(const void* data, void* state, FsmContext& ctx)
{
    const auto* d = static_cast<const FsmComparePropertyAction*>(data);
    auto* s = static_cast<CompareState*>(state);

    ResolvedField f;
    if (!ResolveField(ctx, d->targetObject, d->component, d->property, f))
        return true;   // FSM latched

    bool holds = false;
    double current = 0.0;
    if (ReadFieldNumeric(f, current))
    {
        const double ref = ParseNumericValue(*f.info, d->value);
        switch (d->compare)
        {
            case FsmCompareOp::Equals:    holds = current == ref; break;
            case FsmCompareOp::NotEquals: holds = current != ref; break;
            case FsmCompareOp::Less:      holds = current < ref;  break;
            case FsmCompareOp::Greater:   holds = current > ref;  break;
        }
    }
    else if (f.info->type == DekiPropertyType::String)
    {
        const std::string& cur = *static_cast<const std::string*>(
            const_cast<const void*>(f.field));
        if (d->compare == FsmCompareOp::Equals)         holds = cur == d->value;
        else if (d->compare == FsmCompareOp::NotEquals) holds = cur != d->value;
        else
        {
            ctx.Fail("Compare Property: Less/Greater on a String property");
            return true;
        }
    }
    else
    {
        ctx.Fail("Compare Property: property type is not comparable");
        return true;
    }

    if (holds)
    {
        // Edge-triggered: fire once, re-arm when the condition stops holding,
        // so a state that ignores the event is not spammed every frame.
        if (!s->fired)
        {
            s->fired = 1;
            if (d->eventIfTrue.empty())
            {
                ctx.Fail("Compare Property action has an empty event name");
                return true;
            }
            ctx.SendEvent(d->eventIfTrue);
        }
    }
    else
    {
        s->fired = 0;
    }

    return !d->everyFrame;   // one-shot evaluates once; everyFrame never finishes
}

const FsmActionOps kCompareOps = { sizeof(CompareState), nullptr, &Compare_Update, nullptr };

// ---------------------------------------------------------------------------
// Move To
// ---------------------------------------------------------------------------

struct MoveToState { float elapsed; float startX; float startY; uint8_t started; };

bool MoveTo_Update(const void* data, void* state, FsmContext& ctx)
{
    const auto* d = static_cast<const FsmMoveToAction*>(data);
    auto* s = static_cast<MoveToState*>(state);

    DekiObject* target = ctx.ResolveTarget(d->targetObject);
    if (!target)
        return true;   // FSM latched

    if (!s->started)
    {
        s->startX = target->GetX();
        s->startY = target->GetY();
        s->started = 1;
    }

    const float endX = d->relative ? s->startX + d->x : d->x;
    const float endY = d->relative ? s->startY + d->y : d->y;

    s->elapsed += ctx.dt;
    float u = d->duration > 0.0f ? s->elapsed / d->duration : 1.0f;
    if (u > 1.0f) u = 1.0f;
    const float e = deki::Ease::GetFunction(d->ease)(u);

    target->SetX(s->startX + (endX - s->startX) * e);
    target->SetY(s->startY + (endY - s->startY) * e);
    return u >= 1.0f;
}

const FsmActionOps kMoveToOps = { sizeof(MoveToState), nullptr, &MoveTo_Update, nullptr };

// ---------------------------------------------------------------------------
// Watch Button
// ---------------------------------------------------------------------------

bool WatchButton_Update(const void* data, void* /*state*/, FsmContext& ctx)
{
    const auto* d = static_cast<const FsmWatchButtonAction*>(data);

    DekiObject* target = ctx.ResolveTarget(d->buttonObject);
    if (!target)
        return true;   // FSM latched

    ButtonComponent* button = target->GetComponent<ButtonComponent>();
    if (!button)
    {
        char buf[192];
        std::snprintf(buf, sizeof(buf), "Watch Button: object '%s' has no ButtonComponent",
                      target->GetName().c_str());
        ctx.Fail(buf);
        return true;
    }

    // The flag outlives state re-entries (registered once per FSM + action
    // instance), so clicks are never double-subscribed.
    std::shared_ptr<bool> clicked = ctx.fsm->EnsureClickWatch(data, button);
    if (*clicked)
    {
        *clicked = false;
        if (d->eventName.empty())
        {
            ctx.Fail("Watch Button action has an empty event name");
            return true;
        }
        ctx.SendEvent(d->eventName);
    }
    return false;   // keeps watching while the state is active
}

const FsmActionOps kWatchButtonOps = { 0, nullptr, &WatchButton_Update, nullptr };

} // namespace

// ---------------------------------------------------------------------------
// Registration (typeId = hash of the node name, as stored by the graph loader)
// ---------------------------------------------------------------------------

REGISTER_FSM_ACTION(FsmWaitAction, kWaitOps);
REGISTER_FSM_ACTION(FsmSendEventAction, kSendEventOps);
REGISTER_FSM_ACTION(FsmSetPropertyAction, kSetPropertyOps);
REGISTER_FSM_ACTION(FsmComparePropertyAction, kCompareOps);
REGISTER_FSM_ACTION(FsmMoveToAction, kMoveToOps);
REGISTER_FSM_ACTION(FsmWatchButtonAction, kWatchButtonOps);
