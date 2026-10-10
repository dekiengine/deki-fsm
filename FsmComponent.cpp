#include "FsmComponent.h"
#include <utility>
#include "FsmNodes.h"

#include "deki-2d/ButtonComponent.h"

#include <deki/LogSystem.h>
#include <deki/Time.h>
#include <deki/Scene.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace DekiFsm
{

namespace
{
constexpr uint32_t kStartId = Deki::HashString("FsmStart");
constexpr uint32_t kStateId = Deki::HashString("FsmState");
constexpr uint32_t kAwakeId = Deki::HashString("FsmAwake");
constexpr uint32_t kUpdateId = Deki::HashString("FsmUpdate");

// A state's action flow starts at this node; groups are entered and left
// through theirs. All three are pure wiring: they carry no behavior and are
// never handed to the action registry.
constexpr uint32_t kActionEntryId = Deki::HashString("FsmActionEntry");
constexpr uint32_t kGroupId = Deki::HashString("FsmGroup");
constexpr uint32_t kGroupInId = Deki::HashString("FsmGroupIn");
constexpr uint32_t kGroupExitId = Deki::HashString("FsmGroupExit");

// Variables. The runtime matches these by type id and casts to the concrete
// struct. DekiNodeGraph::DekiNodeMeta and NodeTypeRegistry are editor-only, so
// nothing here may use reflection (the DEKI_NODE_VARIABLES marker is for the
// editor's pickers).
constexpr uint32_t kVariablesId = Deki::HashString("FsmVariables");
constexpr uint32_t kNumberVarId = Deki::HashString("FsmNumberVar");
constexpr uint32_t kBoolVarId = Deki::HashString("FsmBoolVar");
constexpr uint32_t kTextVarId = Deki::HashString("FsmTextVar");

// Limit on transitions per frame, for the whole machine: states that pass an
// event around in a cycle would otherwise spin forever within a frame.
constexpr int kMaxTransitionsPerFrame = 16;

// The same one level down: an action flow may loop, so a ring of instant
// actions would otherwise never end the frame.
constexpr int kMaxActionStepsPerFrame = 256;

// The most one frame advances a machine. A longer frame (a scene loading,
// the board sending a screenshot, waking from sleep) counts as this much, so
// a ring of short timed actions cannot run past kMaxActionStepsPerFrame.
constexpr float kMaxFrameSeconds = 1.0f;

// Groups and exits are resolved by walking, and a group's In can lead into
// another group, and so on; this bounds that walk.
constexpr int kMaxFlowHops = 32;

const char* const kFinishedEvent = "FINISHED";

const std::string kEmptyName;

// Largest per-run state any action in this graph asks for, inner graphs
// included (a state's action flow, a group's contents, to any depth).
//
// Measured once, so every track can hold one slot that fits whatever it ends
// up running, and a state change never allocates. Only one action per track
// is live at a time, so one slot is enough.
size_t MaxActionState(const DekiNodeGraph::NodeGraphData::Graph& graph)
{
    size_t maxSize = 0;
    for (const auto& node : graph.nodes)
    {
        if (const FsmActionOps* ops = FsmActionRegistry::Instance().Find(node.typeId))
        {
            if (ops->stateSize > maxSize)
            {
                maxSize = ops->stateSize;
            }
        }
        if (node.inner)
        {
            const size_t inner = MaxActionState(*node.inner);
            if (inner > maxSize)
            {
                maxSize = inner;
            }
        }
    }
    return maxSize;
}
}  // namespace

// ============================================================================
// FsmContext helpers (declared in FsmActionRegistry.h)
// ============================================================================

void FsmContext::SendEvent(const std::string& name)
{
    if (fsm)
    {
        fsm->SendEvent(name);
    }
}

Deki::Object* FsmContext::ResolveTarget(const std::string& name)
{
    return fsm ? fsm->ResolveTargetObject(name) : nullptr;
}

void FsmContext::Fail(const char* message)
{
    if (fsm)
    {
        fsm->FailFsm(message);
    }
}

// ============================================================================
// Lifecycle
// ============================================================================

void FsmComponent::Awake()
{
}

void FsmComponent::Update()
{
    FsmGraph* g = graph.Get();
    if (!g || !g->data)
    {
        return;  // no graph assigned: nothing to run, and not an error
    }

    // Asset reloaded or reassigned: drop the failure and the machine state.
    if (g != m_LastGraph)
    {
        m_LastGraph = g;
        ResetMachine();
    }
    if (m_Failed)
    {
        return;
    }

    m_TransitionsThisFrame = 0;

    if (!m_Initialized)
    {
        InitializeMachine(*g->data);
        if (m_Failed)
        {
            return;
        }
    }

    // Events queued between frames (external SendEvent callers, click
    // callbacks, events raised during initialization by Awake actions).
    ProcessEvents();
    if (m_Failed)
    {
        return;
    }

    RunActions();
    if (m_Failed)
    {
        return;
    }

    // Events raised by this frame's actions (including per-track FINISHED).
    ProcessEvents();
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
    m_EventQueue.push_back({ name, -1 });
}

const std::string& FsmComponent::ActiveStateName() const
{
    if (!m_Tracks.empty() && m_Tracks[0].active && m_Tracks[0].active->typeId == kStateId)
    {
        return static_cast<const FsmStateNode*>(m_Tracks[0].active->instance)->name;
    }
    return kEmptyName;
}

void FsmComponent::FailFsm(const char* message)
{
    if (m_Failed)
    {
        return;  // already failed: log only the first error
    }
    DEKI_LOG_ERROR("FsmComponent (%s): %s — machine stopped", GetOwner() ? GetOwner()->GetName().c_str() : "?",
                   message ? message : "unknown error");
    m_Failed = true;
}

Deki::Object* FsmComponent::ResolveTargetObject(const std::string& name)
{
    Deki::Object* owner = GetOwner();
    if (name.empty())
    {
        return owner;
    }

    // Anywhere in the scene tree, not just the root list, which in a
    // single-root scene holds nothing but Root.
    if (owner && owner->GetOwnerScene())
    {
        if (Deki::Object* found = owner->GetOwnerScene()->FindDekiObject(name))
        {
            return found;
        }
    }

    char buf[192];
    std::snprintf(buf, sizeof(buf), "target object '%s' not found in scene", name.c_str());
    FailFsm(buf);
    return nullptr;
}

std::shared_ptr<bool> FsmComponent::EnsureClickWatch(const void* key, Deki2D::ButtonComponent* button)
{
    // Linear: a machine watches one or two buttons, and the search is shorter
    // than hashing the key would be.
    for (const ClickWatch& watch : m_ClickWatches)
    {
        if (watch.key == key)
        {
            return watch.flag;
        }
    }

    auto flag = std::make_shared<bool>(false);
    m_ClickWatches.push_back({ key, flag });
    if (button)
    {
        button->AddOnClickCallback([flag]() { *flag = true; });
    }
    return flag;
}

// ============================================================================
// Machine internals
// ============================================================================

void FsmComponent::ResetMachine()
{
    // Drops everything without running onExit, on purpose: this runs when the
    // graph asset was reloaded or reassigned, so track state may point into
    // freed DekiNodeGraph::NodeGraphData.
    m_Tracks.clear();
    m_Initialized = false;
    m_Failed = false;
    m_EventQueue.clear();
    m_EventHead = 0;
    m_MaxActionState = 0;    // re-measured from the new graph on the next init
    m_ClickWatches.clear();  // button callbacks keep their now unused flags alive
    m_Variables.clear();     // declared again from the new graph on the next init
}

void FsmComponent::InitializeVariables(const DekiNodeGraph::NodeGraphData& g)
{
    m_Variables.clear();

    // The declarations live in the child stack of the Variables node. Matched by
    // type id and cast to the concrete struct: reflection metadata does not
    // exist on device, so this path must not use it.
    for (const auto& node : g.Nodes())
    {
        if (node.typeId != kVariablesId)
        {
            continue;
        }

        m_Variables.reserve(m_Variables.size() + node.children.size());
        for (const auto& child : node.children)
        {
            if (!child.enabled || !child.instance)
            {
                continue;
            }

            Variable var;
            if (child.typeId == kNumberVarId)
            {
                const auto* d = static_cast<const FsmNumberVariable*>(child.instance);
                var.nameHash = d->name.empty() ? 0u : Deki::HashString(d->name.c_str());
                var.type = Deki::PropertyType::Float;
                var.number = d->value;
            }
            else if (child.typeId == kBoolVarId)
            {
                const auto* d = static_cast<const FsmBoolVariable*>(child.instance);
                var.nameHash = d->name.empty() ? 0u : Deki::HashString(d->name.c_str());
                var.type = Deki::PropertyType::Bool;
                var.number = d->value ? 1.0f : 0.0f;
            }
            else if (child.typeId == kTextVarId)
            {
                const auto* d = static_cast<const FsmTextVariable*>(child.instance);
                var.nameHash = d->name.empty() ? 0u : Deki::HashString(d->name.c_str());
                var.type = Deki::PropertyType::String;
                var.text = d->value;
            }
            else
            {
                FailFsm("the Variables stack contains an entry that is not a variable");
                return;
            }

            if (var.nameHash == 0)
            {
                FailFsm("a variable declaration has an empty name");
                return;
            }
            m_Variables.push_back(std::move(var));
        }
    }

    ApplyVariableOverrides();
}

// This object's own starting values, over the graph's: one graph, many
// objects, each tuned in its inspector. Each is "name=value", parsed by the
// declared type. A name the graph does not declare, or a value of the wrong
// type, stops the machine, so a typo does not silently run the default.
void FsmComponent::ApplyVariableOverrides()
{
    for (const std::string& entry : variableOverrides)
    {
        const size_t eq = entry.find('=');
        const std::string name = eq == std::string::npos ? entry : entry.substr(0, eq);
        if (eq == std::string::npos || name.empty())
        {
            char buf[192];
            std::snprintf(buf, sizeof(buf), "variable override '%s' is not name=value", entry.c_str());
            FailFsm(buf);
            return;
        }
        const std::string value = entry.substr(eq + 1);
        const uint32_t hash = Deki::HashString(name.c_str());

        Variable* var = nullptr;
        for (Variable& v : m_Variables)
        {
            if (v.nameHash == hash)
            {
                var = &v;
            }
        }
        if (!var)
        {
            char buf[192];
            std::snprintf(buf, sizeof(buf), "variable override '%s': the graph declares no variable '%s'",
                          entry.c_str(), name.c_str());
            FailFsm(buf);
            return;
        }

        bool ok = true;
        if (var->type == Deki::PropertyType::String)
        {
            var->text = value;
        }
        else if (var->type == Deki::PropertyType::Bool)
        {
            ok = (value == "true" || value == "false" || value == "1" || value == "0");
            var->number = (value == "true" || value == "1") ? 1.0f : 0.0f;
        }
        else
        {
            char* end = nullptr;
            const float number = std::strtof(value.c_str(), &end);
            ok = !value.empty() && end != nullptr && *end == '\0';
            var->number = number;
        }
        if (!ok)
        {
            char buf[192];
            std::snprintf(buf, sizeof(buf), "variable override '%s': '%s' is not a value of the variable's type",
                          entry.c_str(), value.c_str());
            FailFsm(buf);
            return;
        }
    }
}

bool FsmComponent::BindVariable(const Deki::PropertyRef& ref, Deki::PropertyBinding& out)
{
    for (Variable& var : m_Variables)
    {
        if (var.nameHash != ref.fieldHash)
        {
            continue;
        }

        const Deki::FieldRef* info = DekiVariableFieldRef(var.type);
        if (!info)
        {
            FailFsm("a variable has a type that cannot be read or written");
            return false;
        }
        // Bools and ints are stored in the float slot, so the binding gives the
        // storage type (Float) rather than the declared one; comparisons and
        // arithmetic behave the same either way.
        out.info = (var.type == Deki::PropertyType::String) ? info : DekiVariableFieldRef(Deki::PropertyType::Float);
        out.field =
            (var.type == Deki::PropertyType::String) ? static_cast<void*>(&var.text) : static_cast<void*>(&var.number);
        return true;
    }

    char buf[192];
    std::snprintf(buf, sizeof(buf), "no variable named '%s' is declared in this graph", ref.field.c_str());
    FailFsm(buf);
    return false;
}

const DekiNodeGraph::NodeGraphData::NodeInstance*
FsmComponent::ResolveFlowTarget(const DekiNodeGraph::NodeGraphData::Graph*& graph,
                                const DekiNodeGraph::NodeGraphData::NodeInstance* node, Track& track)
{
    for (int hop = 0; hop < kMaxFlowHops; ++hop)
    {
        if (!node)
        {
            FailFsm("a flow wire leads nowhere");
            return nullptr;
        }

        if (node->typeId == kStateId)
        {
            return node;  // `graph` already holds the state's graph
        }

        if (node->typeId == kGroupId)
        {
            // Into the group: continue from whatever its Group In points at.
            if (!node->inner)
            {
                FailFsm("a Group has no contents");
                return nullptr;
            }
            const DekiNodeGraph::NodeGraphData::NodeInstance* in = node->inner->FindFirstOfType(kGroupInId);
            if (!in)
            {
                FailFsm("a Group has no Group In node");
                return nullptr;
            }
            const DekiNodeGraph::NodeGraphData::NodeInstance* next = node->inner->Next(in->id, 0);
            if (!next)
            {
                const auto* d = static_cast<const FsmGroupNode*>(node->instance);
                char buf[192];
                std::snprintf(buf, sizeof(buf), "group '%s' has nothing wired to its Group In", d->name.c_str());
                FailFsm(buf);
                return nullptr;
            }
            track.groups.push_back({ graph, node });
            graph = node->inner;
            node = next;
            continue;
        }

        if (node->typeId == kGroupExitId)
        {
            // Out of the group: continue from the pin of the same name, in the
            // graph the group itself lives in.
            const auto* exitData = static_cast<const FsmGroupExitNode*>(node->instance);
            if (track.groups.empty())
            {
                char buf[192];
                std::snprintf(buf, sizeof(buf), "Group Exit '%s' is not inside a group (nothing to leave)",
                              exitData->name.c_str());
                FailFsm(buf);
                return nullptr;
            }
            const Track::GroupFrame frame = track.groups.back();
            track.groups.pop_back();

            const auto* groupData = static_cast<const FsmGroupNode*>(frame.group->instance);
            int pin = -1;
            for (size_t i = 0; i < groupData->exits.size(); ++i)
            {
                if (groupData->exits[i] == exitData->name)
                {
                    pin = static_cast<int>(i);
                    break;
                }
            }
            if (pin < 0)
            {
                char buf[192];
                std::snprintf(buf, sizeof(buf), "group '%s' has no exit named '%s'", groupData->name.c_str(),
                              exitData->name.c_str());
                FailFsm(buf);
                return nullptr;
            }
            const DekiNodeGraph::NodeGraphData::NodeInstance* next = frame.graph->Next(frame.group->id, pin);
            if (!next)
            {
                char buf[192];
                std::snprintf(buf, sizeof(buf), "group '%s' exit '%s' is not wired", groupData->name.c_str(),
                              exitData->name.c_str());
                FailFsm(buf);
                return nullptr;
            }
            graph = frame.graph;
            node = next;
            continue;
        }

        FailFsm("a wire leads to a node that is not a State, a Group or a Group Exit");
        return nullptr;
    }

    FailFsm("groups nested more than 32 deep, or a cycle of groups with no state in it");
    return nullptr;
}

void FsmComponent::InitializeMachine(const DekiNodeGraph::NodeGraphData& g)
{
    // Every graph has the lifecycle entries Awake, Start and Update. Each
    // wired output starts its own parallel track, entered in lifecycle order
    // (Awake, then Start, then Update). An unwired output is an unused hook,
    // like a lifecycle method you did not override, and not an error. A
    // machine with nothing wired at all has nothing to run, and that is an
    // error.
    m_Initialized = true;

    // Variables first: an Awake-track action may bind one on its very first
    // frame, and their storage must never move afterwards.
    InitializeVariables(g);
    if (m_Failed)
    {
        return;
    }

    // Then the size every track's action-state slot needs, before any track
    // exists: EnterState runs an action the moment a track is created.
    m_MaxActionState = MaxActionState(g.Root());

    const uint32_t kLifecycleOrder[] = { kAwakeId, kStartId, kUpdateId };
    for (uint32_t entryTypeId : kLifecycleOrder)
    {
        for (const auto& node : g.Nodes())
        {
            if (node.typeId != entryTypeId)
            {
                continue;
            }
            const DekiNodeGraph::NodeGraphData::NodeInstance* first = g.Root().Next(node.id, 0);
            if (!first)
            {
                continue;  // unused hook
            }
            m_Tracks.emplace_back();
            // Allocated once, here; no state change resizes it.
            m_Tracks.back().stateBuf.assign(m_MaxActionState, 0);
            EnterState(m_Tracks.back(), &g.Root(), first);
            if (m_Failed)
            {
                return;
            }
        }
    }

    if (m_Tracks.empty())
    {
        FailFsm("graph has nothing to run: no lifecycle output (Awake/Start/Update) is wired");
    }
}

void FsmComponent::EnterState(Track& track, const DekiNodeGraph::NodeGraphData::Graph* graph,
                              const DekiNodeGraph::NodeGraphData::NodeInstance* target)
{
    // Groups are crossed here, not stored: what a track holds is always a real
    // State, whatever depth of grouping it was reached through.
    const DekiNodeGraph::NodeGraphData::Graph* stateGraph = graph;
    const DekiNodeGraph::NodeGraphData::NodeInstance* state = ResolveFlowTarget(stateGraph, target, track);
    if (!state)
    {
        return;  // machine already failed
    }

    // Older assets keep a state's actions as an inspector stack ("children")
    // rather than an inner graph. Such a state would silently do nothing, so
    // report it instead.
    if (!state->children.empty() && !state->inner)
    {
        const auto* d = static_cast<const FsmStateNode*>(state->instance);
        char buf[192];
        std::snprintf(buf, sizeof(buf),
                      "state '%s' still stores its actions as a stack; re-author its "
                      "action flow (open the state on the canvas) and save the graph",
                      d->name.c_str());
        FailFsm(buf);
        return;
    }

    ExitState(track);
    track.active = state;
    track.graph = stateGraph;
    track.actions = state->inner;
    track.finishedFired = false;

    // The action-state slot needs nothing here: it was sized for the whole
    // graph when the track was created, and BeginAction zeroes the part the
    // incoming action uses.

    // Start at whatever the Entry node points at. No Entry, or nothing wired to
    // it, is an empty flow: the state does nothing and finishes at once, which
    // is what a "wait for an event here" state looks like.
    const DekiNodeGraph::NodeGraphData::NodeInstance* first = nullptr;
    if (track.actions)
    {
        if (const DekiNodeGraph::NodeGraphData::NodeInstance* entry = track.actions->FindFirstOfType(kActionEntryId))
        {
            first = track.actions->Next(entry->id, 0);
        }
    }

    FsmContext ctx{ GetOwner(), this, 0.0f };
    BeginAction(track, first, ctx);
}

void FsmComponent::BeginAction(Track& track, const DekiNodeGraph::NodeGraphData::NodeInstance* node, FsmContext& ctx)
{
    track.current = node;
    track.currentOps = nullptr;
    if (!node)
    {
        return;  // flow ran off its end: FINISHED on the next pass
    }

    const FsmActionOps* ops = FsmActionRegistry::Instance().Find(node->typeId);
    if (!ops)
    {
        FailFsm("an action flow contains a node with no registered runtime ops");
        track.current = nullptr;
        return;
    }
    // The slot is measured from the same registry this just read, so an action
    // that does not fit means the two disagree (a graph reloaded against a
    // rebuilt action library, say). Fail rather than clamp: writing stateSize
    // bytes into a shorter slot corrupts memory somewhere else entirely.
    if (ops->stateSize > track.stateBuf.size())
    {
        FailFsm("an action needs more run state than this machine measured "
                "(graph and action library out of step; reload the graph)");
        track.current = nullptr;
        track.currentOps = nullptr;
        return;
    }
    track.currentOps = ops;

    // Zero on every entry, so looping back onto an action restarts it instead
    // of resuming a half-finished run, and the previous action's data is never
    // read as this one's state. Some actions keep no state at all, and a graph
    // made only of those has an empty slot.
    uint8_t* const state = track.stateBuf.empty() ? nullptr : track.stateBuf.data();
    if (state && ops->stateSize > 0)
    {
        std::memset(state, 0, ops->stateSize);
    }
    if (ops->onEnter)
    {
        ops->onEnter(node->instance, state, ctx);
    }
}

void FsmComponent::ExitState(Track& track)
{
    if (!track.active)
    {
        return;
    }

    // Only the running action needs exiting: the ones before it were exited as
    // they finished, the ones after it were never entered.
    if (track.current && track.currentOps && track.currentOps->onExit)
    {
        // Still the running action's state: nothing has entered the slot since.
        uint8_t* const state = track.stateBuf.empty() ? nullptr : track.stateBuf.data();
        FsmContext ctx{ GetOwner(), this, 0.0f };
        track.currentOps->onExit(track.current->instance, state, ctx);
    }

    track.active = nullptr;
    track.graph = nullptr;
    track.actions = nullptr;
    track.current = nullptr;
    track.currentOps = nullptr;
    // stateBuf is kept: it belongs to the track for the machine's life, and
    // freeing it here would add an allocation to every transition. Its
    // contents are stale, and BeginAction zeroes what the next action reads.
    track.finishedFired = false;
    // `groups` is kept on purpose: it is the track's position in the grouping,
    // maintained by ResolveFlowTarget as the flow enters and leaves groups.
}

// No graph parameter: every track already knows which graph level its active
// state lives in, which is the only place its transitions can be wired.
void FsmComponent::ProcessEvents()
{
    while (m_EventHead < m_EventQueue.size() && !m_Failed)
    {
        // By value, and by index: entering a state runs its first action, which
        // may raise events of its own, so the vector can grow (and move) inside
        // this loop. Taking the event out makes that safe, and the index keeps
        // each read from shifting the remaining entries down. Moved, not
        // copied: the slot is never read again, and a copy would allocate for
        // any name over 15 characters.
        const QueuedEvent ev = std::move(m_EventQueue[m_EventHead++]);

        // Broadcast events go to every track (each may transition on it by
        // itself); track-scoped events (FINISHED) only to their own track.
        for (size_t t = 0; t < m_Tracks.size() && !m_Failed; ++t)
        {
            if (ev.track >= 0 && static_cast<int>(t) != ev.track)
            {
                continue;
            }

            Track& track = m_Tracks[t];
            if (!track.active)
            {
                continue;
            }

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
                // Not listened for here. For a track's own FINISHED that means
                // a terminal state, where the track stays.
                if (ev.track >= 0)
                {
                    DEKI_LOG_DEBUG("FsmComponent: event '%s' ignored by state '%s'", ev.name.c_str(),
                                   state->name.c_str());
                }
                continue;
            }

            // Transitions are wired in the graph the state itself lives in,
            // which for a state inside a group is that group's inner graph.
            const DekiNodeGraph::NodeGraphData::NodeInstance* next = track.graph->Next(track.active->id, pin);
            if (!next)
            {
                char buf[192];
                std::snprintf(buf, sizeof(buf), "state '%s' transition '%s' is not wired", state->name.c_str(),
                              ev.name.c_str());
                FailFsm(buf);
                return;
            }

            if (++m_TransitionsThisFrame > kMaxTransitionsPerFrame)
            {
                FailFsm("more than 16 transitions in one frame (event cycle?)");
                return;
            }
            EnterState(track, track.graph, next);

            // The state that raised this track's FINISHED is gone. Any queued
            // event scoped to this track belongs to that state, not to the one
            // just entered, so it must not be delivered.
            //
            // Only from the unread part: entries before m_EventHead are already
            // handled, and erasing one would slide the pending ones under the
            // index this loop reads with.
            m_EventQueue.erase(std::remove_if(m_EventQueue.begin() + static_cast<std::ptrdiff_t>(m_EventHead),
                                              m_EventQueue.end(),
                                              [t](const QueuedEvent& q) { return q.track == static_cast<int>(t); }),
                               m_EventQueue.end());
        }
    }

    // All read: rewind to the front, keeping the storage for the next frame.
    // A failure leaves the rest where it is; ResetMachine clears both.
    if (m_EventHead >= m_EventQueue.size())
    {
        m_EventQueue.clear();
        m_EventHead = 0;
    }
}

