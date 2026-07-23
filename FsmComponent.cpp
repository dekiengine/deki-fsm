#include "FsmComponent.h"
#include "FsmNodes.h"

#include "deki-2d/ButtonComponent.h"

#include "DekiLogSystem.h"
#include "DekiTime.h"
#include "Prefab.h"

#include <cstring>

namespace
{
    constexpr uint32_t kStartId  = DekiHashString("FsmStart");
    constexpr uint32_t kStateId  = DekiHashString("FsmState");
    constexpr uint32_t kUpdateId = DekiHashString("FsmUpdate");

    // Event-transition storm guard: a graph whose states hand an event around
    // in a cycle would otherwise spin forever within one frame.
    constexpr int kMaxTransitionsPerFrame = 16;

    const char* kFinishedEvent = "FINISHED";

    const std::string kEmptyName;
}

// ============================================================================
// FsmContext helpers (declared in FsmActionRegistry.h)
// ============================================================================

void FsmContext::SendEvent(const std::string& name)
{
    if (fsm) fsm->SendEvent(name);
}

DekiObject* FsmContext::ResolveTarget(const std::string& name)
{
    return fsm ? fsm->ResolveTargetObject(name) : nullptr;
}

void FsmContext::Fail(const char* message)
{
    if (fsm) fsm->FailFsm(message);
}

// ============================================================================
// Lifecycle
// ============================================================================

void FsmComponent::Awake()
{
    SetNeedsUpdate(true);
}

void FsmComponent::Update()
{
    FsmGraph* g = graph.Get();
    if (!g || !g->data)
        return;   // no graph assigned -> nothing to run (not an error)

    // Asset reloaded or reassigned: drop the latched failure + machine state.
    if (g != lastGraph_)
    {
        lastGraph_ = g;
        ResetMachine();
    }
    if (failed_)
        return;

    transitionsThisFrame_ = 0;

    if (!active_)
    {
        EnterInitialState(*g->data);
        if (failed_ || !active_)
            return;
    }

    // Events queued between frames (external SendEvent callers, click
    // callbacks that fired after last frame's pass).
    ProcessEvents(*g->data);
    if (failed_ || !active_)
        return;

    RunActions();
    if (failed_)
        return;

    // Events raised by this frame's actions (including FINISHED).
    ProcessEvents(*g->data);
}

// ============================================================================
// Public API
// ============================================================================

void FsmComponent::SendEvent(const std::string& name)
{
    if (name.empty())
    {
        FailFsm("SendEvent with an empty event name");
        return;
    }
    eventQueue_.push_back(name);
}

const std::string& FsmComponent::ActiveStateName() const
{
    if (active_ && active_->typeId == kStateId)
        return static_cast<const FsmStateNode*>(active_->instance)->name;
    return kEmptyName;
}

void FsmComponent::FailFsm(const char* message)
{
    if (failed_)
        return;   // one error per latch
    DEKI_LOG_ERROR("FsmComponent (%s): %s — machine stopped",
                   GetOwner() ? GetOwner()->GetName().c_str() : "?",
                   message ? message : "unknown error");
    failed_ = true;
}

DekiObject* FsmComponent::ResolveTargetObject(const std::string& name)
{
    DekiObject* owner = GetOwner();
    if (name.empty())
        return owner;

    if (owner && owner->GetOwnerPrefab())
    {
        for (DekiObject* obj : owner->GetOwnerPrefab()->GetObjects())
        {
            if (obj && obj->GetName() == name)
                return obj;
        }
    }

    char buf[192];
    std::snprintf(buf, sizeof(buf), "target object '%s' not found in prefab", name.c_str());
    FailFsm(buf);
    return nullptr;
}

std::shared_ptr<bool> FsmComponent::EnsureClickWatch(const void* key, ButtonComponent* button)
{
    auto it = clickWatches_.find(key);
    if (it != clickWatches_.end())
        return it->second;

    auto flag = std::make_shared<bool>(false);
    clickWatches_[key] = flag;
    if (button)
        button->AddOnClickCallback([flag]() { *flag = true; });
    return flag;
}

// ============================================================================
// Machine internals
// ============================================================================

void FsmComponent::ResetMachine()
{
    // Hard drop, deliberately WITHOUT running onExit: this path fires when the
    // graph asset was reloaded or reassigned, so active_ may point into freed
    // NodeGraphData — touching it would be use-after-free.
    active_ = nullptr;
    ops_.clear();
    stateOffsets_.clear();
    stateBlob_.clear();
    finishedLatch_.clear();
    finishedFired_ = false;
    failed_ = false;
    eventQueue_.clear();
    clickWatches_.clear();   // callbacks on buttons keep their (now orphan) flags alive
    updateActions_.clear();
    updateBlob_.clear();
}

void FsmComponent::BuildUpdateStacks(const NodeGraphData& g)
{
    // Collect every FsmUpdateNode's enabled actions into one persistent table.
    // Built once per graph (with the initial state), never exited: this is the
    // machine's Update() lifecycle, running alongside whichever state is active.
    updateActions_.clear();
    updateBlob_.clear();

    size_t blobSize = 0;
    for (const auto& node : g.Nodes())
    {
        if (node.typeId != kUpdateId)
            continue;
        for (const auto& child : node.children)
        {
            if (!child.enabled)
                continue;
            const FsmActionOps* ops = FsmActionRegistry::Instance().Find(child.typeId);
            if (!ops)
            {
                FailFsm("Update stack contains an action with no registered runtime ops");
                return;
            }
            UpdateActionSlot slot;
            slot.data = child.instance;
            slot.ops = ops;
            slot.stateOffset = blobSize;
            updateActions_.push_back(slot);
            blobSize += ops->stateSize;
        }
    }
    updateBlob_.assign(blobSize, 0);

    FsmContext ctx{ GetOwner(), this, 0.0f };
    for (auto& slot : updateActions_)
    {
        if (slot.ops->onEnter)
            slot.ops->onEnter(slot.data, updateBlob_.data() + slot.stateOffset, ctx);
        if (failed_)
            return;
    }
}

