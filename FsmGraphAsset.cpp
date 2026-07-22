#include "FsmGraph.h"
#include "FsmNodes.h"     // pulls in the state node registrations (NodeFactory)
#include "FsmActions.h"   // pulls in the action registrations (NodeFactory)

#include "assets/AssetManager.h"
#include "DekiLogSystem.h"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <vector>

// Runtime loader for the FsmGraph state-machine asset. Mirrors the plain
// data-asset loaders: the editor compiles the ".asset" JSON to a MessagePack
// cache via the generic path, and here we parse that cache with the generic
// NodeGraphData loader (which creates state/action instances via NodeFactory).
// Same path on desktop and device. A malformed graph loads as nullptr, loudly.

namespace
{
    FsmGraph* LoadGraphFromMemory(const uint8_t* data, size_t size)
    {
        NodeGraphData* graphData = NodeGraphData::LoadFromMemory(data, size);
        if (!graphData)
        {
            DEKI_LOG_ERROR("FsmGraph: failed to load state machine asset");
            return nullptr;
        }

        auto* graph = new FsmGraph();
        graph->data = graphData;
        return graph;
    }

    struct _FsmGraphLoaderReg
    {
        _FsmGraphLoaderReg()
        {
            auto pathLoader = [](const char* p) -> void*
            {
                std::ifstream f(p, std::ios::binary | std::ios::ate);
                if (!f.is_open())
                    return nullptr;
                std::streamsize n = f.tellg();
                f.seekg(0, std::ios::beg);
                std::vector<uint8_t> buf(static_cast<size_t>(n));
                if (!f.read(reinterpret_cast<char*>(buf.data()), n))
                    return nullptr;
                return LoadGraphFromMemory(buf.data(), static_cast<size_t>(n));
            };
            auto unloader  = [](void* a) { delete static_cast<FsmGraph*>(a); };
            auto memLoader = [](const uint8_t* d, size_t n) -> void* { return LoadGraphFromMemory(d, n); };

            Deki::AssetManager::RegisterLoader("FsmGraph", pathLoader, unloader, memLoader);
        }
    };
    static _FsmGraphLoaderReg s_fsmGraphLoaderReg;
}
