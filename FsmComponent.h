#pragma once

#include "DekiEngine.h"
#include "FsmGraph.h"
#include "FsmActionRegistry.h"
#include "assets/AssetRef.h"

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
// an unwired output is an unused hook. ALL actions live in states: within
// each track the active state's enabled actions run in stack order every
// frame; when all of them have finished, that track's FINISHED fires
// (matched only against that track's transitions). Custom events (raised by
// actions or SendEvent()) broadcast to every track; each track's active
// state decides via its `transitions` list.
//
// Failure policy (no fallbacks): an unwired Start pin, a wire into a
// non-State node, an action type with no registered runtime ops, an
// unresolvable target object, a matched transition with no wire, a
// transition storm (>16 per frame), or a graph with nothing to run logs ONE
// error and latches the machine off until the graph asset is reloaded or
// reassigned.
class FsmComponent : public DekiBehaviour
{
    DEKI_COMPONENT(FsmComponent, DekiBehaviour, "Logic", "3f8a61c9-7b2e-4d5a-9c14-8e6f2a0b5d73", "DEKI_FEATURE_FSM")
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

private:
    // One independent state flow, begun by a wired lifecycle root output.
    // Action bookkeeping is index-aligned with the active state's children
    // (disabled children get a null ops slot).
    struct Track
    {
        const NodeGraphData::NodeInstance* active = nullptr;
        std::vector<const FsmActionOps*> ops;
        std::vector<size_t> stateOffsets;
        std::vector<uint8_t> stateBlob;      // zero-initialized on state entry
        std::vector<uint8_t> finishedLatch;
        bool finishedFired = false;
    };

    // track -1 = broadcast (offered to every track); >= 0 = that track only
    // (FINISHED, which must not leak between parallel flows).
    struct QueuedEvent
    {
        std::string name;
        int track = -1;
    };

    void ResetMachine();
    void InitializeMachine(const NodeGraphData& g);
    void EnterState(const NodeGraphData& g, Track& track,
                    const NodeGraphData::NodeInstance* state);
    void ExitState(Track& track);
    void ProcessEvents(const NodeGraphData& g);
    void RunActions();

    std::vector<Track> tracks_;
    bool initialized_ = false;            // tracks/stacks built for lastGraph_
    FsmGraph* lastGraph_ = nullptr;       // detect asset reload/reassign
    bool failed_ = false;
    int transitionsThisFrame_ = 0;

    std::vector<QueuedEvent> eventQueue_;
    std::unordered_map<const void*, std::shared_ptr<bool>> clickWatches_;
};

#include "generated/FsmComponent.gen.h"
