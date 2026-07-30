#pragma once

#include "DekiEngine.h"
#include "FsmGraph.h"
#include "FsmActionRegistry.h"
#include "assets/AssetRef.h"
#include "reflection/PropertyRef.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Runs a state-machine graph asset (PlayMaker-style) on the object it sits on.
//
// The graph mirrors a script's lifecycle with PARALLEL TRACKS. The three
// entry nodes (Awake/Start/Update) are permanent fixtures of every graph,
// exactly like the hooks of a DekiBehaviour; each WIRED entry output begins
// its own track — an independent state flow with its own active state — and
// an unwired output is an unused hook. Custom events (raised by actions or
// SendEvent()) broadcast to every track; each track's active state decides
// via its `transitions` list.
//
// ALL actions live in states, as the nodes of the state's own inner graph (its
// ACTION FLOW). Entering a state starts at the flow's Entry node and follows
// its wire; when an action finishes it reports the OUTPUT PIN it finished on,
// and control moves to whatever that pin is wired to (so a run of instant
// actions completes within one frame). Running off an unwired pin ends the
// flow and fires the track's FINISHED (matched only against that track's
// transitions). An action that never finishes (Watch Button, an everyFrame
// setter) parks the flow on itself and nothing downstream runs — so put such
// watchers in a state of their own, typically a track wired from Update.
//
// A flow is a graph, so it may branch (Compare Property has a true pin and a
// false pin) and it may loop. Re-entering an action zeroes its runtime state
// exactly as if it were entered for the first time.
//
// GROUPS are pure organization: entering one continues from its Group In node,
// and a transition onto a Group Exit node inside continues from the matching
// pin on the group OUTSIDE. Groups nest, cost nothing at runtime, and never
// change what the machine does — only which canvas you see it on.
//
// Failure policy (no fallbacks): an unwired Start pin, a wire into a node that
// is not a State/Group/Group Exit, an action type with no registered runtime
// ops, an unresolvable target object, a matched transition with no wire, a
// group whose In or matching exit pin is unwired, a Group Exit naming no pin
// on its group (or sitting at the root with no group to leave), a transition
// storm (>16 per frame), an action flow that steps more than 256 times in one
// frame, or a graph with nothing to run logs ONE error and latches the machine
// off until the graph asset is reloaded or reassigned.
class FsmComponent : public DekiBehaviour
{
    DEKI_COMPONENT(FsmComponent, DekiBehaviour, "Logic", "3f8a61c9-7b2e-4d5a-9c14-8e6f2a0b5d73", "DEKI_FEATURE_FSM")
    DEKI_DESCRIPTION("Runs a state machine graph asset on this object.")
public:
    // The state-machine graph. Assign a ".asset" of type "FsmGraph". No asset
    // -> the component idles (nothing to run).
    DEKI_EXPORT
    Deki::AssetRef<FsmGraph> graph;

    FsmComponent() = default;

    void Awake() override;
    void Update() override;

    // Raise an event by name (from actions or any game code). Queued,
    // broadcast to every track, matched against each track's active state's
    // transitions during Update.
    void SendEvent(const std::string& name);

    bool Failed() const { return failed_; }

    // The first track's authored state name ("" while none) — debug/UI readout.
    const std::string& ActiveStateName() const;

    // ---- Internal helpers for actions (via FsmContext) ----

    // One error, then latch. Public so FsmContext (and custom project actions)
    // can enforce the same policy.
    void FailFsm(const char* message);

    // "" = owner; else by object name within the owner's prefab. nullptr after
    // latching failed (logged).
    DekiObject* ResolveTargetObject(const std::string& name);

    // Shared clicked-flag for Watch Button: first call for `key` (the action's
    // data instance) registers a click callback on `button` that sets the
    // flag; later calls return the same flag. The shared_ptr keeps the flag
    // alive for the callback even if this component dies first.
    std::shared_ptr<bool> EnsureClickWatch(const void* key, class ButtonComponent* button);

