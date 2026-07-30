/**
 * @file FsmGraphEditor.cpp
 * @brief Editor-side registration for the FsmGraph state-machine asset.
 *
 * Registers (a) the asset type so the Asset Browser's Create... menu offers
 * "State Machine" with a valid empty graph, and (b) the node-graph domain so
 * the generic Node Graph window claims this asset type and scopes its add-node
 * menu to the "Fsm" node categories (see FsmNodes.h; action types live under
 * "Fsm/Actions" and appear only INSIDE a state, on the canvas you get by
 * double-clicking it). Compilation needs no code here: the type has a runtime
 * loader, so the generic data-asset path transcodes the JSON to a MessagePack
 * cache.
 */

#ifdef DEKI_EDITOR

#include <deki-editor/EditorExtension.h>
#include <deki-editor/EditorRegistry.h>

#include "deki-nodegraph/DekiNode.h"

namespace DekiEditor
{

class FsmGraphAssetEditor : public AssetTypeEditor
{
public:
    const char* GetTypeName() const override    { return "FsmGraph"; }
    const char* GetDisplayName() const override { return "State Machine"; }
    const char* GetExtension() const override   { return ".asset"; }

    // Every graph carries its three permanent lifecycle entries (the editor
    // re-seeds missing ones on open); Start comes wired into one empty state
    // so a fresh machine runs immediately. The state ships with its action
    // flow already containing the Entry node it starts from — double-click the
    // state to get there.
    const char* GetDefaultContent() const override
    {
        return R"({
  "links": [
    { "from": 2, "fromPin": 0, "to": 4, "toPin": 0 }
  ],
  "nextNodeId": 7,
  "nodes": [
    { "id": 1, "type": "FsmAwake", "values": {}, "x": 60.0, "y": 40.0 },
    { "id": 2, "type": "FsmStart", "values": {}, "x": 60.0, "y": 170.0 },
    { "id": 3, "type": "FsmUpdate", "values": {}, "x": 60.0, "y": 300.0 },
    { "id": 4, "type": "FsmState",
      "graph": {
        "links": [],
        "nodes": [
          { "id": 6, "type": "FsmActionEntry", "values": {}, "x": 60.0, "y": 60.0 }
        ]
      },
      "values": { "name": "Idle", "transitions": [] }, "x": 300.0, "y": 170.0 },
    { "id": 5, "type": "FsmVariables", "children": [], "values": {}, "x": 60.0, "y": 430.0 }
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

// Re-registration hook for plugin-only hot reload: the editor wipes the domain
// registry while this DLL stays loaded, so the static registrar above never
// reruns. Registry Register() dedupes, so calling this repeatedly is safe.
// Invoked from DekiFsm_RegisterGraphTypes (FsmModule.cpp).
extern "C" void DekiFsm_RegisterEditorGraphDomain(void)
{
    NodeGraphDomainRegistry::Instance().Register(&g_FsmDomain);
}

#endif // DEKI_EDITOR
