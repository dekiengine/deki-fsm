#pragma once

#include <deki/Engine.h>
#include "FsmGraph.h"
#include "FsmActionRegistry.h"
#include <deki/assets/AssetRef.h>
#include <deki/reflection/PropertyRef.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Deki2D
{
class ButtonComponent;
}

namespace DekiFsm
{

/// Runs a state-machine graph asset (PlayMaker-style) on its object.
///
/// The graph follows a script's lifecycle with parallel tracks. Every graph
/// has the three entry nodes Awake, Start and Update, like the hooks of a
/// Deki::Component. Each wired entry output starts its own track, an
/// independent state flow with its own active state; an unwired output is an
/// unused hook. Custom events (raised by actions or SendEvent()) go to every
/// track, and each track's active state decides through its `transitions`.
///
/// All actions live in states, as the nodes of the state's inner graph, its
/// action flow. Entering a state starts at the flow's Entry node and follows
/// its wire. When an action finishes it reports the output pin it finished on,
/// and control moves to whatever that pin is wired to, so a run of instant
/// actions completes within one frame. Running off an unwired pin ends the
/// flow and fires the track's FINISHED, matched only against that track's
/// transitions. An action that never finishes (Watch Button, an everyFrame
/// setter) keeps the flow on itself and nothing after it runs, so put such
/// watchers in a state of their own, usually on a track wired from Update.
///
/// A flow is a graph, so it may branch (Compare Property has a true pin and a
/// false pin) and loop. Entering an action again zeroes its runtime state,
/// as on its first entry.
///
/// Groups only organize: entering one continues from its Group In node, and a
/// transition onto a Group Exit node inside continues from the matching pin on
/// the group outside. Groups nest, cost nothing at runtime, and change only
/// which canvas you see the machine on, not what it does.
///
/// Any of these logs one error and stops the machine until the graph asset is
/// reloaded or reassigned: an unwired Start pin, a wire into a node that is
/// not a State, Group or Group Exit, an action type with no registered runtime
/// ops, a target object that cannot be found, a matched transition with no
/// wire, a group whose In or matching exit pin is unwired, a Group Exit naming
/// no pin on its group (or at the root, with no group to leave), more than 16
/// transitions in one frame, an action flow taking more than 256 steps in one
/// frame, or a graph with nothing to run.
DEKI_CATEGORY("Logic")
DEKI_DESCRIPTION("Runs a state machine graph asset on this object.")
class FsmComponent : public Deki::Component
{
public:
    // The state-machine graph: a ".asset" of type "FsmGraph". Without one the
    // component does nothing.
    DEKI_EXPORT
    DEKI_TOOLTIP("The state machine asset this object runs. Author it in the node graph editor.")
    Deki::AssetRef<FsmGraph> graph;

    // This object's own starting values for the graph's variables, one
    // "name=value" per entry, applied over the values the graph declares. One
    // graph can then serve many objects, each tuned here (a row of bobbing
    // coins with different phases, say). An unknown name or a value of the
    // wrong type stops the machine.
    DEKI_EXPORT
    DEKI_TOOLTIP("This object's own starting values for the graph's variables, as name=value (e.g. speedHz=0.625).")
    std::vector<std::string> variableOverrides;

    FsmComponent() = default;

    void Awake() override;
    void Update() override;

    /// Raises an event by name, from actions or any game code. It is queued,
    /// sent to every track, and matched against each track's active state's
    /// transitions during Update.
    void SendEvent(const std::string& name);

    bool Failed() const { return m_Failed; }

    /// The first track's state name ("" while there is none), for debug
    /// displays.
    const std::string& ActiveStateName() const;

    // ---- Helpers for actions (through FsmContext) ----

    /// Logs one error and stops the machine. Public so FsmContext and a
    /// project's own actions follow the same rule.
    void FailFsm(const char* message);

    /// "" means the owner; anything else names an object in the owner's
    /// scene. When there is none, logs, stops the machine and returns nullptr.
    Deki::Object* ResolveTargetObject(const std::string& name);

    /// Clicked flag for Watch Button. The first call for `key` (the action's
    /// data instance) registers a click callback on `button` that sets the
    /// flag; later calls return the same flag. The shared_ptr keeps the flag
    /// alive for the callback even if this component is destroyed first.
    std::shared_ptr<bool> EnsureClickWatch(const void* key, Deki2D::ButtonComponent* button);

    /// Binds a PropertyRef that targets one of this machine's variables.
    /// Variables are declared by the graph's Variables node and stored in this
    /// component, so the engine's BindPropertyRef cannot find them; actions
    /// call this instead when ref.component is "Variable". When there is no
    /// such variable, logs, stops the machine and returns false.
    bool BindVariable(const Deki::PropertyRef& ref, Deki::PropertyBinding& out);

private:
    // One independent state flow, started by a wired lifecycle entry output.
    struct Track
    {
        // The active State node and the graph it lives in (the root, or the
        // inside of a group), where its transition wires are looked up.
        const DekiNodeGraph::NodeGraphData::NodeInstance* active = nullptr;
        const DekiNodeGraph::NodeGraphData::Graph* graph = nullptr;

