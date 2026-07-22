/**
 * @file FsmModule.cpp
 * @brief Module entry point for deki-fsm DLL.
 *
 * Exports the standard Deki plugin interface so the editor can load
 * deki-fsm.dll and register its components (FsmComponent). For linked DLLs
 * (not dynamically loaded), DekiFsm_EnsureRegistered() must be called from the
 * main executable to trigger the static initializers.
 */

#include "interop/DekiPlugin.h"
#include "DekiModuleFeatureMeta.h"
#include "FsmModule.h"
#include "FsmComponent.h"
#include "reflection/ComponentRegistry.h"
#include "reflection/ComponentFactory.h"

#ifdef DEKI_EDITOR

// Auto-generated registration helpers
extern void DekiFsm_RegisterComponents();
extern int DekiFsm_GetAutoComponentCount();
extern const DekiComponentMeta* DekiFsm_GetAutoComponentMeta(int index);

static bool s_FsmRegistered = false;

extern "C" {

/**
 * @brief Ensure deki-fsm module is loaded and components are registered.
 * @return Number of components registered by this module
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
    return "Deki FSM Module";
}

DEKI_PLUGIN_API const char* DekiPlugin_GetVersion(void)
{
#ifdef DEKI_MODULE_VERSION
    return DEKI_MODULE_VERSION;
#else
    return "0.0.0-dev";
#endif
}

DEKI_PLUGIN_API const char* DekiPlugin_GetReflectionJson(void)
{
    // Not used - we use component metadata instead
    return "{}";
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
}

DEKI_PLUGIN_API void DekiPlugin_OnPlayModeStop(void)
{
    // Machine state lives per FsmComponent instance; instances die with the
    // play-mode prefab, so there is nothing global to reset here.
}

// deki-fsm renders no editor UI of its own, so it links no ImGui and shares no
// ImGui context. Its inspectors are the editor's generic reflection UI and the
// generic Node Graph window.

// =============================================================================
// Module Feature API
// =============================================================================

static const char* s_FsmGuids[] = { FsmComponent::StaticGuid };

static const DekiModuleFeatureInfo s_Features[] = {
    {"fsm", "FSM", "PlayMaker-style state machines with action stacks", true, "DEKI_FEATURE_FSM", s_FsmGuids, 1},
};

DEKI_PLUGIN_API int DekiPlugin_GetFeatureCount(void)
{
    return sizeof(s_Features) / sizeof(s_Features[0]);
}

DEKI_PLUGIN_API const DekiModuleFeatureInfo* DekiPlugin_GetFeature(int index)
{
    if (index < 0 || index >= DekiPlugin_GetFeatureCount())
        return nullptr;
    return &s_Features[index];
}

// =============================================================================
// Module-specific feature API (for linked DLL access without name conflicts)
// =============================================================================

DEKI_FSM_API const char* DekiFsm_GetName(void)
{
    return "FSM";
}

DEKI_FSM_API int DekiFsm_GetFeatureCount(void)
{
    return DekiPlugin_GetFeatureCount();
}

DEKI_FSM_API const DekiModuleFeatureInfo* DekiFsm_GetFeature(int index)
{
    return DekiPlugin_GetFeature(index);
}

} // extern "C"

#endif // DEKI_EDITOR
