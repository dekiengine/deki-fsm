// The state machine interpreter (FsmComponent) running real graphs: states
// and their action flows, FINISHED and custom events, parallel lifecycle
// tracks, groups, variables, frame time handed from one action to the next,
// and every way a broken graph stops the machine instead of misbehaving.
//
// Graphs are written here as the JSON the editor saves and compiled to
// MessagePack as the asset pipeline does, then loaded through NodeGraphData.
// The action library is left out: its actions reach into objects, scenes and
// other packages. Instead the graphs use two real action types, Send Event
// and Wait, whose behaviour the tests register themselves, so the
// interpreter is what is under test. A Send Event node's eventName is used
// as a label that says what the test action does.

#include <gtest/gtest.h>

#include "FsmActions.h"
#include "FsmComponent.h"
#include "FsmGraph.h"
#include "deki-2d/ButtonComponent.h"
#include "deki-nodegraph/NodeGraphData.h"

#include <deki/Time.h>
#include <deki/TypeId.h>
#include <deki/reflection/PropertyRef.h>

#include <nlohmann/json.hpp>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

using json = nlohmann::json;
using namespace DekiFsm;

// FsmComponent's Watch Button helper calls into deki-2d, which these tests do
// not link; no test here uses a button.
void Deki2D::ButtonComponent::AddOnClickCallback(const ButtonCallback&) {}

