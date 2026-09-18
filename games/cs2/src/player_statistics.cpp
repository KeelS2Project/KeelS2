#include <keels2/cs2/player_statistics.h>
#include <keels2/cs2/entity_writes.h>

namespace keels2::cs2
{
KeelResult ResolvePlayerStatistics(const platform::LoadedModule& module, const std::string& profile,
    KeelCs2PlayerStatisticsBindings& bindings, std::string& error)
{
    bindings = {};
    // The existing writer resolver validates the full exact Ubuntu fingerprint
    // and the NetworkStateChanged entry bytes. It rejects Windows/other builds.
    void* notify{};
    auto result = ResolveEntityWrites(module,profile,notify,error);
    if (result != KEEL_RESULT_OK) return result;
    void** controller{};
    void** money{};
    void** tracking{};
    if (platform::FindPrimaryVtable(module,"CCSPlayerController",30,controller,error) != platform::ModuleLookup::found ||
        platform::FindPrimaryVtable(module,"CCSPlayerController_InGameMoneyServices",3,money,error) != platform::ModuleLookup::found ||
        platform::FindPrimaryVtable(module,"CCSPlayerController_ActionTrackingServices",3,tracking,error) != platform::ModuleLookup::found)
        return KEEL_RESULT_INCOMPATIBLE;
    if (controller[29] != notify)
    {
        error = "player statistics notification method is incompatible";
        return KEEL_RESULT_INCOMPATIBLE;
    }
    bindings = {controller,money,tracking,notify};
    return KEEL_RESULT_OK;
}
}
