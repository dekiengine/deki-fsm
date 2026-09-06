#pragma once

#include "deki-nodegraph/NodeGraphData.h"

// A state-machine graph as a loadable asset. The .asset (JSON,
// "type":"FsmGraph") is authored in the editor's Node Graph window and
// compiled to MessagePack by the generic data-asset path; at runtime the
// loader parses it into type-erased node instances (states + their action
// stacks, see FsmNodes.h / FsmActions.h) plus the link table. FsmComponent
// interprets it; nothing here is editor-only.
struct FsmGraph
{
    // Asset type name for AssetRef<FsmGraph> / AssetManager lookup. Matches
    // the ".asset" file's "type" field, the runtime loader registration, and
    // the editor's node-graph domain registration.
    static constexpr const char* AssetTypeName = "FsmGraph";

    NodeGraphData* data = nullptr;

    ~FsmGraph() { delete data; }
};