namespace
{

// ---------------------------------------------------------------------------
// Test actions
// ---------------------------------------------------------------------------

std::vector<std::string> g_Log;
std::vector<float> g_DtSeen;

std::string Label(const void* data)
{
    return static_cast<const FsmSendEventAction*>(data)->eventName;
}

bool StartsWith(const std::string& s, const char* prefix)
{
    return s.rfind(prefix, 0) == 0;
}

// Send Event, as a labelled test action:
//   "run"           never finishes
//   "pin1"          finishes on output pin 1
//   "send:X"        raises X when it runs, then finishes
//   "enter-send:X"  raises X as it is entered
//   "dt"            records the frame time it is given
//   anything else   finishes on pin 0
constexpr size_t kLabelState = 4;
FsmActionOps LabelOps()
{
    FsmActionOps ops;
    ops.stateSize = kLabelState;
    ops.onEnter = [](const void* data, void* state, FsmContext& ctx)
    {
        const std::string label = Label(data);
        g_Log.push_back("enter:" + label);
        // The slot must come zeroed, whatever ran in it before.
        const auto* bytes = static_cast<const uint8_t*>(state);
        for (size_t i = 0; i < kLabelState; ++i)
        {
            if (bytes[i] != 0)
            {
                g_Log.push_back("dirty-state");
                break;
            }
        }
        std::memset(state, 0xAB, kLabelState);
        if (StartsWith(label, "enter-send:"))
        {
            ctx.SendEvent(label.substr(11));
        }
    };
    ops.onUpdate = [](const void* data, void*, FsmContext& ctx) -> int
    {
        const std::string label = Label(data);
        if (label == "run")
        {
            return kFsmActionRunning;
        }
        if (label == "pin1")
        {
            return 1;
        }
        if (label == "dt")
        {
            g_DtSeen.push_back(ctx.dt);
        }
        if (StartsWith(label, "send:"))
        {
            ctx.SendEvent(label.substr(5));
        }
        return 0;
    };
    ops.onExit = [](const void* data, void*, FsmContext&) { g_Log.push_back("exit:" + Label(data)); };
    return ops;
}

// Wait, as a timed action: spends the frame time it is given and passes on
// what it did not use, as the real timed actions do.
struct WaitState
{
    float elapsed;
};
FsmActionOps WaitOps()
{
    FsmActionOps ops;
    ops.stateSize = sizeof(WaitState);
    ops.onUpdate = [](const void* data, void* state, FsmContext& ctx) -> int
    {
        auto* s = static_cast<WaitState*>(state);
        const float seconds = static_cast<const FsmWaitAction*>(data)->seconds;
        s->elapsed += ctx.dt;
        if (s->elapsed < seconds)
        {
            ctx.dt = 0.0f;
            return kFsmActionRunning;
        }
        ctx.dt = s->elapsed - seconds;
        return 0;
    };
    return ops;
}

// ---------------------------------------------------------------------------
// Graph building, in the editor's JSON form
// ---------------------------------------------------------------------------

json Node(uint32_t id, const char* type, json values = json::object())
{
    return { { "id", id }, { "type", type }, { "values", values } };
}

json Link(uint32_t from, int32_t fromPin, uint32_t to)
{
    return { { "from", from }, { "fromPin", fromPin }, { "to", to }, { "toPin", 0 } };
}

json Act(uint32_t id, const std::string& label)
{
    return Node(id, "FsmSendEvent", { { "eventName", label }, { "delaySec", 0.0f } });
}

json Wait(uint32_t id, float seconds)
{
    return Node(id, "FsmWait", { { "seconds", seconds }, { "secondsVariable", "" } });
}

// A state whose flow runs `actions` (ids `entryId`+1 onwards) one after the
// other from its Entry node `entryId`.
json State(uint32_t id, const std::string& name, std::vector<std::string> transitions, uint32_t entryId,
           const std::vector<json>& actions = {}, const std::vector<json>& extraLinks = {})
{
    json nodes = json::array({ Node(entryId, "FsmActionEntry") });
    json links = json::array();
    uint32_t previous = entryId;
    for (const json& a : actions)
    {
        nodes.push_back(a);
        links.push_back(Link(previous, 0, a["id"].get<uint32_t>()));
        previous = a["id"].get<uint32_t>();
    }
    for (const json& l : extraLinks)
    {
        links.push_back(l);
    }
    json n = Node(id, "FsmState", { { "name", name }, { "transitions", transitions } });
    n["graph"] = { { "links", links }, { "nodes", nodes } };
    return n;
}

// The lifecycle entries every graph has: Awake 1, Start 2, Update 3.
json Lifecycle()
{
    return json::array({ Node(1, "FsmAwake"), Node(2, "FsmStart"), Node(3, "FsmUpdate") });
}

std::unique_ptr<FsmGraph> Compile(json nodes, json links)
{
    const json doc = { { "links", links }, { "nextNodeId", 1000 }, { "nodes", nodes } };
    const std::vector<uint8_t> blob = json::to_msgpack(doc);
    auto graph = std::make_unique<FsmGraph>();
    graph->data = DekiNodeGraph::NodeGraphData::LoadFromMemory(blob.data(), blob.size());
    return graph;
}

class FsmTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        FsmActionRegistry::Instance().Register(Deki::HashString("FsmSendEvent"), LabelOps());
        FsmActionRegistry::Instance().Register(Deki::HashString("FsmWait"), WaitOps());
    }

    void SetUp() override
    {
        g_Log.clear();
        g_DtSeen.clear();
        Deki::Time::UpdateF(16.0f);
    }

    // Loads the graph into the component; fails the test if it does not load.
    void Run(json nodes, json links)
    {
        m_Graph = Compile(std::move(nodes), std::move(links));
        ASSERT_NE(m_Graph->data, nullptr) << "the test graph did not load";
        fsm.graph = m_Graph.get();
    }

    std::vector<std::string> Log() const { return g_Log; }

    FsmComponent fsm;
    std::unique_ptr<FsmGraph> m_Graph;
};

using Strings = std::vector<std::string>;

}  // namespace

// --- Flow and FINISHED ---

TEST_F(FsmTest, ActionsRunInOrderAndFinishedMovesOn)
{
    json nodes = Lifecycle();
    nodes.push_back(State(10, "A", { "FINISHED" }, 11, { Act(12, "a1"), Act(13, "a2") }));
    nodes.push_back(State(20, "B", {}, 21, { Act(22, "run") }));
    Run(nodes, json::array({ Link(2, 0, 10), Link(10, 0, 20) }));

    fsm.Update();
    EXPECT_FALSE(fsm.Failed());
    EXPECT_EQ(fsm.ActiveStateName(), "B");
    EXPECT_EQ(Log(), (Strings{ "enter:a1", "exit:a1", "enter:a2", "exit:a2", "enter:run" }));
}

