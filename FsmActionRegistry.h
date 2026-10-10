#pragma once

#include "FsmApi.h"  // DEKI_FSM_API

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace Deki
{
class Object;
}

namespace DekiFsm
{
class FsmComponent;

/// Passed to every action callback: the owner, the FSM and the frame time, plus
/// the helpers actions need. FsmComponent.cpp implements the helpers.
struct FsmContext
{
    Deki::Object* owner = nullptr;  // the object the FsmComponent is on
    FsmComponent* fsm = nullptr;
    // Seconds of this frame not yet spent. A timed action that finishes sets
    // it to what it did not use, so the next action in the flow starts with
    // that remainder rather than the whole frame again: a loop of tweens then
    // keeps time however long the frames are, and machines started apart stay
    // apart.
    float dt = 0.0f;

    /// Queues an event on the FSM. It is checked against the active state's
    /// transitions after the action pass.
    void SendEvent(const std::string& name);

    /// "" means the owner; anything else names an object in the owner's
    /// scene. When there is none, logs, marks the FSM failed and returns
    /// nullptr, so callers just return.
    Deki::Object* ResolveTarget(const std::string& name);

    /// Logs one error and marks the FSM failed for good. A broken action
    /// stops the machine visibly instead of quietly misbehaving.
    void Fail(const char* message);
};

/// onUpdate's return value while the action is still running: nothing after it
/// runs this frame. Anything >= 0 is the output pin the action finished on.
constexpr int kFsmActionRunning = -1;

/// Runtime behavior for one action type.
///
/// An action's data lives in reflected structs shared by every FsmComponent
/// using the same graph asset. So per-run state goes in a separate block the
/// interpreter allocates per action node: `stateSize` bytes, zeroed when the
/// action is entered and passed to every callback. onEnter and onExit may be
/// null.
///
/// onUpdate returns kFsmActionRunning while the action is still going, else the
/// index of the output pin it finished on. That is how a branching action picks
/// what runs next (Compare Property returns 0 for true, 1 for false). An action
/// with one outcome returns 0. An action with no onUpdate finishes on pin 0 as
/// soon as it is entered.
///
/// An action that never finishes (Watch Button, everyFrame setters) always
/// returns kFsmActionRunning, which keeps the flow on it.
struct FsmActionOps
{
    size_t stateSize = 0;
    void (*onEnter)(const void* data, void* state, FsmContext& ctx) = nullptr;
    int (*onUpdate)(const void* data, void* state, FsmContext& ctx) = nullptr;
    void (*onExit)(const void* data, void* state, FsmContext& ctx) = nullptr;
};

/// Maps a typeId (Deki::HashString of the action's node name) to its runtime
/// ops. The data structs register themselves with DekiNodeGraph::NodeFactory
/// through their generated code; this registry holds the behavior. It is
/// static storage, so it goes away with its DLL, as do the graphs that use it.
class DEKI_FSM_API FsmActionRegistry
{
public:
    static FsmActionRegistry& Instance();

    void Register(uint32_t typeId, const FsmActionOps& ops) { m_Ops[typeId] = ops; }
    const FsmActionOps* Find(uint32_t typeId) const
    {
        auto it = m_Ops.find(typeId);
        return it != m_Ops.end() ? &it->second : nullptr;
    }

private:
    FsmActionRegistry() = default;
    std::unordered_map<uint32_t, FsmActionOps> m_Ops;
};

/// Registers this package's own actions (FsmActionLibrary.cpp). Called from
/// DekiFsmInitSystem.
void RegisterActionLibrary();

// Registers runtime ops for an action struct. Put it at file scope in a .cpp,
// next to the callbacks. ClassName must be a DEKI_NODE type; the key is the
// hash of its node name, which is what the graph loader stores.
#define REGISTER_FSM_ACTION(ClassName, Ops)                                                                            \
    static struct ClassName##_FsmActionRegistrar                                                                       \
    {                                                                                                                  \
        ClassName##_FsmActionRegistrar()                                                                               \
        {                                                                                                              \
            FsmActionRegistry::Instance().Register(::Deki::HashString(ClassName::StaticNodeName), Ops);                \
        }                                                                                                              \
    } s_##ClassName##_FsmActionRegistrar

}  // namespace DekiFsm
