/**
 * @file FsmActionLibrary.cpp
 * @brief Runtime behavior (FsmActionOps) for the milestone-1 action set.
 *
 * Data structs live in FsmActions.h; this file supplies each one's callbacks
 * and registers them keyed by node-name hash (REGISTER_FSM_ACTION). Per-run
 * state goes in the interpreter's zero-initialized blob (ops.stateSize), never
 * in the shared data struct — many FsmComponents may run the same graph asset.
 *
 * Resolution happens in onEnter, never per frame: object lookups, field
 * bindings and literal parsing all land in the blob when the action starts, so
 * onUpdate is a store, a compare or a bool read. Nothing here touches a string
 * once a state is running.
 *
 * Failure policy is the package's: anything unresolvable (object name, missing
 * component/field, unparsable value) calls ctx.Fail once and the machine
 * latches off. No fallbacks.
 */

#include "FsmActions.h"
#include "FsmActionRegistry.h"
#include "FsmComponent.h"

#include "deki-2d/AnimationComponent.h"
#include "deki-2d/ButtonComponent.h"
#include "deki-tween/Easing.h"

#include "DekiComponent.h"   // DekiHashString
#include "DekiObject.h"
#include "DekiLogSystem.h"
#include "Scene.h"                   // Instantiate / RemoveObject
#include "reflection/PropertyRef.h"   // BindPropertyRef / Write / Compare

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace
{

// The pin a single-outcome action finishes on. Branching actions name their
// own (Compare Property: kTruePin / kFalsePin), matching the order of the
// labels in their DEKI_NODE_OUTPUTS declaration.
constexpr int kDone = 0;
constexpr int kTruePin = 0;
constexpr int kFalsePin = 1;

// ---------------------------------------------------------------------------
// Binding plumbing shared by Set Property / Compare Property.
//
// Both actions resolve their PropertyRef ONCE, in onEnter, into the per-run
// state blob: object lookup, component lookup, field lookup and literal parse
// all happen there, and the per-frame path is a write or a compare through a
// cached pointer. The blob is zero-initialized, so BoundState must stay POD.
// ---------------------------------------------------------------------------

struct BoundState
{
    PropertyBinding binding;
    uint8_t bound;     // 1 once the reference resolved (0 = the FSM latched)
    uint8_t flag;      // Set Property: applied; Compare Property: fired
};

// Resolve a reference to a live binding. A "Variable" reference is the
// machine's own storage (the engine can't see it), anything else is a component
// field or the object's transform.
bool BindRef(FsmContext& ctx, const PropertyRef& ref, const char* actionName,
             PropertyBinding& out)
{
    if (ref.component == kVariableRefComponent)
        return ctx.fsm && ctx.fsm->BindVariable(ref, out);

    DekiObject* target = ctx.ResolveTarget(ref.object);
    if (!target)
        return false;   // FSM already latched by ResolveTarget

    const char* why = nullptr;
    if (!BindPropertyRef(target, ref, out, &why))
    {
        char buf[224];
        std::snprintf(buf, sizeof(buf), "%s: %s (object '%s', component '%s', field '%s')",
                      actionName, why ? why : "unresolved reference",
                      target->GetName().c_str(), ref.component.c_str(), ref.field.c_str());
        ctx.Fail(buf);
        return false;
    }
    return true;
}

// Resolve `ref` and pre-parse `literal` into s->binding. Fails the FSM (once,
// with the offending names) and leaves s->bound at 0 on any miss.
void BindOrFail(FsmContext& ctx, const PropertyRef& ref, const std::string& literal,
                const char* actionName, BoundState* s)
{
    if (!BindRef(ctx, ref, actionName, s->binding))
        return;

    if (!ParsePropertyLiteral(*s->binding.info, literal.c_str(), s->binding.number))
    {
        char buf[224];
        std::snprintf(buf, sizeof(buf), "%s: '%s' is not a valid value for field '%s'",
                      actionName, literal.c_str(), ref.field.c_str());
        ctx.Fail(buf);
        return;
    }
    s->bound = 1;
}

// ---------------------------------------------------------------------------
// Wait
// ---------------------------------------------------------------------------

struct WaitState { float elapsed; };

int Wait_Update(const void* data, void* state, FsmContext& ctx)
{
    const auto* d = static_cast<const FsmWaitAction*>(data);
    auto* s = static_cast<WaitState*>(state);
    s->elapsed += ctx.dt;
    return s->elapsed >= d->seconds ? kDone : kFsmActionRunning;
}

const FsmActionOps kWaitOps = { sizeof(WaitState), nullptr, &Wait_Update, nullptr };

// ---------------------------------------------------------------------------
// Send Event
// ---------------------------------------------------------------------------

struct SendEventState { float elapsed; uint8_t sent; };

int SendEvent_Update(const void* data, void* state, FsmContext& ctx)
{
    const auto* d = static_cast<const FsmSendEventAction*>(data);
    auto* s = static_cast<SendEventState*>(state);
    if (s->sent)
        return kDone;
    if (d->eventName.empty())
    {
        ctx.Fail("Send Event action has an empty event name");
        return kDone;
    }
    s->elapsed += ctx.dt;
    if (s->elapsed >= d->delaySec)
    {
        ctx.SendEvent(d->eventName);
        s->sent = 1;
        return kDone;
    }
    return kFsmActionRunning;
}

const FsmActionOps kSendEventOps = { sizeof(SendEventState), nullptr, &SendEvent_Update, nullptr };

// ---------------------------------------------------------------------------
// Set Property
// ---------------------------------------------------------------------------

void SetProperty_Enter(const void* data, void* state, FsmContext& ctx)
{
    const auto* d = static_cast<const FsmSetPropertyAction*>(data);
    BindOrFail(ctx, d->target, d->value, "Set Property", static_cast<BoundState*>(state));
}

int SetProperty_Update(const void* data, void* state, FsmContext& ctx)
{
    const auto* d = static_cast<const FsmSetPropertyAction*>(data);
    auto* s = static_cast<BoundState*>(state);
    if (!s->bound)
        return kDone;   // FSM latched in onEnter

    // Everything expensive already happened at bind time: this is a store.
    WriteBoundProperty(s->binding, d->value);
    s->flag = 1;
    return d->everyFrame ? kFsmActionRunning : kDone;   // everyFrame parks the flow
}

const FsmActionOps kSetPropertyOps = { sizeof(BoundState), &SetProperty_Enter, &SetProperty_Update, nullptr };

// ---------------------------------------------------------------------------
// Compare Property
// ---------------------------------------------------------------------------

void Compare_Enter(const void* data, void* state, FsmContext& ctx)
{
    const auto* d = static_cast<const FsmComparePropertyAction*>(data);
    auto* s = static_cast<BoundState*>(state);
    BindOrFail(ctx, d->target, d->value, "Compare Property", s);
    if (!s->bound)
        return;

    // Ordering a string has no meaning here; catch it at bind time rather than
    // silently comparing something surprising every frame.
    if (static_cast<DekiPropertyType>(s->binding.info->type) == DekiPropertyType::String &&
        d->compare != FsmCompareOp::Equals && d->compare != FsmCompareOp::NotEquals)
    {
        ctx.Fail("Compare Property: Less/Greater on a String field");
        s->bound = 0;
    }
}

int Compare_Update(const void* data, void* state, FsmContext& ctx)
{
    const auto* d = static_cast<const FsmComparePropertyAction*>(data);
    auto* s = static_cast<BoundState*>(state);
    if (!s->bound)
        return kDone;   // FSM latched in onEnter

    const int cmp = CompareBoundProperty(s->binding, d->value);
    bool holds = false;
    switch (d->compare)
    {
        case FsmCompareOp::Equals:    holds = cmp == 0; break;
        case FsmCompareOp::NotEquals: holds = cmp != 0; break;
        case FsmCompareOp::Less:      holds = cmp < 0;  break;
        case FsmCompareOp::Greater:   holds = cmp > 0;  break;
    }

    // Gate: park here (re-testing every frame) until the comparison holds, then
    // leave down "true". There is no false outcome in this mode by definition.
    if (d->waitUntilTrue)
        return holds ? kTruePin : kFsmActionRunning;

    // Branch: decide now and leave down the matching pin. No event names, no
    // edge tracking — the outcome IS the wire that gets followed.
    return holds ? kTruePin : kFalsePin;
}

const FsmActionOps kCompareOps = { sizeof(BoundState), &Compare_Enter, &Compare_Update, nullptr };

// ---------------------------------------------------------------------------
// Tween Property
// ---------------------------------------------------------------------------

// Bound once in onEnter, along with the start value(s) read off the live field.
// A Vector2 target drives both axes, so one action can move diagonally.
struct TweenState
{
    PropertyBinding binding;
    uint8_t bound;
    float elapsed;
    double start;    // first axis
    double start2;   // second axis (Vector2 targets)
};

void Tween_Enter(const void* data, void* state, FsmContext& ctx)
{
    const auto* d = static_cast<const FsmTweenPropertyAction*>(data);
    auto* s = static_cast<TweenState*>(state);

    if (!BindRef(ctx, d->target, "Tween Property", s->binding))
        return;   // FSM latched

    const auto type = static_cast<DekiPropertyType>(s->binding.info->type);
    if (type != DekiPropertyType::Float && type != DekiPropertyType::Double &&
        type != DekiPropertyType::Vector2)
    {
        ctx.Fail("Tween Property: only float and Vector2 fields can be tweened");
        return;
    }

    if (!ParsePropertyLiteral(*s->binding.info, d->to.c_str(),
                              s->binding.number, s->binding.number2))
    {
        char buf[224];
        std::snprintf(buf, sizeof(buf), "Tween Property: '%s' is not a valid value for field '%s'",
                      d->to.c_str(), d->target.field.c_str());
        ctx.Fail(buf);
        return;
    }

    s->start = ReadBoundProperty(s->binding);
    s->start2 = ReadBoundProperty2(s->binding);
    s->bound = 1;
}

int Tween_Update(const void* data, void* state, FsmContext& ctx)
{
    const auto* d = static_cast<const FsmTweenPropertyAction*>(data);
    auto* s = static_cast<TweenState*>(state);
    if (!s->bound)
        return kDone;   // FSM latched in onEnter

    const double end  = d->relative ? s->start  + s->binding.number  : s->binding.number;
    const double end2 = d->relative ? s->start2 + s->binding.number2 : s->binding.number2;

    s->elapsed += ctx.dt;
    float u = d->duration > 0.0f ? s->elapsed / d->duration : 1.0f;
    if (u > 1.0f) u = 1.0f;
    const float e = deki::Ease::GetFunction(d->ease)(u);

    WriteBoundNumbers(s->binding,
                      s->start  + (end  - s->start)  * e,
                      s->start2 + (end2 - s->start2) * e);
    return u >= 1.0f ? kDone : kFsmActionRunning;
}

const FsmActionOps kTweenOps = { sizeof(TweenState), &Tween_Enter, &Tween_Update, nullptr };

// ---------------------------------------------------------------------------
// Modify Property (arithmetic on any numeric target)
// ---------------------------------------------------------------------------

void Modify_Enter(const void* data, void* state, FsmContext& ctx)
{
    const auto* d = static_cast<const FsmModifyPropertyAction*>(data);
    auto* s = static_cast<BoundState*>(state);
    BindOrFail(ctx, d->target, d->operand, "Modify Property", s);
    if (!s->bound)
        return;

    if (static_cast<DekiPropertyType>(s->binding.info->type) == DekiPropertyType::String)
    {
        ctx.Fail("Modify Property: arithmetic on a String field");
        s->bound = 0;
    }
}

int Modify_Update(const void* data, void* state, FsmContext& ctx)
{
    const auto* d = static_cast<const FsmModifyPropertyAction*>(data);
    auto* s = static_cast<BoundState*>(state);
    if (!s->bound)
        return kDone;   // FSM latched in onEnter

    const double cur = ReadBoundProperty(s->binding);
    const double rhs = s->binding.number;
    double next = cur;
    switch (d->operation)
    {
        case FsmMathOp::Add:      next = cur + rhs; break;
        case FsmMathOp::Subtract: next = cur - rhs; break;
        case FsmMathOp::Multiply: next = cur * rhs; break;
        case FsmMathOp::Divide:
            if (rhs == 0.0)
            {
                ctx.Fail("Modify Property: divide by zero");
                return kDone;
            }
            next = cur / rhs;
            break;
        case FsmMathOp::Min:      next = cur < rhs ? cur : rhs; break;
        case FsmMathOp::Max:      next = cur > rhs ? cur : rhs; break;
    }

    // Vector2 targets apply the same operation to both axes.
    double next2 = 0.0;
    if (static_cast<DekiPropertyType>(s->binding.info->type) == DekiPropertyType::Vector2)
    {
        const double cur2 = ReadBoundProperty2(s->binding);
        const double rhs2 = s->binding.number2;
        switch (d->operation)
        {
            case FsmMathOp::Add:      next2 = cur2 + rhs2; break;
            case FsmMathOp::Subtract: next2 = cur2 - rhs2; break;
            case FsmMathOp::Multiply: next2 = cur2 * rhs2; break;
            case FsmMathOp::Divide:
                if (rhs2 == 0.0)
                {
                    ctx.Fail("Modify Property: divide by zero");
                    return kDone;
                }
                next2 = cur2 / rhs2;
                break;
            case FsmMathOp::Min:      next2 = cur2 < rhs2 ? cur2 : rhs2; break;
            case FsmMathOp::Max:      next2 = cur2 > rhs2 ? cur2 : rhs2; break;
        }
    }

    WriteBoundNumbers(s->binding, next, next2);
    return d->everyFrame ? kFsmActionRunning : kDone;
}

const FsmActionOps kModifyOps = { sizeof(BoundState), &Modify_Enter, &Modify_Update, nullptr };

// ---------------------------------------------------------------------------
// Random Property
// ---------------------------------------------------------------------------

void Random_Enter(const void* data, void* state, FsmContext& ctx)
{
    const auto* d = static_cast<const FsmRandomPropertyAction*>(data);
    auto* s = static_cast<BoundState*>(state);
    // No literal to parse: the value comes from the range, not from text.
    if (!BindRef(ctx, d->target, "Random Property", s->binding))
        return;   // FSM latched

    if (static_cast<DekiPropertyType>(s->binding.info->type) == DekiPropertyType::String)
    {
        ctx.Fail("Random Property: cannot write a random number into a String field");
        return;
    }
    s->bound = 1;
}

int Random_Update(const void* data, void* state, FsmContext& /*ctx*/)
{
    const auto* d = static_cast<const FsmRandomPropertyAction*>(data);
    auto* s = static_cast<BoundState*>(state);
    if (!s->bound)
        return kDone;

    auto roll = [&]() {
        const double u = static_cast<double>(std::rand()) / (static_cast<double>(RAND_MAX) + 1.0);
        double v = d->min + u * (static_cast<double>(d->max) - static_cast<double>(d->min));
        if (d->wholeNumbers)
        {
            // Inclusive of both ends for whole numbers, which is what "1 to 5"
            // means to someone authoring a die roll.
            const double lo = d->min < d->max ? d->min : d->max;
            const double hi = d->min < d->max ? d->max : d->min;
            v = lo + std::floor(u * (hi - lo + 1.0));
            if (v > hi) v = hi;
        }
        return v;
    };

    const bool isVec2 =
        static_cast<DekiPropertyType>(s->binding.info->type) == DekiPropertyType::Vector2;
    WriteBoundNumbers(s->binding, roll(), isVec2 ? roll() : 0.0);
    return kDone;
}

const FsmActionOps kRandomOps = { sizeof(BoundState), &Random_Enter, &Random_Update, nullptr };

// ---------------------------------------------------------------------------
// Spawn Scene
// ---------------------------------------------------------------------------

int Spawn_Update(const void* data, void* /*state*/, FsmContext& ctx)
{
    const auto* d = static_cast<const FsmSpawnSceneAction*>(data);

    Scene* source = const_cast<Deki::AssetRef<Scene>&>(d->scene).Get();
    if (!source)
    {
        ctx.Fail("Spawn Scene: no scene assigned (or it failed to load)");
        return kDone;
    }

    DekiObject* owner = ctx.owner;
    Scene* into = owner ? owner->GetOwnerScene() : nullptr;
    if (!into)
    {
        ctx.Fail("Spawn Scene: the FSM's object is not in a running scene");
        return kDone;
    }

    float px = d->x;
    float py = d->y;
    if (d->relative && owner)
    {
        px += owner->GetX();
        py += owner->GetY();
    }

    DekiObject* spawned = source->Instantiate(into, px, py);
    if (!spawned)
    {
        ctx.Fail("Spawn Scene: instantiate failed");
        return kDone;
    }
    if (!d->spawnedName.empty())
        spawned->SetName(d->spawnedName);
    return kDone;
}

const FsmActionOps kSpawnOps = { 0, nullptr, &Spawn_Update, nullptr };

// ---------------------------------------------------------------------------
// Destroy Object
// ---------------------------------------------------------------------------

int Destroy_Update(const void* data, void* /*state*/, FsmContext& ctx)
{
    const auto* d = static_cast<const FsmDestroyObjectAction*>(data);

    DekiObject* target = ctx.ResolveTarget(d->targetObject);
    if (!target)
        return kDone;   // FSM latched

    Scene* owner = target->GetOwnerScene();
    if (!owner)
    {
        ctx.Fail("Destroy Object: the target is not in a running scene");
        return kDone;
    }
    owner->RemoveObject(target);
    return kDone;
}

const FsmActionOps kDestroyOps = { 0, nullptr, &Destroy_Update, nullptr };

// ---------------------------------------------------------------------------
// Set Parent
// ---------------------------------------------------------------------------

int SetParent_Update(const void* data, void* /*state*/, FsmContext& ctx)
{
    const auto* d = static_cast<const FsmSetParentAction*>(data);

    DekiObject* target = ctx.ResolveTarget(d->targetObject);
    if (!target)
        return kDone;   // FSM latched

    // An empty new parent means the scene root, so it is resolved separately
    // from ResolveTarget (where empty means "the FSM's own object").
    DekiObject* parent = nullptr;
    if (!d->newParent.empty())
    {
        parent = ctx.ResolveTarget(d->newParent);
        if (!parent)
            return kDone;   // FSM latched
    }
    target->SetParent(parent);
    return kDone;
}

const FsmActionOps kSetParentOps = { 0, nullptr, &SetParent_Update, nullptr };

// ---------------------------------------------------------------------------
// Play Animation
// ---------------------------------------------------------------------------

struct PlayAnimState { AnimationComponent* anim; };

void PlayAnim_Enter(const void* data, void* state, FsmContext& ctx)
{
    const auto* d = static_cast<const FsmPlayAnimationAction*>(data);
    auto* s = static_cast<PlayAnimState*>(state);

    DekiObject* target = ctx.ResolveTarget(d->targetObject);
    if (!target)
        return;   // FSM latched

    AnimationComponent* anim = target->GetComponent<AnimationComponent>();
    if (!anim)
    {
        char buf[192];
        std::snprintf(buf, sizeof(buf), "Play Animation: object '%s' has no AnimationComponent",
                      target->GetName().c_str());
        ctx.Fail(buf);
        return;
    }

    anim->currentSequence = d->sequence;
    anim->playOnceOverride = !d->loop;
    anim->hasFinished = false;
    anim->Play(/*restart_if_playing*/ true);
    s->anim = anim;
}

int PlayAnim_Update(const void* data, void* state, FsmContext& /*ctx*/)
{
    const auto* d = static_cast<const FsmPlayAnimationAction*>(data);
    auto* s = static_cast<PlayAnimState*>(state);
    if (!s->anim)
        return kDone;   // FSM latched in onEnter
    if (!d->waitForFinish)
        return kDone;   // fire and forget: the animation keeps running
    return (s->anim->hasFinished || !s->anim->isPlaying) ? kDone : kFsmActionRunning;
}

const FsmActionOps kPlayAnimOps = { sizeof(PlayAnimState), &PlayAnim_Enter, &PlayAnim_Update, nullptr };

// ---------------------------------------------------------------------------
// Send Event To
// ---------------------------------------------------------------------------

int SendEventTo_Update(const void* data, void* /*state*/, FsmContext& ctx)
{
    const auto* d = static_cast<const FsmSendEventToAction*>(data);

    if (d->eventName.empty())
    {
        ctx.Fail("Send Event To action has an empty event name");
        return kDone;
    }

    DekiObject* target = ctx.ResolveTarget(d->targetObject);
    if (!target)
        return kDone;   // FSM latched

    FsmComponent* fsm = target->GetComponent<FsmComponent>();
    if (!fsm)
    {
        char buf[192];
        std::snprintf(buf, sizeof(buf), "Send Event To: object '%s' has no FsmComponent",
                      target->GetName().c_str());
        ctx.Fail(buf);
        return kDone;
    }
    fsm->SendEvent(d->eventName);
    return kDone;
}

const FsmActionOps kSendEventToOps = { 0, nullptr, &SendEventTo_Update, nullptr };

// ---------------------------------------------------------------------------
// Log
// ---------------------------------------------------------------------------

int Log_Update(const void* data, void* /*state*/, FsmContext& ctx)
{
    const auto* d = static_cast<const FsmLogAction*>(data);
    DEKI_LOG_INFO("FSM (%s): %s",
                  ctx.owner ? ctx.owner->GetName().c_str() : "?", d->message.c_str());
    return kDone;
}

const FsmActionOps kLogOps = { 0, nullptr, &Log_Update, nullptr };

// ---------------------------------------------------------------------------
// Watch Button
// ---------------------------------------------------------------------------

// The clicked flag is owned by the FsmComponent's watch map for the component's
// whole life (never erased), so caching the raw pointer here is safe and turns
// the per-frame path into a single bool read.
struct WatchButtonState { bool* clicked; };

void WatchButton_Enter(const void* data, void* state, FsmContext& ctx)
{
    const auto* d = static_cast<const FsmWatchButtonAction*>(data);
    auto* s = static_cast<WatchButtonState*>(state);

    DekiObject* target = ctx.ResolveTarget(d->buttonObject);
    if (!target)
        return;   // FSM latched

    ButtonComponent* button = target->GetComponent<ButtonComponent>();
    if (!button)
    {
        char buf[192];
        std::snprintf(buf, sizeof(buf), "Watch Button: object '%s' has no ButtonComponent",
                      target->GetName().c_str());
        ctx.Fail(buf);
        return;
    }

    // Registered once per FSM + action instance, so clicks are never
    // double-subscribed across state re-entries.
    s->clicked = ctx.fsm->EnsureClickWatch(data, button).get();
}

int WatchButton_Update(const void* /*data*/, void* state, FsmContext& /*ctx*/)
{
    auto* s = static_cast<WatchButtonState*>(state);
    if (!s->clicked)
        return kDone;   // FSM latched in onEnter

    if (*s->clicked)
    {
        *s->clicked = false;
        return kDone;   // leave down "clicked"
    }
    return kFsmActionRunning;   // keeps watching while the state is active
}

const FsmActionOps kWatchButtonOps = { sizeof(WatchButtonState), &WatchButton_Enter,
                                      &WatchButton_Update, nullptr };

} // namespace