TEST_F(FsmTest, ARunningActionHoldsTheFlowUntilAnEventLeaves)
{
    json nodes = Lifecycle();
    nodes.push_back(State(10, "A", { "GO" }, 11, { Act(12, "run"), Act(13, "after") }));
    nodes.push_back(State(20, "B", {}, 21));
    Run(nodes, json::array({ Link(2, 0, 10), Link(10, 0, 20) }));

    fsm.Update();
    fsm.Update();
    EXPECT_EQ(fsm.ActiveStateName(), "A");
    EXPECT_EQ(Log(), (Strings{ "enter:run" }));  // "after" never reached

    fsm.SendEvent("GO");
    fsm.Update();
    EXPECT_EQ(fsm.ActiveStateName(), "B");
    EXPECT_EQ(Log(), (Strings{ "enter:run", "exit:run" }));  // left through onExit
}

TEST_F(FsmTest, AnActionsOutputPinChoosesTheBranch)
{
    json nodes = Lifecycle();
    nodes.push_back(State(10, "A", {}, 11, { Act(12, "pin1") },
                          { Link(12, 0, 13), Link(12, 1, 14), Link(13, 0, 15) }));
    nodes[3]["graph"]["nodes"].push_back(Act(13, "zero"));
    nodes[3]["graph"]["nodes"].push_back(Act(14, "one"));
    nodes[3]["graph"]["nodes"].push_back(Act(15, "after-zero"));
    Run(nodes, json::array({ Link(2, 0, 10) }));

    fsm.Update();
    EXPECT_EQ(Log(), (Strings{ "enter:pin1", "exit:pin1", "enter:one", "exit:one" }));
}

TEST_F(FsmTest, ATerminalStateStaysPut)
{
    json nodes = Lifecycle();
    nodes.push_back(State(10, "End", {}, 11, { Act(12, "once") }));
    Run(nodes, json::array({ Link(2, 0, 10) }));
    fsm.Update();
    fsm.Update();
    EXPECT_FALSE(fsm.Failed());
    EXPECT_EQ(fsm.ActiveStateName(), "End");
    EXPECT_EQ(Log(), (Strings{ "enter:once", "exit:once" }));  // its flow ran once, then idles
}

// --- Events ---

TEST_F(FsmTest, AnEventTheStateDoesNotListenForIsIgnored)
{
    json nodes = Lifecycle();
    nodes.push_back(State(10, "A", { "GO" }, 11, { Act(12, "run") }));
    nodes.push_back(State(20, "B", {}, 21));
    Run(nodes, json::array({ Link(2, 0, 10), Link(10, 0, 20) }));
    fsm.Update();
    fsm.SendEvent("SOMETHING_ELSE");
    fsm.Update();
    EXPECT_FALSE(fsm.Failed());
    EXPECT_EQ(fsm.ActiveStateName(), "A");
}

TEST_F(FsmTest, AnActionCanRaiseTheEventThatMovesOn)
{
    json nodes = Lifecycle();
    nodes.push_back(State(10, "A", { "DONE" }, 11, { Act(12, "send:DONE"), Act(13, "run") }));
    nodes.push_back(State(20, "B", {}, 21, { Act(22, "b") }));
    Run(nodes, json::array({ Link(2, 0, 10), Link(10, 0, 20) }));
    fsm.Update();
    EXPECT_EQ(fsm.ActiveStateName(), "B");  // raised during the frame, handled the same frame
}

TEST_F(FsmTest, AListenedEventThatIsNotWiredStopsTheMachine)
{
    json nodes = Lifecycle();
    nodes.push_back(State(10, "A", { "GO" }, 11, { Act(12, "run") }));
    Run(nodes, json::array({ Link(2, 0, 10) }));
    fsm.Update();
    fsm.SendEvent("GO");
    fsm.Update();
    EXPECT_TRUE(fsm.Failed());
}

