#include <keels2/authoring.hpp>

namespace docs
{
using namespace keels2::authoring;
class Interfaces final : public Plugin
{
public:
    static constexpr PluginInfo Info{"Docs Interfaces", "KeelS2 documentation", "1.0.0", "Acquire borrowed Source 2 interfaces"};
    bool Load() override
    {
        auto* cvars = GetCVarSystem<ICvar>();
        auto* server = GetSource2Server<IServerGameDLL>();
        auto* clients = GetSource2GameClients<IServerGameClients>();
        auto* namedCvars = GetEngineInterface<ICvar>(CVAR_INTERFACE_VERSION);
        auto* namedServer = GetServerInterface<IServerGameDLL>(INTERFACEVERSION_SERVERGAMEDLL);
        if (!cvars || !server || !clients || !namedCvars || !namedServer)
        {
            LogError("An expected interface is unavailable: {}", LastError());
            return false;
        }
        LogMessage("Source 2 interfaces acquired.");
        return true;
    }
};
}
KEELS2_PLUGIN(docs::Interfaces)
