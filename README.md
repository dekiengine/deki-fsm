# deki-fsm

Docs: https://dekiengine.github.io/deki-fsm/ (components and properties, generated from the code)

PlayMaker-style finite state machines for Deki Engine.

Put an `FsmComponent` on an object and give it a **State Machine** asset
(`FsmGraph`), authored in the editor's Node Graph window.

## Two levels of canvas

A graph is a tree of canvases. Double-click a node to go in, use the
breadcrumb to come back out.

- **The root** is the flow: `Awake` / `Start` / `Update` entries, **States**,
  **Groups**, and the transition wires between them.
- **Inside a State** is its **action flow**: the actions are nodes there, wired
  one to the next.
- **Inside a Group** are more states, exactly like the root.

Nothing runs from a hidden list. If it runs, it is a node on some canvas.

## States and transitions

- **States** are nodes on the flow canvas. Each track has exactly one active.
- **Transitions** are wires labeled with event names, one output pin per entry
  in the state's `transitions` list. `FINISHED` fires automatically when the
  state's action flow runs off its end; other events come from the **Send
  Event** action or from game code (`FsmComponent::SendEvent`).
- A state with no transitions is terminal: the track parks there.

## Action flows

Double-click a state to open its flow. It starts at the permanent **Entry**
node and follows the wires:

- Every action has one input and one or more **output pins**. It finishes on
  one of them, and control moves to whatever that pin is wired to. A run of
  instant actions completes in one frame.
- **Branching is pins, not events.** Compare Property has a `true` pin and a
  `false` pin; wire each to a different action. There are no `eventIfTrue`-style
  fields anywhere in the library.
- Flows can **loop**. Re-entering an action resets it as if it were the first
  time. A loop with nothing slow in it hits a guard at 256 steps per frame.
- An action that never finishes (Watch Button, anything with `everyFrame` on)
  **parks** the flow on itself and nothing downstream runs. That is how you
  write a per-frame watcher: park on it, in its own state, on a track wired
  from `Update`.
- Running off an **unwired** pin ends the flow and raises `FINISHED`. An empty
  flow (Entry wired to nothing) is a legitimate "just wait for an event" state.

## Groups

A **Group** is a state-shaped box holding a whole sub-flow. It takes one input
like a state and has one output pin per **exit**; double-click it to work on the
states inside.

- **Group In** (permanent, one per group) is the inside of the group's input:
  wire it to the first state.
- **Group Exit** nodes are the inside of its output pins, matched **by name** to
  the group's `exits` list. Wire a state's transition to an Exit and the flow
  leaves the group through the matching pin outside.

Groups nest, run no actions themselves and cost nothing at runtime.
Collapsing part of a machine into a group never changes what it does.

## Parallel tracks

`Awake`, `Start` and `Update` are permanent entries, the same hooks as a
`Deki::Component`. Each wired output starts its own track: an independent flow
with its own active state, all running side by side, entered in that order. An
unwired entry is just an unused hook. Custom events reach every track,
`FINISHED` is per-track.

## Actions

| Action | What it does | Pins out |
|---|---|---|
| **Wait** | Pause for N seconds. | done |
| **Send Event** | Raise an event on this machine, optionally after a delay. The only action that raises one. | done |
| **Send Event To** | Raise an event on *another* object's machine. Door tells room. | done |
| **Set Property** | Write a value to any field, transform value or variable. | done |
| **Modify Property** | Arithmetic on any numeric target: add, subtract, multiply, divide, min, max. Score and health live here. | done |
| **Random Property** | Write a random number into any numeric target; `wholeNumbers` for die rolls. | done |
| **Compare Property** | The `if` of the graph: branch on any component's live data. `waitUntilTrue` turns it into a gate that parks until the comparison holds. | true, false |
| **Tween Property** | Ease any float or Vector2 target to a value over time, with easing. Movement, spin, scale, fade. | done |
| **Spawn Scene** | Instantiate a scene at a position, optionally renaming the new root so later actions can find it. | done |
| **Destroy Object** | Remove an object and its children from the running scene. | done |
| **Set Parent** | Reparent an object; an empty parent moves it to the scene root. | done |
| **Play Animation** | Pick a sequence on an `AnimationComponent` and play it, optionally waiting for it to finish. | done |
| **Watch Button** | Park until the button is clicked. The input-to-transition bridge: wire `clicked` to a Send Event. | clicked |
| **Log** | Write a line to the console. Print-debugging for graphs. | done |

