# deki-fsm

PlayMaker-style finite state machines for Deki Engine.

Put an `FsmComponent` on any object and assign it a **State Machine** asset
(`FsmGraph`), authored in the editor's Node Graph window:

- **States** are canvas nodes. Exactly one is active per component.
- Each state holds an ordered **action stack** (edited in the state's
  inspector): Wait, Send Event, Set Property, Compare Property, Move To,
  Watch Button. Actions run every frame in stack order.
- **Transitions** are wires labeled with event names — one output pin per
  entry in the state's `transitions` list. `FINISHED` fires automatically
  when every action in the state has finished; actions and game code raise
  custom events (`FsmComponent::SendEvent`).

Set Property / Compare Property drive any `DEKI_EXPORT` field of any
component through reflection, so most gameplay glue needs no C++. Game
projects add their own actions by declaring a `DEKI_NODE` struct with
category `"Fsm/Actions"` and registering runtime ops with
`REGISTER_FSM_ACTION` (see `FsmActionRegistry.h`).

Failure policy: a broken graph (missing Start, unwired transition, unknown
action, bad target/property name) logs one error and stops that machine.
No fallbacks.

Requires: `deki-2d` (Watch Button), `deki-tween` (Move To easing).
