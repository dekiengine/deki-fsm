// Package entry point for the deki-fsm DLL. Exports the standard Deki plugin
// interface, so the editor can load the DLL and register its components
// (FsmComponent). When the DLL is linked rather than loaded at runtime, the
// main executable must call DekiFsmEnsureRegistered() to run the static
// initializers.

#include <deki/interop/Plugin.h>
#include "FsmPackage.h"
#include "FsmInit.h"
#include "FsmComponent.h"
#include "FsmNodes.h"
#include "FsmActions.h"
#include <deki/reflection/ComponentRegistry.h>
#include <deki/reflection/ComponentFactory.h>
#include "deki-nodegraph/DekiNode.h"  // DekiNodeGraph::NodeFactory + DekiNodeGraph::NodeTypeRegistry (editor)

extern void DekiFsmRegisterComponents();
extern int DekiFsmGetAutoComponentCount();
extern const Deki::ComponentMeta* DekiFsmGetAutoComponentMeta(int index);

namespace DekiFsm
{

#ifdef DEKI_EDITOR

// The exports below are C symbols at global scope; the package's own
// registration helpers and statics live in its namespace.
using namespace DekiFsm;

// Defined in editor/FsmGraphEditor.cpp; registers the Fsm graph domain again.
extern "C" void DekiFsmRegisterEditorGraphDomain(void);

static bool s_FsmRegistered = false;

namespace
{
// Does what the generated REGISTER_RUNTIME_NODE/REGISTER_NODE static
// registrars do, but can run again. Those run once at DLL load, and the
// editor's plugin-only hot reload clears the shared node registries without
// unloading this package. Safe to repeat: DekiNodeGraph::NodeFactory
// overwrites by typeId and DekiNodeGraph::NodeTypeRegistry ignores duplicates.
template <typename T>
void RegisterFsmNodeType()
{
    DekiNodeGraph::SceneFormat::NodeFactory::Instance().Register(
        Deki::HashString(T::StaticNodeName), []() -> void* { return new T(); },
        [](void* p, Deki::SceneFormat::SceneMsgPackParser& parser, uint32_t mapSize) -> bool
        { return DeserializeMsgPack(*static_cast<T*>(p), parser, mapSize); },
        [](void* p) { delete static_cast<T*>(p); });
    DekiNodeGraph::NodeTypeRegistry::Instance().Register(&T::GetNodeMeta(), sizeof(DekiNodeGraph::DekiNodeMeta));
}
}  // namespace

extern "C"
{
    // Registers this package's node graph types: the state and action node
    // factories, editor metas and the Fsm graph domain. Called at package load
    // through ::DekiPluginRegisterComponents, and again whenever the registries
    // are cleared while this DLL stays loaded (plugin-only hot reload).
    DEKI_FSM_API void DekiFsmRegisterGraphTypes(void)
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
        DekiFsmRegisterEditorGraphDomain();
    }

    // Registers the package's components once. Returns how many there are.
    DEKI_FSM_API int DekiFsmEnsureRegistered(void)
    {
        if (s_FsmRegistered)
        {
            return ::DekiFsmGetAutoComponentCount();
        }
        s_FsmRegistered = true;

        ::DekiFsmRegisterComponents();

        return ::DekiFsmGetAutoComponentCount();
    }

}  // extern "C"

// =============================================================================
// Plugin metadata
// =============================================================================

extern "C"
{
    DEKI_PLUGIN_API const char* DekiPluginGetName(void)
    {
        return "Deki FSM Package";
    }

    DEKI_PLUGIN_API const char* DekiPluginGetVersion(void)
    {
#ifdef DEKI_PACKAGE_VERSION
        return DEKI_PACKAGE_VERSION;
#else
        return "0.0.0-dev";
#endif
    }

    DEKI_PLUGIN_API int DekiPluginInit(void)
    {
        DekiFsmInitSystem();
        return 0;
    }

    DEKI_PLUGIN_API void DekiPluginShutdown(void)
    {
        s_FsmRegistered = false;
    }

    DEKI_PLUGIN_API int DekiPluginGetComponentCount(void)
    {
        return ::DekiFsmGetAutoComponentCount();
    }

    DEKI_PLUGIN_API const Deki::ComponentMeta* DekiPluginGetComponentMeta(int index)
    {
        return ::DekiFsmGetAutoComponentMeta(index);
    }

    DEKI_PLUGIN_API void DekiPluginRegisterComponents(void)
    {
        DekiFsmEnsureRegistered();
        // Outside the s_FsmRegistered check on purpose: every hot reload (full
        // or plugin-only) clears the node registries, and this export is how
        // they are filled again after a plugin-only one.
        DekiFsmRegisterGraphTypes();
    }

    DEKI_PLUGIN_API void DekiPluginOnPlayModeStop(void)
    {
        // Machine state lives in each FsmComponent, which goes away with the
        // play-mode scene, so there is nothing global to reset.
    }

    // deki-fsm draws no editor UI of its own, so it links no ImGui and shares
    // no ImGui context. Its inspectors are the editor's reflection UI and the
    // Node Graph window.

    // =============================================================================
    // Package-specific API, with names that do not clash when DLLs link each other
    // =============================================================================

    DEKI_FSM_API const char* DekiFsmGetName(void)
    {
        return "FSM";
    }

}  // extern "C"

#endif  // DEKI_EDITOR
}  // namespace DekiFsm
