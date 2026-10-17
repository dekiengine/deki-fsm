// The text the state machine node types show in the Node Graph window: their
// names, descriptions, pin labels, menu groups, field names and enum values.
//
// The node types are declared in FsmNodes.h and FsmActions.h, which are
// compiled for devices and cannot mark text for translation, so the text is
// listed here, where tools/ci/check_translations.py finds it and
// editor/lang/ja.json translates it. The Node Graph window passes it through
// Tr where it draws it. Keep this list in step with those headers.
//
// The Ease values come from deki-tween, which translates them.

#ifdef DEKI_EDITOR

#include <deki-editor/Localization.h>

namespace DekiEditor
{
namespace
{

[[maybe_unused]] constexpr const char* kFsmNodeText[] = {
    // Wait
    TrMark("Wait"),
    TrMark("Do nothing for a set number of seconds, then continue."),
    TrMark("in"),
    TrMark("done"),
    TrMark("Actions"),
    TrMark("Seconds"),
    TrMark("Seconds Variable"),
    // Send Event
    TrMark("Send Event"),
    TrMark("Raise an event on this FSM, optionally after a delay."),
    TrMark("Event Name"),
    TrMark("Delay Sec"),
    // Set Property
    TrMark("Set Property"),
    TrMark("Write a value into any component field or variable."),
    TrMark("Target"),
    TrMark("Value"),
    TrMark("Value Variable"),
    TrMark("Every Frame"),
    // Compare Property
    TrMark("Compare Property"),
    TrMark("The if: compare a field, then continue down true or false."),
    TrMark("true"),
    TrMark("false"),
    TrMark("Compare"),
    TrMark("Equals"),
    TrMark("NotEquals"),
    TrMark("Less"),
    TrMark("Greater"),
    TrMark("Wait Until True"),
    // Tween Property
    TrMark("Tween Property"),
    TrMark("Ease a numeric field to a new value over time."),
    TrMark("To"),
    TrMark("To Variable"),
    TrMark("Duration"),
    TrMark("Duration Variable"),
    TrMark("Ease"),
    TrMark("Relative"),
    // Modify Property
    TrMark("Modify Property"),
    TrMark("Do arithmetic on a number field or variable. The score and health workhorse."),
    TrMark("Operation"),
    TrMark("Add"),
    TrMark("Subtract"),
    TrMark("Multiply"),
    TrMark("Divide"),
    TrMark("Min"),
    TrMark("Operand"),
    TrMark("Operand Variable"),
    // Random Property
    TrMark("Random Property"),
    TrMark("Write a random number into a number field or variable."),
    TrMark("Max"),
    TrMark("Whole Numbers"),
    // Spawn Scene
    TrMark("Spawn Scene"),
    TrMark("Instantiate a scene into the running scene."),
    TrMark("Scene"),
    TrMark("X"),
    TrMark("Y"),
    TrMark("Spawned Name"),
    // Destroy Object
    TrMark("Destroy Object"),
    TrMark("Remove an object and its children from the scene."),
    TrMark("Target Object"),
    // Set Parent
    TrMark("Set Parent"),
    TrMark("Move an object under a new parent, or out to the root."),
    TrMark("New Parent"),
    // Play Animation
    TrMark("Play Animation"),
    TrMark("Play an animation on the target, optionally waiting for it to finish."),
    TrMark("Sequence"),
    TrMark("Loop"),
    TrMark("Wait For Finish"),
    // Send Event To
    TrMark("Send Event To"),
    TrMark("Raise an event on another object's FSM."),
    // Log
    TrMark("Log"),
    TrMark("Write a line to the console."),
    TrMark("Message"),
    // Watch Button
    TrMark("Watch Button"),
    TrMark("Wait here until the target button is clicked."),
    TrMark("clicked"),
    TrMark("Button Object"),
    // Start
    TrMark("Start"),
    TrMark("Main flow entry, like Start(). Wire it to the machine's first state."),
    TrMark("start"),
    TrMark("Flow"),
    // Awake
    TrMark("Awake"),
    TrMark("Setup flow entry, like Awake(). Its track runs before Start and Update."),
    // Update
    TrMark("Update"),
    TrMark("Per-frame flow entry, like Update(). Where watcher states live."),
    // State
    TrMark("State"),
    TrMark("One state. Holds its actions inside, and one output per transition event."),
    TrMark("Name"),
    TrMark("Transitions"),
    TrMark("Transition"),
    // Entry
    TrMark("Entry"),
    TrMark("Where a state's action flow starts."),
    TrMark("run"),
    // Group
    TrMark("Group"),
    TrMark("A box holding a sub-flow of states. Tidies the canvas, changes nothing at runtime."),
    TrMark("Exits"),
    TrMark("Exit"),
    // Group In
    TrMark("Group In"),
    TrMark("The inside of a group's input: wire it to the group's first state."),
    // Group Exit
    TrMark("Group Exit"),
    TrMark("Leaves the group through the output pin with this name."),
    // Variables
    TrMark("Variables"),
    TrMark("The graph's variables and their starting values."),
    // Number
    TrMark("Number"),
    TrMark("A number variable."),
    // Bool
    TrMark("Bool"),
    TrMark("A true or false variable."),
    // Text
    TrMark("Text"),
    TrMark("A text variable."),
};

}  // namespace
}  // namespace DekiEditor

#endif  // DEKI_EDITOR