void FsmComponent::RunActions()
{
    const float frameSeconds = std::min(Deki::Time::GetDeltaTimeF() * 0.001f, kMaxFrameSeconds);

    // Every track runs the one action its active state is on, and follows the
    // wires for as long as actions keep finishing this frame. Each track has
    // its own FINISHED, and its own frame time to spend (FsmContext::dt).
    for (size_t t = 0; t < m_Tracks.size(); ++t)
    {
        Track& track = m_Tracks[t];
        if (!track.active)
        {
            continue;
        }
        FsmContext ctx{ GetOwner(), this, frameSeconds };

        int steps = 0;
        while (track.current)
        {
            const FsmActionOps* ops = track.currentOps;
            const DekiNodeGraph::NodeGraphData::NodeInstance* node = track.current;
            void* const state = track.stateBuf.empty() ? nullptr : static_cast<void*>(track.stateBuf.data());

            // No onUpdate: the action finishes on pin 0 as soon as it is entered.
            const int pin = ops->onUpdate ? ops->onUpdate(node->instance, state, ctx) : 0;
            if (m_Failed)
            {
                return;
            }
            if (pin == kFsmActionRunning)
            {
                break;  // still running: nothing downstream runs
            }

            if (ops->onExit)
            {
                ops->onExit(node->instance, state, ctx);
            }
            if (m_Failed)
            {
                return;
            }

            // An unwired pin ends the flow, which is what raises FINISHED.
            BeginAction(track, track.actions->Next(node->id, pin), ctx);
            if (m_Failed)
            {
                return;
            }

            if (++steps > kMaxActionStepsPerFrame)
            {
                FailFsm("an action flow ran more than 256 steps in one frame "
                        "(a loop with nothing that takes time in it?)");
                return;
            }
        }

        if (!track.current && !track.finishedFired)
        {
            track.finishedFired = true;
            m_EventQueue.push_back({ kFinishedEvent, static_cast<int>(t) });
        }
    }
}

}  // namespace DekiFsm
