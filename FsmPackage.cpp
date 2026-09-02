/**
 * @file FsmPackage.cpp
 * @brief Package entry point for deki-fsm DLL.
 *
 * Exports the standard Deki plugin interface so the editor can load
 * deki-fsm.dll and register its components (FsmComponent). For linked DLLs
 * (not dynamically loaded), DekiFsm_EnsureRegistered() must be called from the
 * main executable to trigger the static initializers.
 */

#include "interop/DekiPlugin.h"
#include "FsmPackage.h"
#include "FsmComponent.h"
#include "FsmNodes.h"
#include "FsmActions.h"
#include "reflection/ComponentRegistry.h"
#include "reflection/ComponentFactory.h"
#include "deki-nodegraph/DekiNode.h"   // NodeFactory + NodeTypeRegistry (editor)

#ifdef DEKI_EDITOR

// Auto-generated registration helpers
extern void DekiFsm_RegisterComponents();
extern int DekiFsm_GetAutoComponentCount();
extern const DekiComponentMeta* DekiFsm_GetAutoComponentMeta(int index);

// Defined in editor/FsmGraphEditor.cpp (re-registers the Fsm graph domain).
extern "C" void DekiFsm_RegisterEditorGraphDomain(void);

static bool s_FsmRegistered = false;

namespace
{
    // Re-runnable mirror of the generated REGISTER_RUNTIME_NODE/REGISTER_NODE
    // static registrars. Those run once at DLL load; the editor's plugin-only
    // hot reload wipes the shared node registries WITHOUT unloading this
    // package, so registration must be repeatable on demand. NodeFactory
    // overwrites by typeId and NodeTypeRegistry dedupes, so this is idempotent.
    template<typename T>
    void RegisterFsmNodeType()
    {
        SceneFormat::NodeFactory::Instance().Register(
            DekiHashString(T::StaticNodeName),
            []() -> void* { return new T(); },
            [](void* p, SceneFormat::SceneMsgPackParser& parser, uint32_t mapSize) -> bool {
                return DeserializeMsgPack(*static_cast<T*>(p), parser, mapSize); },
            [](void* p) { delete static_cast<T*>(p); });
        NodeTypeRegistry::Instance().Register(&T::GetNodeMeta(), sizeof(DekiNodeMeta));
    }
}

extern "C" {

/**
 * @brief (Re-)register this package's node graph types: state/action node
 * factories, editor metas, and the Fsm graph domain. Called at package load via
 * DekiPlugin_RegisterComponents and again after any registry wipe that keeps
 * this DLL loaded (plugin-only hot reload).
 */
DEKI_FSM_API void DekiFsm_RegisterGraphTypes(void)
{
    RegisterFsmNodeType<FsmStartNode>();
    RegisterFsmNodeType<FsmAwakeNode>();
    RegisterFsmNodeType<FsmUpdateNode>();
    RegisterFsmNodeType<FsmStateNode>();
    RegisterFsmNodeType<FsmActionEntryNode>();
    RegisterFsmNodeType<FsmGroupNode>();
    RegisterFsmNodeType<FsmGroupInNode>();
    RegisterFsmNodeType<FsmGroupExitNode>();
    RegisterFsmNodeType<FsmVariablesNode>();
    RegisterFsmNodeType<FsmNumberVariable>();
    RegisterFsmNodeType<FsmBoolVariable>();
    RegisterFsmNodeType<FsmTextVariable>();
    RegisterFsmNodeType<FsmWaitAction>();
    RegisterFsmNodeType<FsmSendEventAction>();
    RegisterFsmNodeType<FsmSetPropertyAction>();
    RegisterFsmNodeType<FsmComparePropertyAction>();
    RegisterFsmNodeType<FsmModifyPropertyAction>();
    RegisterFsmNodeType<FsmRandomPropertyAction>();
    RegisterFsmNodeType<FsmTweenPropertyAction>();
    RegisterFsmNodeType<FsmSpawnSceneAction>();
    RegisterFsmNodeType<FsmDestroyObjectAction>();
    RegisterFsmNodeType<FsmSetParentAction>();
    RegisterFsmNodeType<FsmPlayAnimationAction>();
    RegisterFsmNodeType<FsmSendEventToAction>();
    RegisterFsmNodeType<FsmLogAction>();
    RegisterFsmNodeType<FsmWatchButtonAction>();
    DekiFsm_RegisterEditorGraphDomain();
}

/**
 * @brief Ensure deki-fsm package is loaded and components are registered.
 * @return Number of components registered by this package
 */
DEKI_FSM_API int DekiFsm_EnsureRegistered(void)
{
    if (s_FsmRegistered)
        return DekiFsm_GetAutoComponentCount();
    s_FsmRegistered = true;

    DekiFsm_RegisterComponents();

    return DekiFsm_GetAutoComponentCount();
}

} // extern "C"

// =============================================================================
// Plugin metadata (for dynamic loading compatibility)
// =============================================================================

extern "C" {

DEKI_PLUGIN_API const char* DekiPlugin_GetName(void)
{
    return "Deki FSM Package";
}

DEKI_PLUGIN_API const char* DekiPlugin_GetVersion(void)
{
#ifdef DEKI_PACKAGE_VERSION
    return DEKI_PACKAGE_VERSION;
#else
    return "0.0.0-dev";
#endif
}

DEKI_PLUGIN_API int DekiPlugin_Init(void)
{
    return 0;
}

DEKI_PLUGIN_API void DekiPlugin_Shutdown(void)
{
    s_FsmRegistered = false;
}

DEKI_PLUGIN_API int DekiPlugin_GetComponentCount(void)
{
    return DekiFsm_GetAutoComponentCount();
}

DEKI_PLUGIN_API const DekiComponentMeta* DekiPlugin_GetComponentMeta(int index)
{
    return DekiFsm_GetAutoComponentMeta(index);
}

DEKI_PLUGIN_API void DekiPlugin_RegisterComponents(void)
{
    DekiFsm_EnsureRegistered();
    // Deliberately OUTSIDE the s_FsmRegistered latch: node registries are
    // wiped on every hot reload (full or plugin-only) and this export is the
    // re-registration path for the plugin-only case.
    DekiFsm_RegisterGraphTypes();
}

DEKI_PLUGIN_API void DekiPlugin_OnPlayModeStop(void)
{
    // Machine state lives per FsmComponent instance; instances die with the
    // play-mode scene, so there is nothing global to reset here.
}

// deki-fsm renders no editor UI of its own, so it links no ImGui and shares no
// ImGui context. Its inspectors are the editor's generic reflection UI and the
// generic Node Graph window.

// =============================================================================
// Package-specific feature API (for linked DLL access without name conflicts)
// =============================================================================

DEKI_FSM_API const char* DekiFsm_GetName(void)
{
    return "FSM";
}

} // extern "C"

#endif // DEKI_EDITOR
