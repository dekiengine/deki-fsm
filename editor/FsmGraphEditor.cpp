/**
 * @file FsmGraphEditor.cpp
 * @brief Editor-side registration for the FsmGraph state-machine asset.
 *
 * Registers (a) the asset type so the Asset Browser's Create... menu offers
 * "State Machine" with a valid empty graph, and (b) the node-graph domain so
 * the generic Node Graph window claims this asset type and scopes its add-node
 * menu to the "Fsm" node categories (see FsmNodes.h; action types live under
 * "Fsm/Actions" and appear only inside a state's action stack). Compilation
 * needs no code here: the type has a runtime loader, so the generic data-asset
 * path transcodes the JSON to a MessagePack cache.
 */

#ifdef DEKI_EDITOR

#include <deki-editor/EditorExtension.h>
#include <deki-editor/EditorRegistry.h>

#include "reflection/DekiNode.h"

namespace DekiEditor
{

class FsmGraphAssetEditor : public AssetTypeEditor
{
public:
    const char* GetTypeName() const override    { return "FsmGraph"; }
    const char* GetDisplayName() const override { return "State Machine"; }
    const char* GetExtension() const override   { return ".asset"; }

    // A minimal valid graph: Start wired into one empty state.
    const char* GetDefaultContent() const override
    {
        return R"({
  "links": [
    { "from": 1, "fromPin": 0, "to": 2, "toPin": 0 }
  ],
  "nextNodeId": 3,
  "nodes": [
    { "id": 1, "type": "FsmStart", "values": {}, "x": 60.0, "y": 120.0 },
    { "id": 2, "type": "FsmState", "values": { "name": "Idle", "transitions": ["FINISHED"] }, "x": 300.0, "y": 120.0 }
  ],
  "type": "FsmGraph"
})";
    }

    int GetCompileTarget() const override { return 2; }  // Data
};

REGISTER_EDITOR(FsmGraphAssetEditor)

} // namespace DekiEditor

REGISTER_NODE_GRAPH_DOMAIN(g_FsmDomain,
                           "FsmGraph", "State Machine",
                           "Fsm", "FsmStart");

#endif // DEKI_EDITOR
