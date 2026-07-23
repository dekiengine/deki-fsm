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
    constexpr uint32_t kAwakeId  = DekiHashString("FsmAwake");
    constexpr uint32_t kUpdateId = DekiHashString("FsmUpdate");

    // Event-transition storm guard (machine-wide): a graph whose states hand
    // an event around in a cycle would otherwise spin forever within a frame.
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

    if (!initialized_)
    {
        InitializeMachine(*g->data);
        if (failed_)
            return;
    }

    // Events queued between frames (external SendEvent callers, click
    // callbacks, events raised during initialization by Awake actions).
    ProcessEvents(*g->data);
    if (failed_)
        return;

    RunActions();
    if (failed_)
        return;

    // Events raised by this frame's actions (including per-track FINISHED).
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
    eventQueue_.push_back({ name, -1 });
}

const std::string& FsmComponent::ActiveStateName() const
{
    if (!tracks_.empty() && tracks_[0].active && tracks_[0].active->typeId == kStateId)
        return static_cast<const FsmStateNode*>(tracks_[0].active->instance)->name;
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
    // graph asset was reloaded or reassigned, so track state may point into
    // freed NodeGraphData — touching it would be use-after-free.
    tracks_.clear();
    initialized_ = false;
    failed_ = false;
    eventQueue_.clear();
    clickWatches_.clear();   // callbacks on buttons keep their (now orphan) flags alive
    updateActions_.clear();
    updateBlob_.clear();
}

void FsmComponent::RunAwakeStacks(const NodeGraphData& g)
{
    // Awake is instantaneous, like DekiBehaviour::Awake(): every enabled
    // action of every FsmAwakeNode runs in ONE pass (enter -> one update ->
    // exit, dt 0) right now, before the Update stacks arm and before any
    // track enters its first state. An action that does not finish in that
    // single pass is frame-based (Wait, Move To, Watch Button, everyFrame)
    // and does not belong in Awake — that is a graph error, not a partial run.
    FsmContext ctx{ GetOwner(), this, 0.0f };
    std::vector<uint8_t> state;   // per-action scratch, discarded after the pass

    for (const auto& node : g.Nodes())
    {
        if (node.typeId != kAwakeId)
            continue;
        for (const auto& child : node.children)
        {
            if (!child.enabled)
                continue;
            const FsmActionOps* ops = FsmActionRegistry::Instance().Find(child.typeId);
            if (!ops)
            {
                FailFsm("Awake stack contains an action with no registered runtime ops");
                return;
            }

            state.assign(ops->stateSize, 0);
            if (ops->onEnter)
                ops->onEnter(child.instance, state.data(), ctx);
            if (failed_)
                return;

            bool finished = true;
            if (ops->onUpdate)
                finished = ops->onUpdate(child.instance, state.data(), ctx);
            if (failed_)
                return;
            if (!finished)
            {
                FailFsm("Awake action did not finish immediately — frame-based "
                        "actions (Wait, Move To, Watch Button, everyFrame) belong "
                        "in a state or an Update stack, not Awake");
                return;
            }

            if (ops->onExit)
                ops->onExit(child.instance, state.data(), ctx);
            if (failed_)
                return;
        }
    }
}

void FsmComponent::BuildUpdateStacks(const NodeGraphData& g)
{
    // Collect every FsmUpdateNode's enabled actions into one persistent table.
    // Built once per graph, never exited: this is the machine's Update()
    // lifecycle, running alongside every track.
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

void FsmComponent::InitializeMachine(const NodeGraphData& g)
{
    // Script lifecycle order: Awake pass -> Update stacks arm -> every wired
    // lifecycle root output begins its parallel track (in graph node order).
    // Events queued by Awake actions are processed right after, so Awake can
    // redirect any track on frame one.
    initialized_ = true;

    RunAwakeStacks(g);
    if (failed_)
        return;

    BuildUpdateStacks(g);
    if (failed_)
        return;

    for (const auto& node : g.Nodes())
    {
        if (failed_)
            return;

        if (node.typeId == kStartId)
        {
            // Start exists solely to point at a state; unwired = authoring error.
            const NodeGraphData::NodeInstance* first = g.Next(node.id, 0);
            if (!first)
            {
                FailFsm("Start node's output is not wired to a state");
                return;
            }
            tracks_.emplace_back();
            EnterState(g, tracks_.back(), first);
        }
        else if (node.typeId == kAwakeId || node.typeId == kUpdateId)
        {
            // Optional: these nodes carry their lifecycle stacks either way;
            // a wired output additionally starts a parallel flow.
            const NodeGraphData::NodeInstance* first = g.Next(node.id, 0);
            if (first)
            {
                tracks_.emplace_back();
                EnterState(g, tracks_.back(), first);
            }
        }
    }

    if (tracks_.empty() && updateActions_.empty())
        FailFsm("graph has nothing to run: no wired Start/Awake/Update output "
                "and no Update actions");
}

void FsmComponent::EnterState(const NodeGraphData& g, Track& track,
                              const NodeGraphData::NodeInstance* state)
{
    if (state->typeId != kStateId)
    {
        FailFsm("a wire leads to a node that is not a State");
        return;
    }

    ExitState(track);
    track.active = state;

    // Build the per-entry action table: ops + a zeroed runtime-state blob,
    // index-aligned with the state's children.
    const size_t count = state->children.size();
    track.ops.assign(count, nullptr);
    track.stateOffsets.assign(count, 0);
    track.finishedLatch.assign(count, 0);
    track.finishedFired = false;

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
            track.active = nullptr;
            return;
        }
        track.ops[i] = ops;
        track.stateOffsets[i] = blobSize;
        blobSize += ops->stateSize;
    }
    track.stateBlob.assign(blobSize, 0);

    FsmContext ctx{ GetOwner(), this, 0.0f };
    for (size_t i = 0; i < count; ++i)
    {
        if (!track.ops[i] || !track.ops[i]->onEnter)
            continue;
        track.ops[i]->onEnter(state->children[i].instance,
                              track.stateBlob.data() + track.stateOffsets[i], ctx);
        if (failed_)
            return;
    }
}

