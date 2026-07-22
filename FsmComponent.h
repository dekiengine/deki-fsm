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
// One state is active at a time. Every frame the active state's enabled
// actions run in stack order; when all of them have finished, the built-in
// FINISHED event fires. Events (FINISHED, or custom names raised by actions /
// SendEvent()) are matched against the active state's `transitions` list; a
// match follows that pin's wire to the next state (exit -> enter). Unmatched
// events are dropped (states legitimately listen to subsets).
//
// Failure policy (no fallbacks): a missing Start node, an unwired Start or
// transition pin, a link into a non-State node, an action type with no
// registered runtime ops, an unresolvable target object, or a transition
// storm (>16 per frame) logs ONE error and latches the machine off until the
// graph asset is reloaded or reassigned.
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

    // Raise an event by name (from actions or any game code). Queued and
    // matched against the active state's transitions during Update.
    void SendEvent(const std::string& name);

    bool Failed() const { return failed_; }

    // The active state's authored name ("" while none) — debug/UI readout.
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
    void ResetMachine();
    void EnterInitialState(const NodeGraphData& g);
    void EnterState(const NodeGraphData& g, const NodeGraphData::NodeInstance* state);
    void ExitState();
    void ProcessEvents(const NodeGraphData& g);
    void RunActions();

    const NodeGraphData::NodeInstance* active_ = nullptr;
    FsmGraph* lastGraph_ = nullptr;       // detect asset reload/reassign
    bool failed_ = false;

    // Per-active-state action bookkeeping, index-aligned with the state's
    // children (disabled children get a null ops slot).
    std::vector<const FsmActionOps*> ops_;
    std::vector<size_t> stateOffsets_;
    std::vector<uint8_t> stateBlob_;      // zero-initialized on state entry
    std::vector<uint8_t> finishedLatch_;
    bool finishedFired_ = false;
    int transitionsThisFrame_ = 0;

    std::vector<std::string> eventQueue_;
    std::unordered_map<const void*, std::shared_ptr<bool>> clickWatches_;
};

#include "generated/FsmComponent.gen.h"