    /**
     * @brief Bind a PropertyRef that targets one of this machine's variables.
     *
     * Variables are declared by the graph's Variables node and stored per
     * component (this one), so the engine's BindPropertyRef cannot resolve
     * them — actions call this instead when ref.component is "Variable".
     * Returns false (after logging + latching) when there is no such variable.
     */
    bool BindVariable(const PropertyRef& ref, PropertyBinding& out);

private:
    // One independent state flow, begun by a wired lifecycle root output.
    struct Track
    {
        // Where the track is: the active State node, and the graph that node
        // lives in (which is where its transition wires are looked up — the
        // root, or the inside of a group).
        const NodeGraphData::NodeInstance* active = nullptr;
        const NodeGraphData::Graph* graph = nullptr;

        // The groups descended through to reach `active`, outermost first.
        // Each frame remembers the group node AND the graph it sits in, which
        // is exactly what leaving through a Group Exit needs.
        struct GroupFrame
        {
            const NodeGraphData::Graph* graph = nullptr;
            const NodeGraphData::NodeInstance* group = nullptr;
        };
        std::vector<GroupFrame> groups;

        // The active state's action flow (its inner graph; null = no actions).
        const NodeGraphData::Graph* actions = nullptr;
        // The one action running right now, null once the flow has run off its
        // end (or before it starts). `currentOps` is its registered behavior.
        const NodeGraphData::NodeInstance* current = nullptr;
        const FsmActionOps* currentOps = nullptr;

        // Per-run action state: one slice per node of `actions`, indexed by the
        // node's position in that graph's node vector. Zeroed when an action is
        // entered, so looping back onto one restarts it cleanly.
        std::vector<size_t> stateOffsets;
        std::vector<uint8_t> stateBlob;
        bool finishedFired = false;
    };

    // track -1 = broadcast (offered to every track); >= 0 = that track only
    // (FINISHED, which must not leak between parallel flows).
    struct QueuedEvent
    {
        std::string name;
        int track = -1;
    };

    // One live variable of this machine. Storage is per component, so two
    // objects running the same graph asset never share a value. Numbers and
    // bools live in `number`; text in `text` (a Vector2 variable would use both
    // number slots, which is why they are separate fields, not a union).
    struct Variable
    {
        uint32_t nameHash = 0;
        DekiPropertyType type = DekiPropertyType::Float;
        float number = 0.0f;
        float number2 = 0.0f;
        std::string text;
    };

    void ResetMachine();
    void InitializeMachine(const NodeGraphData& g);
    // Read the graph's Variables node (if any) and allocate this machine's live
    // copies from the declared initial values.
    void InitializeVariables(const NodeGraphData& g);

    // Follow a flow wire to the State it ultimately lands on: descending into
    // any Group it passes through and ascending out of any Group Exit, pushing
    // and popping `groups` to match. Returns nullptr after latching the machine
    // failed. `graph` is in/out: the graph `node` lives in on the way in, the
    // graph the returned State lives in on the way out.
    const NodeGraphData::NodeInstance* ResolveFlowTarget(
        const NodeGraphData::Graph*& graph, const NodeGraphData::NodeInstance* node,
        Track& track);

    // Make `target` (resolved through groups) this track's active state and
    // start its action flow at the Entry node's wire.
    void EnterState(Track& track, const NodeGraphData::Graph* graph,
                    const NodeGraphData::NodeInstance* target);
    void ExitState(Track& track);
    // Make `node` the running action: zero its state slice and call onEnter.
    // A null node means the flow has run off its end.
    void BeginAction(Track& track, const NodeGraphData::NodeInstance* node, FsmContext& ctx);
    void ProcessEvents();
    void RunActions();

    std::vector<Track> tracks_;
    bool initialized_ = false;            // tracks/stacks built for lastGraph_
    FsmGraph* lastGraph_ = nullptr;       // detect asset reload/reassign
    bool failed_ = false;
    int transitionsThisFrame_ = 0;

    std::vector<QueuedEvent> eventQueue_;
    std::unordered_map<const void*, std::shared_ptr<bool>> clickWatches_;

    // Stable for the machine's life once built (actions cache pointers into it),
    // so this must never be reallocated while a graph is running — it is filled
    // once in InitializeVariables and only cleared by ResetMachine.
    std::vector<Variable> variables_;
};

#include "generated/FsmComponent.gen.h"
