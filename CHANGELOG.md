# Changelog

Notable changes to `deki-fsm`. Engine and editor changes are in the
[engine changelog](https://github.com/dekiengine/deki-engine/blob/master/CHANGELOG.md).

A package's `minEngine` names the engine version it needs. Before 1.0 a
breaking change bumps the minor across the editor, the engine and every
package together, so a package with no changes of its own is still released
alongside one that has them.

## Unreleased

### Changed
- The asset file is read into an External Deki::Buffer while it is parsed,
  not a std::vector on the internal heap.

### Fixed
- A state machine asset loads on a device: its loader opened the path with
  `std::ifstream`, which cannot open `F:/` or `S:/`.

### Added
- **One graph, many objects.** `FsmComponent.variableOverrides` gives this
  object its own starting values for the graph's variables, one `name=value`
  per entry. An unknown name or a value of the wrong type stops the machine.
- **Actions take numbers from variables.** Wait `secondsVariable`, Tween
  Property `toVariable` and `durationVariable`, Set Property `valueVariable`,
  Modify Property `operandVariable`: when set, the named Number variable's
  value, read as the action starts, replaces the typed literal. Together with
  the overrides, a behaviour such as a bobbing sine wave is one graph that
  every object tunes (deki-demo's `assets/fsm/bob.asset` replaces its C++
  Bobber this way).

## 0.16.0

### Changed
- **Moved into the `DekiFsm` namespace.** Every component was declared at global
  scope, which made its identity a bare class name — the name a scene file
  stores and the name the registry keys on — so two packages defining one name
  collided there with nothing to tell them apart. Each component carries
  `DEKI_FORMER_NAME` with the name it was saved under before, so existing
  scenes load unchanged and are written back qualified on the next save.
  Code naming these types needs the namespace: `using namespace DekiFsm;` or a
  qualified name.
- Enum properties are stored by name rather than by number, so appending to an
  enum or reordering one no longer changes what a saved scene means. Files
  written before this still read.
- `minEngine` 0.16.0. Reflection ABI 17: the package must be rebuilt.

## 0.15.0

### Changed
- No changes of its own. Released alongside engine 0.15.0 so `minEngine`
  tracks the engine version.
