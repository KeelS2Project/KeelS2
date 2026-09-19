#include "game_adapter_loader.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace
{

std::uint32_t dispatches{};

std::uint32_t Begin() noexcept
{
    ++dispatches;
    return 1;
}

void End() noexcept
{
    --dispatches;
}

}

int main(int argument_count, char** arguments)
{
    if (argument_count != 2)
    {
        return 1;
    }
#if defined(_WIN32)
    constexpr const char* platform = "win64";
#else
    constexpr const char* platform = "linuxsteamrt64";
#endif
    const keels2::host::GameAdapterHostApi host{
        sizeof(keels2::host::GameAdapterHostApi),
        keels2::host::kGameAdapterAbiVersion,
        &Begin,
        &End
    };
    keels2::host::GameAdapterModule module;
    std::string error;
    if (!module.Load(arguments[1], "synthetic", platform, host, error) ||
        !module.Get() || std::string(module.Get()->Name()) != "synthetic" ||
        module.Path().parent_path() != std::filesystem::path(arguments[1]))
    {
        return 2;
    }
    std::int32_t slot{100};
    if (module.SupportsClientCommands() ||
        module.CommandCaller(nullptr, slot) != KEEL_RESULT_OK || slot != -1)
    {
        return 9;
    }
    if (module.EntityConstruction().size || module.EntityConstruction().ready) return 20;
    if (module.EntityInput().size || module.EntityInput().dispatch) return 21;
    KeelHostCompatibilityInfo compatibility{};
    const keels2::host::GameEntityIdentity entity{};
    const KeelPlayerAction action{sizeof(KeelPlayerAction), KEELS2_PLAYER_ACTION_KILL, {}, 0};
    if (module.PlayerAction(entity, action) != KEEL_RESULT_UNSUPPORTED)
        return 10;
    bool accessed = false;
    const keels2::host::GameEntityAccessRequest access{entity, "CCSPlayerPawn"};
    if (module.VisitEntities(&access, 1, [](void* data, void* const*, std::uint32_t) {
        *static_cast<bool*>(data) = true; return KEEL_RESULT_OK;
    }, &accessed) != KEEL_RESULT_UNSUPPORTED || accessed) return 16;
    keels2::host::GameEntityIdentity captured{3,4,5};
    if (module.CaptureEntity(&captured, captured) != KEEL_RESULT_UNSUPPORTED ||
        captured.index || captured.source2_handle || captured.epoch) return 17;
    KeelDamageInfo damage{}; damage.size = sizeof(damage);
    const KeelDamageEdit edit{sizeof(edit),0,1,2,{1,2,3},{4,5,6}};
    KeelBool matches = KEEL_TRUE;
    if (module.ReadDamage(&damage,damage) != KEEL_RESULT_UNSUPPORTED ||
        module.WriteDamage(&damage,edit) != KEEL_RESULT_UNSUPPORTED ||
        module.WeaponMatches(entity,&damage,matches) != KEEL_RESULT_UNSUPPORTED || matches) return 18;
    std::uint32_t capabilities = UINT32_MAX;
    const KeelPlayerManagementAction management{sizeof(KeelPlayerManagementAction), KEELS2_PLAYER_MANAGEMENT_RESPAWN, 0, 0};
    if (module.PlayerManagementCapabilities(capabilities) != KEEL_RESULT_UNSUPPORTED || capabilities ||
        module.ManagePlayer(entity, management) != KEEL_RESULT_UNSUPPORTED) return 12;
    capabilities = UINT32_MAX;
    const keels2::host::GameSchemaField field{};
    const std::int32_t value = 1;
    if (module.EntityWriteCapabilities(capabilities) != KEEL_RESULT_UNSUPPORTED || capabilities ||
        module.WriteEntityField(entity,field,&value,sizeof(value)) != KEEL_RESULT_UNSUPPORTED) return 13;
    capabilities = UINT32_MAX;
    if (module.EntityToolCapabilities(capabilities) != KEEL_RESULT_UNSUPPORTED || capabilities ||
        module.ApplyEntityTool(entity,KEELS2_ENTITY_TOOL_REMOVE,nullptr,nullptr) != KEEL_RESULT_UNSUPPORTED) return 19;
    capabilities = UINT32_MAX;
    const KeelRoundTermination round{sizeof(round),8,1,0,0};
    if (module.RoundCapabilities(capabilities) != KEEL_RESULT_UNSUPPORTED || capabilities ||
        module.TerminateRound(round) != KEEL_RESULT_UNSUPPORTED) return 14;
    std::uint32_t read_mask = UINT32_MAX, write_mask = UINT32_MAX;
    std::int32_t statistic = 99;
    if (module.PlayerStatCapabilities(read_mask,write_mask) != KEEL_RESULT_UNSUPPORTED || read_mask || write_mask ||
        module.ReadPlayerStat(entity,KEELS2_PLAYER_STAT_MONEY,statistic) != KEEL_RESULT_UNSUPPORTED || statistic ||
        module.WritePlayerStat(entity,KEELS2_PLAYER_STAT_MONEY,1) != KEEL_RESULT_UNSUPPORTED) return 15;
    std::uint64_t buttons = UINT64_MAX, context = UINT64_MAX;
    if (module.ReadPlayerInput(0, 1, buttons, context) != KEEL_RESULT_UNSUPPORTED || buttons || context)
        return 11;
    if (!module.Get()->Start(nullptr, nullptr, compatibility, error) ||
        !module.Get()->CompleteStartup(error) || !module.Get()->IsGameThread())
    {
        return 3;
    }
    module.Get()->Stop();
    if (module.Get()->IsGameThread())
    {
        return 4;
    }
    module.Reset();
    if (module.Get() || !module.Path().empty() || dispatches != 0)
    {
        return 5;
    }
    auto invalid_host = host;
    ++invalid_host.abi_version;
    if (module.Load(arguments[1], "synthetic", platform, invalid_host, error) ||
        module.Get())
    {
        return 6;
    }
    if (module.Load(arguments[1], "synthetic", "wrong", host, error) ||
        module.Get())
    {
        return 7;
    }
    if (module.Load(arguments[1], "../synthetic", platform, host, error) ||
        module.Get())
    {
        return 8;
    }
    return 0;
}
