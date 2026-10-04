#pragma once

#include "deki-nodegraph/NodeGraphData.h"

namespace DekiFsm
{

/// A state-machine graph as a loadable asset. The .asset (JSON,
/// "type":"FsmGraph") is authored in the editor's Node Graph window and
/// compiled to MessagePack like other data assets. At runtime the loader turns
/// it into node instances (states and their action stacks, see FsmNodes.h and
/// FsmActions.h) plus the link table, and FsmComponent runs it.
struct FsmGraph
{
    // Asset type name for AssetRef<FsmGraph> and AssetManager lookup. Must
    // match the .asset file's "type" field, the runtime loader registration
    // and the editor's node-graph domain registration.
    static constexpr const char* kAssetTypeName = "FsmGraph";

    DekiNodeGraph::NodeGraphData* data = nullptr;

    ~FsmGraph() { delete data; }
};

/// Registers the FsmGraph asset loader. Safe to call more than once. Called
/// from DekiFsmInitSystem (FsmInit.h); that call is also what links
/// FsmGraphAsset.cpp and its state and action registrations into a firmware.
void RegisterGraphLoader();

}  // namespace DekiFsm
