#ifndef KEELS2_HOST_GAME_ADAPTER_LOADER_H
#define KEELS2_HOST_GAME_ADAPTER_LOADER_H

#include "game_adapter.h"

#include <keels2/platform/dynamic_library.h>

#include <filesystem>
#include <string>

namespace keels2::host
{

class GameAdapterModule final
{
public:
    GameAdapterModule() = default;
    GameAdapterModule(const GameAdapterModule&) = delete;
    GameAdapterModule& operator=(const GameAdapterModule&) = delete;
    ~GameAdapterModule();

    bool Load(
        const std::filesystem::path& directory,
        const char* game,
        const char* platform,
        const GameAdapterHostApi& host,
        std::string& error);
    void Reset() noexcept;
    GameAdapter* Get() const noexcept;
    bool SupportsClientCommands() const noexcept;
    KeelResult CommandCaller(const void* context, std::int32_t& slot) const noexcept;
    KeelResult ReadDamage(const void* record, KeelDamageInfo& output) const noexcept;
    KeelResult WriteDamage(void* record, const KeelDamageEdit& edit) const noexcept;
    KeelResult WeaponMatches(const GameEntityIdentity& pawn, const void* candidate, KeelBool& matches) const noexcept;
    KeelResult CaptureEntity(const void* instance, GameEntityIdentity& entity) const noexcept;
    KeelResult VisitEntities(const GameEntityAccessRequest* entities, std::uint32_t count,
        KeelEntityAccessCallback callback, void* user_data) const noexcept;
    KeelResult EntityToolCapabilities(std::uint32_t& flags) const noexcept;
    KeelResult ApplyEntityTool(const GameEntityIdentity& entity, std::uint32_t kind,
        const KeelEntityTeleport* request, const char* model) const noexcept;
    KeelResult EntityWriteCapabilities(std::uint32_t& capabilities) const noexcept;
    KeelResult WriteEntityField(const GameEntityIdentity& entity, const GameSchemaField& field, const void* value, std::uint32_t size) const noexcept;
    KeelResult PlayerManagementCapabilities(std::uint32_t& capabilities) const noexcept;
    KeelResult RoundCapabilities(std::uint32_t& capabilities) const noexcept;
    KeelResult PlayerStatCapabilities(std::uint32_t& readable, std::uint32_t& writable) const noexcept;
    KeelResult ReadPlayerStat(const GameEntityIdentity& controller, std::uint32_t key, std::int32_t& value) const noexcept;
    KeelResult WritePlayerStat(const GameEntityIdentity& controller, std::uint32_t key, std::int32_t value) const noexcept;
    KeelResult TerminateRound(const KeelRoundTermination& request) const noexcept;
    KeelResult ManagePlayer(const GameEntityIdentity& entity, const KeelPlayerManagementAction& action) const noexcept;
    KeelResult PlayerAction(const GameEntityIdentity& entity, const KeelPlayerAction& action) const noexcept;
    KeelResult PrintChat(std::int32_t slot, KeelBool broadcast, const char* text) const noexcept;
    KeelResult ObserveConVar(GameConVarHandle convar, GameConVarCallback callback, void* user_data) const noexcept;
    std::uint32_t PlayerCapacity() const noexcept;
    KeelResult ReadPlayer(std::int32_t slot, KeelPlayerInfo& player) const noexcept;
    KeelResult ReadPlayerInput(std::int32_t slot, std::uint32_t controller, std::uint64_t& buttons, std::uint64_t& context) const noexcept;
    const std::filesystem::path& Path() const noexcept;

private:
    platform::DynamicLibrary library_;
    GameAdapterDestroyFn destroy_{};
    GameAdapterCommandCallerFn command_caller_{};
    GameAdapterPlayerActionFn player_action_{};
    GameAdapterPlayersApi players_{};
    GameAdapterPlayerManagementApi player_management_{};
    GameAdapterEntityWritesApi entity_writes_{};
    GameAdapterEntityToolsApi entity_tools_{};
    GameAdapterEntityAccessApi entity_access_{};
    GameAdapterEntityCaptureApi entity_capture_{};
    GameAdapterEntityHookDataApi entity_hook_data_{};
    GameAdapterRoundControlApi round_control_{};
    GameAdapterPlayerStatisticsApi player_statistics_{};
    GameAdapterPlayerInputApi player_input_{};
    GameAdapterMessagingApi messaging_{};
    GameAdapterConVarObserversApi convar_observers_{};
    GameAdapter* adapter_{};
    std::filesystem::path path_;
};

}

#endif