// ---------------------------------------------------------------------------
// Registration (typeId = hash of the node name, as stored by the graph loader)
// ---------------------------------------------------------------------------

REGISTER_FSM_ACTION(FsmWaitAction, kWaitOps);
REGISTER_FSM_ACTION(FsmSendEventAction, kSendEventOps);
REGISTER_FSM_ACTION(FsmSetPropertyAction, kSetPropertyOps);
REGISTER_FSM_ACTION(FsmComparePropertyAction, kCompareOps);
REGISTER_FSM_ACTION(FsmModifyPropertyAction, kModifyOps);
REGISTER_FSM_ACTION(FsmRandomPropertyAction, kRandomOps);
REGISTER_FSM_ACTION(FsmTweenPropertyAction, kTweenOps);
REGISTER_FSM_ACTION(FsmSpawnSceneAction, kSpawnOps);
REGISTER_FSM_ACTION(FsmDestroyObjectAction, kDestroyOps);
REGISTER_FSM_ACTION(FsmSetParentAction, kSetParentOps);
REGISTER_FSM_ACTION(FsmPlayAnimationAction, kPlayAnimOps);
REGISTER_FSM_ACTION(FsmSendEventToAction, kSendEventToOps);
REGISTER_FSM_ACTION(FsmLogAction, kLogOps);
REGISTER_FSM_ACTION(FsmWatchButtonAction, kWatchButtonOps);