        // The groups entered to reach `active`, outermost first. Each entry
        // keeps the group node and the graph it is in, which is what leaving
        // through a Group Exit needs.
        struct GroupFrame
        {
            const DekiNodeGraph::NodeGraphData::Graph* graph = nullptr;
            const DekiNodeGraph::NodeGraphData::NodeInstance* group = nullptr;
        };
        std::vector<GroupFrame> groups;

        // The active state's action flow (its inner graph; null when it has no
        // actions).
        const DekiNodeGraph::NodeGraphData::Graph* actions = nullptr;
        // The action running now; null before the flow starts and once it has
        // run off its end. `currentOps` is its registered behavior.
        const DekiNodeGraph::NodeGraphData::NodeInstance* current = nullptr;
        const FsmActionOps* currentOps = nullptr;

        // Per-run action state. One slot, not one per action: a track runs one
        // action at a time, and the handover is ordered so the outgoing
        // action's onExit reads this before the incoming action's memset
        // clears it. Sized once, to the largest stateSize in the whole graph,
        // so a state change never allocates or frees.
        //
        // A vector, not an inline array: `m_Tracks` reallocates as tracks are
        // added, and only a heap block keeps the address an action was given
        // stable across that.
        std::vector<uint8_t> stateBuf;
        bool finishedFired = false;
    };

    // track -1 sends the event to every track; >= 0 to that track only (used
    // for FINISHED, which must not reach other parallel flows).
    struct QueuedEvent
    {
        std::string name;
        int track = -1;
    };

    // One Watch Button flag, keyed by the action data instance that asked for
    // it. Kept in a vector, not a map: a machine has a handful at most, and a
    // hash map costs memory even when empty, a bucket allocation on first use,
    // and code size in the firmware image.
    struct ClickWatch
    {
        const void* key = nullptr;
        std::shared_ptr<bool> flag;
    };

    // One live variable of this machine. Stored per component, so two objects
    // running the same graph asset never share a value. Numbers and bools live
    // in `number`, text in `text`. The fields are separate, not a union,
    // because a Vector2 variable would use both number slots.
    struct Variable
    {
        uint32_t nameHash = 0;
        Deki::PropertyType type = Deki::PropertyType::Float;
        float number = 0.0f;
        float number2 = 0.0f;
        std::string text;
    };

    void ResetMachine();
    void InitializeMachine(const DekiNodeGraph::NodeGraphData& g);
    // Reads the graph's Variables node, if any, and creates this machine's
    // live copies from the declared initial values.
    void InitializeVariables(const DekiNodeGraph::NodeGraphData& g);
    // Applies variableOverrides over the declared initial values.
    void ApplyVariableOverrides();

    // Follows a flow wire to the State it finally lands on, going into any
    // Group it passes through and out of any Group Exit, pushing and popping
    // `groups` to match. On failure, stops the machine and returns nullptr.
    // `graph` is in/out: the graph `node` lives in on the way in, the graph
    // the returned State lives in on the way out.
    const DekiNodeGraph::NodeGraphData::NodeInstance*
    ResolveFlowTarget(const DekiNodeGraph::NodeGraphData::Graph*& graph,
                      const DekiNodeGraph::NodeGraphData::NodeInstance* node, Track& track);

    // Makes `target` (resolved through groups) this track's active state and
    // starts its action flow at the Entry node's wire.
    void EnterState(Track& track, const DekiNodeGraph::NodeGraphData::Graph* graph,
                    const DekiNodeGraph::NodeGraphData::NodeInstance* target);
    void ExitState(Track& track);
    // Makes `node` the running action: zeroes the track's state slot and calls
    // onEnter. A null node means the flow has run off its end.
    void BeginAction(Track& track, const DekiNodeGraph::NodeGraphData::NodeInstance* node, FsmContext& ctx);
    void ProcessEvents();
    void RunActions();

    // Largest FsmActionOps::stateSize of any action in the graph, found once at
    // init. Every track's slot is this big, so entering a state never
    // allocates.
    size_t m_MaxActionState = 0;

    std::vector<Track> m_Tracks;
    bool m_Initialized = false;       // tracks built for m_LastGraph
    FsmGraph* m_LastGraph = nullptr;  // to notice an asset reload or reassign
    bool m_Failed = false;
    int m_TransitionsThisFrame = 0;

    // Read by index, never erased from the front: handling an event can queue
    // more (an action's onEnter raising one), and erase(begin()) would shift
    // the rest down for every event. Cleared once empty.
    std::vector<QueuedEvent> m_EventQueue;
    size_t m_EventHead = 0;

    std::vector<ClickWatch> m_ClickWatches;

    // Actions keep pointers into this, so it must never reallocate while a
    // graph is running: it is filled once in InitializeVariables and only
    // cleared by ResetMachine.
    std::vector<Variable> m_Variables;
};

}  // namespace DekiFsm