TEST_F(FsmTest, AnEmptyEventNameStopsTheMachine)
{
    json nodes = Lifecycle();
    nodes.push_back(State(10, "A", {}, 11));
    Run(nodes, json::array({ Link(2, 0, 10) }));
    fsm.SendEvent("");
    EXPECT_TRUE(fsm.Failed());
}

TEST_F(FsmTest, AnEventCycleIsCaught)
{
    // A raises PING on entry, which enters B, which raises PING on entry...
    json nodes = Lifecycle();
    nodes.push_back(State(10, "A", { "PING" }, 11, { Act(12, "enter-send:PING"), Act(13, "run") }));
    nodes.push_back(State(20, "B", { "PING" }, 21, { Act(22, "enter-send:PING"), Act(23, "run") }));
    Run(nodes, json::array({ Link(2, 0, 10), Link(10, 0, 20), Link(20, 0, 10) }));
    fsm.Update();
    EXPECT_TRUE(fsm.Failed());
}

// --- Tracks ---

TEST_F(FsmTest, LifecycleTracksStartInAwakeStartUpdateOrder)
{
    json nodes = Lifecycle();
    nodes.push_back(State(10, "U", {}, 11, { Act(12, "update") }));
    nodes.push_back(State(20, "S", {}, 21, { Act(22, "start") }));
    nodes.push_back(State(30, "A", {}, 31, { Act(32, "awake") }));
    Run(nodes, json::array({ Link(3, 0, 10), Link(2, 0, 20), Link(1, 0, 30) }));
    fsm.Update();
    ASSERT_GE(Log().size(), 3u);
    EXPECT_EQ(Log()[0], "enter:awake");
    EXPECT_EQ(Log()[1], "enter:start");
    EXPECT_EQ(Log()[2], "enter:update");
}

TEST_F(FsmTest, FinishedStaysOnItsTrackAndEventsReachEvery)
{
    // Start: S0 finishes at once and moves to S1. Update: T0 runs forever, so
    // its own FINISHED never fires, and it moves only on GO.
    json nodes = Lifecycle();
    nodes.push_back(State(10, "S0", { "FINISHED" }, 11));
    nodes.push_back(State(20, "S1", { "GO" }, 21, { Act(22, "s1"), Act(23, "run") }));
    nodes.push_back(State(30, "T0", { "FINISHED", "GO" }, 31, { Act(32, "run") }));
    nodes.push_back(State(40, "T-finished", {}, 41, { Act(42, "t-finished") }));
    nodes.push_back(State(50, "T1", {}, 51, { Act(52, "t1") }));
    nodes.push_back(State(60, "S2", {}, 61, { Act(62, "s2") }));
    Run(nodes, json::array({ Link(2, 0, 10), Link(10, 0, 20), Link(20, 0, 60), Link(3, 0, 30), Link(30, 0, 40),
                             Link(30, 1, 50) }));

    fsm.Update();
    EXPECT_EQ(fsm.ActiveStateName(), "S1");  // track 0
    for (const auto& line : Log())
    {
        EXPECT_NE(line, "enter:t-finished");
    }

    fsm.SendEvent("GO");
    fsm.Update();
    EXPECT_EQ(fsm.ActiveStateName(), "S2");  // GO moved track 0...
    bool t1 = false;
    for (const auto& line : Log())
    {
        t1 = t1 || line == "enter:t1";
    }
    EXPECT_TRUE(t1);  // ...and track 1
}

TEST_F(FsmTest, AGraphWithNothingWiredStopsTheMachine)
{
    json nodes = Lifecycle();
    nodes.push_back(State(10, "A", {}, 11));
    Run(nodes, json::array());
    fsm.Update();
    EXPECT_TRUE(fsm.Failed());
}

// --- Groups ---