Audio and physics actions arrive when there is something to point them at:
the audio package is raw PCM today, and physics is still to come.

## Variables

The **Variables** node (one per graph, at the root) holds them: add a Number,
Bool or Text entry with a name and a starting value. They are visible from
every flow and group in the document, and each `FsmComponent` gets its own
copy, so two objects running the same graph never share state.

Variables use the same PropertyRef as everything else (component "Variable",
field = the name), so there are no variable-specific actions. A score counter
is **Modify Property** on a variable, "is the score 10?" is **Compare
Property** on it, and copying it into a `TextComponent` is **Set Property**.

### One graph, many objects

**Variable Overrides** on an `FsmComponent` give that object its own starting
values, one `name=value` per entry (`speedHz=0.625`). An unknown name or a
wrong type stops the machine.

Actions can read their numbers from variables too: **Wait** `secondsVariable`,
**Tween Property** `toVariable` / `durationVariable`, **Set Property**
`valueVariable`, **Modify Property** `operandVariable`. The variable is read
when the action starts and replaces the typed literal. One graph, tuned per
object: deki-demo's `assets/fsm/bob.asset` is its C++ Bobber as a state
machine, driven entirely by `amplitude`, `speedHz` and `phase`.

## Targets

Set Property, Compare Property and Tween Property all pick their target the
same way: a **PropertyRef**, three dropdowns in the inspector - object,
component, field. You never type a class name, so you cannot author a broken
reference, and the value editor under it matches the type you picked.

A reference points at one of three things:

- **A component's field** - anything `DEKI_EXPORT`ed, on any object.
- **The object's Transform** - `position`, `x`, `y`, `rotation`, `scale`,
  `scaleX`, `scaleY`, `active`. So "move it" is Tween Property on
  Transform/Position instead of a Move To action, "spin it" is the same action
  on Rotation, "hide it" is Set Property on Active. `position` and `scale` take
  both axes at once.
- **A graph variable** - see above.

References resolve **once**, when the action starts: object lookup, field
lookup and parsing all happen there. After that it is a store or a compare
through a cached pointer, with no strings touched while the machine runs.
That is what keeps it cheap enough for an ESP32-S3.

## Adding your own actions

Declare a `DEKI_NODE` struct with category `"Fsm/Actions"`, give it
`DEKI_NODE_INPUTS("in")` plus one `DEKI_NODE_OUTPUTS(...)` label per outcome,
and register runtime ops with `REGISTER_FSM_ACTION` (see `FsmActionRegistry.h`).
`onUpdate` returns `kFsmActionRunning` while the action is still going, else the
index of the output pin it finished on.

A broken graph logs one error and stops that machine. No fallbacks. Broken
means: nothing wired to run, a wire into something that is not a
State/Group/Group Exit, an unwired transition or exit, an unknown action, a
bad target or property name, or an event storm.

Requires: `deki-nodegraph`, `deki-2d` (Watch Button), `deki-tween` (Tween
Property easing).

## Dependencies

| Dependency | Type |
|---|---|
| `deki-nodegraph` | Deki package |
| `deki-2d` | Deki package |
| `deki-tween` | Deki package |

## Namespace

Types live in `DekiFsm`. Scene files store the qualified name, and so does code:

```cpp
using namespace DekiFsm;
obj->AddComponent<SomeComponent>();
```

Scenes saved before 0.16.0 used bare names and still load; saving writes the current one.

