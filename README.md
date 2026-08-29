# deki-fsm

PlayMaker-style finite state machines for Deki Engine.

Put an `FsmComponent` on any object and assign it a **State Machine** asset
(`FsmGraph`), authored in the editor's Node Graph window.

## Two levels of canvas

A graph is a tree of canvases, and you move between them by **double-clicking a
node**; the breadcrumb above the canvas walks back out.

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

- Every action has one input and one or more **output pins**. When an action
  finishes it reports which pin it finished on, and control moves to whatever
  that pin is wired to. A run of instant actions completes in one frame.
- **Branching is pins, not events.** Compare Property has a `true` pin and a
  `false` pin; wire each to a different action. There are no `eventIfTrue`-style
  fields anywhere in the library.
- A flow is a graph, so it may **loop**. Re-entering an action resets its
  runtime state exactly as if it were entered for the first time. A loop with
  nothing time-consuming in it trips a guard at 256 steps in one frame.
- An action that never finishes (Watch Button, anything with `everyFrame` on)
  **parks** the flow on itself, and nothing downstream runs. That is how a
  per-frame watcher is written: park on it, in a state of its own, typically on
  a track wired from `Update`.
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

Groups nest, run no actions of their own, and cost nothing at runtime. Entering
one continues straight through Group In; collapsing part of a machine into a
group never changes what the machine does.

## Parallel tracks

`Awake`, `Start` and `Update` are permanent lifecycle entries, exactly like the
hooks of a `DekiBehaviour`. Each **wired** output begins its own track: an
independent state flow with its own active state, all running side by side on
one component, entered in that order. An unwired entry is an unused hook, not an
error. Custom events broadcast to every track; `FINISHED` is per-track.

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

Not covered yet: audio (the audio module only exposes raw PCM, with no sound
asset type to point an action at) and physics (there is no physics module).

## Variables

The **Variables** node (permanent, one per graph, at the root) holds the graph's
variables as a child stack in its inspector: add a Number, Bool or Text entry
and give it a name and an initial value. Variables belong to the whole document,
so they are reachable from inside every state's action flow and every group.
Each `FsmComponent` gets its own live copy, so two objects running the same
graph never share state.

Variables are addressed by the *same* PropertyRef as everything else (component
"Variable", field = the name), which is why there are no variable-specific
actions: a score counter is **Modify Property** on a variable, "is the score
10?" is **Compare Property** on the same one, and copying a variable into a
`TextComponent` is **Set Property**.

## Targets, not bespoke verbs

Set Property, Compare Property and Tween Property all address their target the
same way: a **PropertyRef**, picked in the inspector as three dropdowns —
object, then component, then field. There is no typing of class names, so an
invalid reference cannot be authored, and the value editor below it becomes
typed to whatever you picked (a drag field for a float, a checkbox for a bool, a
dropdown for an enum, two drags for a Vector2).

A reference can point at three kinds of thing, all through the same three rows:

- **A component's field** — anything `DEKI_EXPORT`ed, on any object.
- **The object's own Transform** — `position`, `x`, `y`, `rotation`, `scale`,
  `scale_x`, `scale_y`, `active`. So "move this object" is Tween Property on
  Transform / Position rather than a dedicated Move To action, "spin it" is the
  same action on Rotation, and "hide it" is Set Property on Active. `position`
  and `scale` are Vector2 targets that drive both axes in one action.
- **A graph variable** — see above.

Each reference is resolved **once**, when the action starts: object lookup,
field lookup and literal parsing all happen there, so the per-frame path is a
store or a compare through a cached pointer. Nothing touches a string while a
state is running, which is what makes the action set cheap enough for the
ESP32-S3.

## Adding your own actions

Declare a `DEKI_NODE` struct with category `"Fsm/Actions"`, give it
`DEKI_NODE_INPUTS("in")` plus one `DEKI_NODE_OUTPUTS(...)` label per outcome,
and register runtime ops with `REGISTER_FSM_ACTION` (see `FsmActionRegistry.h`).
`onUpdate` returns `kFsmActionRunning` while the action is still going, else the
index of the output pin it finished on.

Failure policy: a broken graph (nothing wired to run, a wire into a node that is
not a State/Group/Group Exit, an unwired transition or group exit, an unknown
action, a bad target or property name, an event or action-flow storm) logs one
error and stops that machine. No fallbacks.

Requires: `deki-nodegraph`, `deki-2d` (Watch Button), `deki-tween` (Tween
Property easing).