namespace
{

// Start -> Group G (exits `exits`) { In -> Inner [g] -FINISHED-> Exit `exitName` } -pin 0-> After.
json GroupGraphNodes(const std::vector<std::string>& exits, const std::string& exitName)
{
    json inner = json::array({ Node(31, "FsmGroupIn"), State(32, "Inner", { "FINISHED" }, 33, { Act(34, "g") }),
                               Node(35, "FsmGroupExit", { { "name", exitName } }) });
    json group = Node(30, "FsmGroup", { { "name", "G" }, { "exits", exits } });
    group["graph"] = { { "links", json::array({ Link(31, 0, 32), Link(32, 0, 35) }) }, { "nodes", inner } };
    json nodes = Lifecycle();
    nodes.push_back(group);
    nodes.push_back(State(40, "After", {}, 41, { Act(42, "after") }));
    return nodes;
}

}  // namespace

TEST_F(FsmTest, AGroupIsEnteredThroughGroupInAndLeftThroughItsExit)
{
    Run(GroupGraphNodes({ "out" }, "out"), json::array({ Link(2, 0, 30), Link(30, 0, 40) }));
    fsm.Update();
    EXPECT_FALSE(fsm.Failed());
    EXPECT_EQ(fsm.ActiveStateName(), "After");
    // "after" is entered as the frame ends; it runs on the next one.
    EXPECT_EQ(Log(), (Strings{ "enter:g", "exit:g", "enter:after" }));
}

TEST_F(FsmTest, AnExitTheGroupDoesNotHaveStopsTheMachine)
{
    Run(GroupGraphNodes({ "out" }, "elsewhere"), json::array({ Link(2, 0, 30), Link(30, 0, 40) }));
    fsm.Update();
    EXPECT_TRUE(fsm.Failed());
}

TEST_F(FsmTest, AGroupExitOutsideAnyGroupStopsTheMachine)
{
    json nodes = Lifecycle();
    nodes.push_back(State(10, "A", { "FINISHED" }, 11));
    nodes.push_back(Node(20, "FsmGroupExit", { { "name", "out" } }));
    Run(nodes, json::array({ Link(2, 0, 10), Link(10, 0, 20) }));
    fsm.Update();
    EXPECT_TRUE(fsm.Failed());
}

// --- Actions ---

TEST_F(FsmTest, EachActionStartsWithAZeroedState)
{
    json nodes = Lifecycle();
    nodes.push_back(State(10, "A", { "FINISHED" }, 11, { Act(12, "first"), Act(13, "second") }));
    Run(nodes, json::array({ Link(2, 0, 10), Link(10, 0, 10) }));  // and loops back into itself
    fsm.Update();
    fsm.Update();
    for (const auto& line : Log())
    {
        EXPECT_NE(line, "dirty-state");
    }
}

TEST_F(FsmTest, FrameTimeLeftByATimedActionGoesToTheNext)
{
    json nodes = Lifecycle();
    nodes.push_back(State(10, "A", {}, 11, { Wait(12, 0.3f), Act(13, "dt") }));
    Run(nodes, json::array({ Link(2, 0, 10) }));
    Deki::Time::UpdateF(500.0f);  // half a second this frame
    fsm.Update();
    ASSERT_EQ(g_DtSeen.size(), 1u);
    EXPECT_NEAR(g_DtSeen[0], 0.2f, 1e-5f);
}

TEST_F(FsmTest, ALongFrameCountsAsOneSecondAtMost)
{
    json nodes = Lifecycle();
    nodes.push_back(State(10, "A", {}, 11, { Act(12, "dt") }));
    Run(nodes, json::array({ Link(2, 0, 10) }));
    Deki::Time::UpdateF(5000.0f);
    fsm.Update();
    ASSERT_EQ(g_DtSeen.size(), 1u);
    EXPECT_FLOAT_EQ(g_DtSeen[0], 1.0f);
}