void FsmComponent::ExitState(Track& track)
{
    if (!track.active)
        return;

    FsmContext ctx{ GetOwner(), this, 0.0f };
    for (size_t i = 0; i < track.ops.size(); ++i)
    {
        if (track.ops[i] && track.ops[i]->onExit)
            track.ops[i]->onExit(track.active->children[i].instance,
                                 track.stateBlob.data() + track.stateOffsets[i], ctx);
    }

    track.active = nullptr;
    track.ops.clear();
    track.stateOffsets.clear();
    track.stateBlob.clear();
    track.finishedLatch.clear();
    track.finishedFired = false;
}

void FsmComponent::ProcessEvents(const NodeGraphData& g)
{
    while (!eventQueue_.empty() && !failed_)
    {
        const QueuedEvent ev = eventQueue_.front();
        eventQueue_.erase(eventQueue_.begin());

        // Broadcast events are offered to every track (each may transition on
        // it independently); track-scoped events (FINISHED) only to their own.
        for (size_t t = 0; t < tracks_.size() && !failed_; ++t)
        {
            if (ev.track >= 0 && static_cast<int>(t) != ev.track)
                continue;

            Track& track = tracks_[t];
            if (!track.active)
                continue;

            const auto* state = static_cast<const FsmStateNode*>(track.active->instance);
            int pin = -1;
            for (size_t i = 0; i < state->transitions.size(); ++i)
            {
                if (state->transitions[i] == ev.name)
                {
                    pin = static_cast<int>(i);
                    break;
                }
            }
            if (pin < 0)
            {
                // Not listened for here. For a track's own FINISHED that just
                // means a terminal state (the track parks — by design).
                if (ev.track >= 0)
                    DEKI_LOG_DEBUG("FsmComponent: event '%s' ignored by state '%s'",
                                   ev.name.c_str(), state->name.c_str());
                continue;
            }

            const NodeGraphData::NodeInstance* next = g.Next(track.active->id, pin);
            if (!next)
            {
                char buf[192];
                std::snprintf(buf, sizeof(buf), "state '%s' transition '%s' is not wired",
                              state->name.c_str(), ev.name.c_str());
                FailFsm(buf);
                return;
            }

            if (++transitionsThisFrame_ > kMaxTransitionsPerFrame)
            {
                FailFsm("more than 16 transitions in one frame (event cycle?)");
                return;
            }
            EnterState(g, track, next);
        }
    }
}

void FsmComponent::RunActions()
{
    const float dt = DekiTime::GetDeltaTimeF() * 0.001f;
    FsmContext ctx{ GetOwner(), this, dt };

    // Update stacks first: global watchers observe the frame before any
    // track's actions mutate it. Finished slots just stop (no FINISHED event).
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

    // Then every track's active state, each with its own FINISHED.
    for (size_t t = 0; t < tracks_.size(); ++t)
    {
        Track& track = tracks_[t];
        if (!track.active)
            continue;

        bool allFinished = true;
        for (size_t i = 0; i < track.ops.size() && track.active; ++i)
        {
            if (!track.ops[i])
                continue;   // disabled child
            if (track.finishedLatch[i])
                continue;
            if (!track.ops[i]->onUpdate)
            {
                track.finishedLatch[i] = 1;   // enter-only action
                continue;
            }
            if (track.ops[i]->onUpdate(track.active->children[i].instance,
                                       track.stateBlob.data() + track.stateOffsets[i], ctx))
                track.finishedLatch[i] = 1;
            else
                allFinished = false;
            if (failed_)
                return;
        }

        if (track.active && allFinished && !track.finishedFired)
        {
            track.finishedFired = true;
            eventQueue_.push_back({ kFinishedEvent, static_cast<int>(t) });
        }
    }
}
