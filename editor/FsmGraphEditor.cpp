// Editor registration for the FsmGraph state-machine asset.
//
// Registers the asset type, so the Asset Browser's Create menu offers "State
// Machine" with a valid empty graph, and the node-graph domain, so the Node
// Graph window opens this asset type and limits its add-node menu to the
// "Fsm" node categories (see FsmNodes.h). Action types live under
// "Fsm/Actions" and appear only inside a state, on the canvas you get by
// double-clicking it. Compiling needs no code here: the type has a runtime
// loader, so the data-asset path converts the JSON to a MessagePack cache.

#ifdef DEKI_EDITOR

#include <deki-editor/EditorExtension.h>
#include <deki-editor/EditorRegistry.h>

#include "deki-nodegraph/DekiNode.h"

// Editor extensions live in DekiEditor; the package's own types are in DekiFsm.
using namespace DekiFsm;

namespace DekiEditor
{

class FsmGraphAssetEditor : public AssetTypeEditor
{
public:
    const char* GetTypeName() const override { return "FsmGraph"; }
    const char* GetDisplayName() const override { return "State Machine"; }
    const char* GetExtension() const override { return ".asset"; }

    // Every graph has its three permanent lifecycle entries (the editor adds
    // back missing ones on open). Start is wired into one empty state so a new
    // machine runs at once, and that state's action flow already holds the
    // Entry node it starts from (double-click the state to see it).
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

}  // namespace DekiEditor

REGISTER_NODE_GRAPH_DOMAIN(kFsmDomain, "FsmGraph", "State Machine", "Fsm", "FsmStart");

// Registers the domain again after a plugin-only hot reload: the editor clears
// the domain registry while this DLL stays loaded, so the static registrar
// above does not run again. Register() ignores duplicates, so repeated calls
// are safe. Called from DekiFsmRegisterGraphTypes (FsmPackage.cpp).
// The exports below are C symbols at global scope; the package's own
// registration helpers and statics live in its namespace.
using namespace DekiEditor;

extern "C" void DekiFsmRegisterEditorGraphDomain(void)
{
    DekiNodeGraph::NodeGraphDomainRegistry::Instance().Register(&kFsmDomain);
}

#endif  // DEKI_EDITOR
