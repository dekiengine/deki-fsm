#pragma once

#include "FsmApi.h"   // DEKI_FSM_API

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

class DekiObject;
class FsmComponent;

// Passed to every action callback. Owner/fsm/dt plus the helpers actions need;
// helpers are implemented by FsmComponent (FsmComponent.cpp).
struct FsmContext
{
    DekiObject* owner = nullptr;   // the object the FsmComponent sits on
    FsmComponent* fsm = nullptr;
    float dt = 0.0f;               // seconds this frame

    // Queue an event on the FSM (processed against the active state's
    // transitions after the action pass).
    void SendEvent(const std::string& name);

    // "" = owner; else an object of the owner's scene by name. Returns
    // nullptr AFTER latching the FSM failed (logged) — callers just bail.
    DekiObject* ResolveTarget(const std::string& name);

    // Log one error and latch the FSM failed (no fallback policy: a broken
    // action stops the machine loudly instead of quietly misbehaving).
    void Fail(const char* message);
};

// onUpdate return value: the action is still running, so nothing downstream of
// it runs this frame. Anything >= 0 is the OUTPUT PIN the action finished on.
constexpr int kFsmActionRunning = -1;

/**
 * @brief Runtime behavior for one action type.
 *
 * Action DATA lives in reflected structs shared by every FsmComponent using
 * the same graph asset, so per-run state goes in a separate blob the
 * interpreter allocates per action node: `stateSize` bytes, zero-initialized
 * when the action is entered, passed back to every callback. onEnter/onExit
 * may be null.
 *
 * onUpdate returns kFsmActionRunning while the action is still going, else the
 * index of the OUTPUT PIN it finished on — which is how a branching action
 * picks its successor (Compare Property returns 0 for true, 1 for false). A
 * single-outcome action returns 0. An action with no onUpdate at all is an
 * enter-only action: done on pin 0 the moment it runs.
 *
 * An action that never finishes (Watch Button, everyFrame setters) simply
 * always returns kFsmActionRunning, which parks the flow on it.
 */
struct FsmActionOps
{
    size_t stateSize = 0;
    void (*onEnter)(const void* data, void* state, FsmContext& ctx) = nullptr;
    int (*onUpdate)(const void* data, void* state, FsmContext& ctx) = nullptr;
    void (*onExit)(const void* data, void* state, FsmContext& ctx) = nullptr;
};

/**
 * @brief typeId (DekiHashString of the action's node name) -> runtime ops.
 *
 * The data structs self-register into NodeFactory via their generated code;
 * this registry carries the behavior half. Cleared implicitly on DLL unload
 * (static storage) — entries and the graphs referencing them live and die
 * with the same package/plugin DLLs.
 */
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

// Register runtime ops for an action struct (place at file scope in a .cpp,
// next to the callbacks). ClassName must be a DEKI_NODE type; the key is the
// hash of its node name, matching what the graph loader stores.
#define REGISTER_FSM_ACTION(ClassName, Ops) \
    static struct ClassName##_FsmActionRegistrar { \
        ClassName##_FsmActionRegistrar() { \
            FsmActionRegistry::Instance().Register( \
                DekiHashString(ClassName::StaticNodeName), Ops); \
        } \
    } s_##ClassName##_FsmActionRegistrar
