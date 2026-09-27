#pragma once

/**
 * @brief Register the FsmGraph asset loader and the package's own actions.
 *
 * Called from deki_init_package_systems() on firmware/static builds and from
 * DekiPlugin_Init() when the package is loaded as a DLL. Idempotent.
 *
 * GLOBAL SCOPE, deliberately: the editor generates a translation unit that
 * declares these as plain `extern void DekiFsm_InitSystem();`, and that file
 * cannot know a package's namespace (see deki-tween's TweenInit.h).
 */
void DekiFsm_InitSystem();
void DekiFsm_ShutdownSystem();
