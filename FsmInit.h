#pragma once

/// Registers the FsmGraph asset loader and the package's own actions. Safe to
/// call more than once. Called from DekiInitPackageSystems() on static builds
/// and from DekiPluginInit() when the package is a DLL.
///
/// Global scope on purpose: the editor generates a file that declares these as
/// plain `extern void DekiFsmInitSystem();`, and that file cannot know a
/// package's namespace (see deki-tween's TweenInit.h).
void DekiFsmInitSystem();
void DekiFsmShutdownSystem();