void FsmComponent::EnterInitialState(const NodeGraphData& g)
{
    BuildUpdateStacks(g);
    if (failed_)
        return;

    const NodeGraphData::NodeInstance* start = g.FindFirstOfType(kStartId);
    if (!start)
    {
        FailFsm("graph has no Start node");
        return;
    }
    const NodeGraphData::NodeInstance* first = g.Next(start->id, 0);
    if (!first)
    {
        FailFsm("Start node's output is not wired to a state");
        return;
    }
    EnterState(g, first);
}

void FsmComponent::EnterState(const NodeGraphData& g, const NodeGraphData::NodeInstance* state)
{
    if (state->typeId != kStateId)
    {
        FailFsm("a wire leads to a node that is not a State");
        return;
    }

    ExitState();
    active_ = state;

    // Build the per-entry action table: ops + a zeroed runtime-state blob,
    // index-aligned with the state's children.
    const size_t count = state->children.size();
    ops_.assign(count, nullptr);
    stateOffsets_.assign(count, 0);
    finishedLatch_.assign(count, 0);
    finishedFired_ = false;

    size_t blobSize = 0;
    for (size_t i = 0; i < count; ++i)
    {
        const auto& child = state->children[i];
        if (!child.enabled)
            continue;
        const FsmActionOps* ops = FsmActionRegistry::Instance().Find(child.typeId);
        if (!ops)
        {
            FailFsm("state contains an action with no registered runtime ops");
            active_ = nullptr;
            return;
        }
        ops_[i] = ops;
        stateOffsets_[i] = blobSize;
        blobSize += ops->stateSize;
    }
    stateBlob_.assign(blobSize, 0);

    FsmContext ctx{ GetOwner(), this, 0.0f };
    for (size_t i = 0; i < count; ++i)
    {
        if (!ops_[i] || !ops_[i]->onEnter)
            continue;
        ops_[i]->onEnter(state->children[i].instance, stateBlob_.data() + stateOffsets_[i], ctx);
        if (failed_)
            return;
    }
}

void FsmComponent::ExitState()
{
    if (!active_)
        return;

    FsmContext ctx{ GetOwner(), this, 0.0f };
    for (size_t i = 0; i < ops_.size(); ++i)
    {
        if (ops_[i] && ops_[i]->onExit)
            ops_[i]->onExit(active_->children[i].instance,
                            stateBlob_.data() + stateOffsets_[i], ctx);
    }

    active_ = nullptr;
    ops_.clear();
    stateOffsets_.clear();
    stateBlob_.clear();
    finishedLatch_.clear();
    finishedFired_ = false;
}

void FsmComponent::ProcessEvents(const NodeGraphData& g)
{
    while (!eventQueue_.empty() && !failed_ && active_)
    {
        const std::string ev = eventQueue_.front();
        eventQueue_.erase(eventQueue_.begin());

        const auto* state = static_cast<const FsmStateNode*>(active_->instance);
        int pin = -1;
        for (size_t i = 0; i < state->transitions.size(); ++i)
        {
            if (state->transitions[i] == ev)
            {
                pin = static_cast<int>(i);
                break;
            }
        }
        if (pin < 0)
        {
            // Not listened for in this state: dropped by design.
            DEKI_LOG_DEBUG("FsmComponent: event '%s' ignored by state '%s'",
                           ev.c_str(), state->name.c_str());
            continue;
        }

        const NodeGraphData::NodeInstance* next = g.Next(active_->id, pin);
        if (!next)
        {
            char buf[192];
            std::snprintf(buf, sizeof(buf), "state '%s' transition '%s' is not wired",
                          state->name.c_str(), ev.c_str());
            FailFsm(buf);
            return;
        }

        if (++transitionsThisFrame_ > kMaxTransitionsPerFrame)
        {
            FailFsm("more than 16 transitions in one frame (event cycle?)");
            return;
        }
        EnterState(g, next);
    }
}

void FsmComponent::RunActions()
{
    const float dt = DekiTime::GetDeltaTimeF() * 0.001f;
    FsmContext ctx{ GetOwner(), this, dt };

    // Update stacks first: global watchers observe the frame before the active
    // state's actions mutate it. Finished slots just stop (no FINISHED event).
    for (auto& slot : updateActions_)
    {
        if (slot.finished)
            continue;
        if (!slot.ops->onUpdate)
        {
            slot.finished = 1;   // enter-only action
            continue;
        }
        if (slot.ops->onUpdate(slot.data, updateBlob_.data() + slot.stateOffset, ctx))
            slot.finished = 1;
        if (failed_)
            return;
    }

    bool allFinished = true;
    for (size_t i = 0; i < ops_.size() && active_; ++i)
    {
        if (!ops_[i])
            continue;   // disabled child
        if (finishedLatch_[i])
            continue;
        if (!ops_[i]->onUpdate)
        {
            finishedLatch_[i] = 1;   // enter-only action
            continue;
        }
        if (ops_[i]->onUpdate(active_->children[i].instance,
                              stateBlob_.data() + stateOffsets_[i], ctx))
            finishedLatch_[i] = 1;
        else
            allFinished = false;
        if (failed_)
            return;
    }

    if (active_ && allFinished && !finishedFired_)
    {
        finishedFired_ = true;
        eventQueue_.push_back(kFinishedEvent);
    }
}