TEST_F(FsmTest, ALoopThatTakesNoTimeIsCaught)
{
    json nodes = Lifecycle();
    nodes.push_back(State(10, "A", {}, 11, { Act(12, "x"), Act(13, "y") }, { Link(13, 0, 12) }));
    Run(nodes, json::array({ Link(2, 0, 10) }));
    fsm.Update();
    EXPECT_TRUE(fsm.Failed());
}

TEST_F(FsmTest, AnActionWithNoRuntimeStopsTheMachine)
{
    json nodes = Lifecycle();
    nodes.push_back(State(10, "A", {}, 11, { Node(12, "FsmSetProperty") }));
    Run(nodes, json::array({ Link(2, 0, 10) }));
    fsm.Update();
    EXPECT_TRUE(fsm.Failed());
}

// --- Variables ---

namespace
{

json WithVariables(json nodes)
{
    json vars = Node(5, "FsmVariables");
    vars["children"] = json::array(
        { { { "enabled", true }, { "type", "FsmNumberVar" }, { "values", { { "name", "speed" }, { "value", 2.0f } } } },
          { { "enabled", true }, { "type", "FsmBoolVar" }, { "values", { { "name", "on" }, { "value", false } } } } });
    nodes.push_back(vars);
    return nodes;
}

float ReadNumber(FsmComponent& fsm, const char* name)
{
    Deki::PropertyRef ref;
    ref.field = name;
    ref.fieldHash = Deki::HashString(name);
    Deki::PropertyBinding binding;
    EXPECT_TRUE(fsm.BindVariable(ref, binding));
    return binding.field ? *static_cast<float*>(binding.field) : -1.0f;
}

}  // namespace

TEST_F(FsmTest, VariablesStartAtTheirDeclaredValues)
{
    json nodes = WithVariables(Lifecycle());
    nodes.push_back(State(10, "A", {}, 11));
    Run(nodes, json::array({ Link(2, 0, 10) }));
    fsm.Update();
    EXPECT_FLOAT_EQ(ReadNumber(fsm, "speed"), 2.0f);
}

TEST_F(FsmTest, AnObjectsOverridesReplaceTheDeclaredValues)
{
    json nodes = WithVariables(Lifecycle());
    nodes.push_back(State(10, "A", {}, 11));
    Run(nodes, json::array({ Link(2, 0, 10) }));
    fsm.variableOverrides = { "speed=0.625", "on=true" };
    fsm.Update();
    EXPECT_FALSE(fsm.Failed());
    EXPECT_FLOAT_EQ(ReadNumber(fsm, "speed"), 0.625f);
    EXPECT_FLOAT_EQ(ReadNumber(fsm, "on"), 1.0f);
}

TEST_F(FsmTest, ABadOverrideStopsTheMachine)
{
    for (const char* bad : { "speed=fast", "nope=1", "speed", "on=maybe" })
    {
        FsmComponent c;
        json nodes = WithVariables(Lifecycle());
        nodes.push_back(State(10, "A", {}, 11));
        auto graph = Compile(nodes, json::array({ Link(2, 0, 10) }));
        ASSERT_NE(graph->data, nullptr);
        c.graph = graph.get();
        c.variableOverrides = { bad };
        c.Update();
        EXPECT_TRUE(c.Failed()) << bad;
    }
}

// --- Reloading ---

TEST_F(FsmTest, AReassignedGraphStartsAgainAndForgetsAFailure)
{
    json broken = Lifecycle();
    broken.push_back(State(10, "A", {}, 11));
    Run(broken, json::array());  // nothing wired
    fsm.Update();
    ASSERT_TRUE(fsm.Failed());

    auto second = Compile(
        [] {
            json n = Lifecycle();
            n.push_back(State(10, "Fresh", {}, 11));
            return n;
        }(),
        json::array({ Link(2, 0, 10) }));
    ASSERT_NE(second->data, nullptr);
    fsm.graph = second.get();
    fsm.Update();
    EXPECT_FALSE(fsm.Failed());
    EXPECT_EQ(fsm.ActiveStateName(), "Fresh");
}
