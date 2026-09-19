#include "game_adapter_loader.h"

#include <algorithm>
#include <cstring>

namespace keels2::host
{

namespace
{

bool ValidName(const char* value, std::size_t maximum) noexcept
{
    if (!value || !value[0])
    {
        return false;
    }
    std::size_t length{};
    for (; value[length] && length <= maximum; ++length)
    {
        const unsigned char character = static_cast<unsigned char>(value[length]);
        if (!((character >= 'a' && character <= 'z') ||
                (character >= '0' && character <= '9') || character == '_'))
        {
            return false;
        }
    }
    return length <= maximum;
}

template <typename Function>
Function SymbolFunction(void* address) noexcept
{
    static_assert(sizeof(Function) == sizeof(address));
    Function function{};
    std::memcpy(&function, &address, sizeof(function));
    return function;
}

}

GameAdapterModule::~GameAdapterModule()
{
    Reset();
}

bool GameAdapterModule::Load(
    const std::filesystem::path& directory,
    const char* game,
    const char* platform_name,
    const GameAdapterHostApi& host,
    std::string& error)
{
    Reset();
    if (!ValidName(game, 32) || !ValidName(platform_name, 32) ||
        host.size != sizeof(GameAdapterHostApi) ||
        host.abi_version != kGameAdapterAbiVersion ||
        !host.begin_command_dispatch || !host.end_command_dispatch)
    {
        error = "game adapter load request is invalid";
        return false;
    }
#if defined(_WIN32)
    path_ = directory / (std::string("keels2_game_") + game + ".dll");
#else
    path_ = directory / (std::string("libkeels2_game_") + game + ".so");
#endif
    if (!library_.Open(path_, error))
    {
        error = "game adapter module could not be loaded: " + error;
        path_.clear();
        return false;
    }
    const GameAdapterQueryFn query = SymbolFunction<GameAdapterQueryFn>(
        library_.Symbol(kGameAdapterQuerySymbol));
    if (!query)
    {
        error = "game adapter query export is missing";
        Reset();
        return false;
    }
    GameAdapterProvider provider{};
    provider.size = sizeof(provider);
    provider.abi_version = kGameAdapterAbiVersion;
    try
    {
        if (!query(kGameAdapterAbiVersion, &provider))
        {
            error = "game adapter rejected the provider ABI";
            Reset();
            return false;
        }
    }
    catch (...)
    {
        error = "game adapter query raised an exception";
        Reset();
        return false;
    }
    if (provider.size != sizeof(provider) ||
        provider.abi_version != kGameAdapterAbiVersion || !provider.game ||
        !provider.platform || std::strcmp(provider.game, game) != 0 ||
        std::strcmp(provider.platform, platform_name) != 0 ||
        !provider.create || !provider.destroy)
    {
        error = "game adapter provider metadata is incompatible";
        Reset();
        return false;
    }
    GameAdapter* adapter{};
    try
    {
        adapter = provider.create(&host);
    }
    catch (...)
    {
        error = "game adapter creation raised an exception";
        Reset();
        return false;
    }
    if (!adapter || !adapter->Name() || std::strcmp(adapter->Name(), game) != 0)
    {
        if (adapter)
        {
            try
            {
                provider.destroy(adapter);
            }
            catch (...)
            {
            }
        }
        error = "game adapter instance metadata is incompatible";
        Reset();
        return false;
    }
    adapter_ = adapter;
    destroy_ = provider.destroy;
    command_caller_ = SymbolFunction<GameAdapterCommandCallerFn>(
        library_.Symbol(kGameAdapterCommandCallerSymbol));
    player_action_ = SymbolFunction<GameAdapterPlayerActionFn>(
        library_.Symbol(kGameAdapterPlayerActionSymbol));
    const auto query_stats = SymbolFunction<GameAdapterQueryPlayerStatisticsFn>(library_.Symbol(kGameAdapterPlayerStatisticsSymbol));
    if (query_stats)
    {
        GameAdapterPlayerStatisticsApi stats{};
        stats.size = sizeof(stats); stats.api_version = kGameAdapterPlayerStatisticsVersion;
        if (query_stats(kGameAdapterPlayerStatisticsVersion,&stats) != KEEL_RESULT_OK || stats.size != sizeof(stats) ||
            stats.api_version != kGameAdapterPlayerStatisticsVersion || !stats.capabilities || !stats.read || !stats.write)
        {
            error = "game adapter player statistics API is incompatible";
            Reset();
            return false;
        }
        player_statistics_ = stats;
    }
    const auto query_round = SymbolFunction<GameAdapterQueryRoundControlFn>(library_.Symbol(kGameAdapterRoundControlSymbol));
    if (query_round)
    {
        GameAdapterRoundControlApi round{};
        round.size = sizeof(round); round.api_version = kGameAdapterRoundControlVersion;
        if (query_round(kGameAdapterRoundControlVersion, &round) != KEEL_RESULT_OK ||
            round.size != sizeof(round) || round.api_version != kGameAdapterRoundControlVersion || !round.capabilities || !round.terminate)
        {
            error = "game adapter round control API is incompatible";
            Reset();
            return false;
        }
        round_control_ = round;
    }
    const auto query_hook_data = SymbolFunction<GameAdapterQueryEntityHookDataFn>(library_.Symbol(kGameAdapterEntityHookDataSymbol));
    if (query_hook_data)
    {
        GameAdapterEntityHookDataApi data{};
        data.size = sizeof(data); data.api_version = kGameAdapterEntityHookDataVersion;
        if (query_hook_data(kGameAdapterEntityHookDataVersion,&data) != KEEL_RESULT_OK ||
            data.size != sizeof(data) || data.api_version != kGameAdapterEntityHookDataVersion ||
            !data.read_damage || !data.write_damage || !data.weapon_matches)
        {
            error = "game adapter entity hook data API is incompatible";
            Reset();
            return false;
        }
        entity_hook_data_ = data;
    }
    const auto query_capture = SymbolFunction<GameAdapterQueryEntityCaptureFn>(library_.Symbol(kGameAdapterEntityCaptureSymbol));
    if (query_capture)
    {
        GameAdapterEntityCaptureApi capture{};
        capture.size = sizeof(capture); capture.api_version = kGameAdapterEntityCaptureVersion;
        if (query_capture(kGameAdapterEntityCaptureVersion, &capture) != KEEL_RESULT_OK ||
            capture.size != sizeof(capture) || capture.api_version != kGameAdapterEntityCaptureVersion || !capture.capture)
        {
            error = "game adapter entity capture API is incompatible";
            Reset();
            return false;
        }
        entity_capture_ = capture;
    }
    const auto query_access = SymbolFunction<GameAdapterQueryEntityAccessFn>(library_.Symbol(kGameAdapterEntityAccessSymbol));
    if (query_access)
    {
        GameAdapterEntityAccessApi access{};
        access.size = sizeof(access); access.api_version = kGameAdapterEntityAccessVersion;
        if (query_access(kGameAdapterEntityAccessVersion, &access) != KEEL_RESULT_OK ||
            access.size != sizeof(access) || access.api_version != kGameAdapterEntityAccessVersion || !access.visit)
        {
            error = "game adapter entity access API is incompatible";
            Reset();
            return false;
        }
        entity_access_ = access;
    }
    const auto query_outputs = SymbolFunction<GameAdapterQueryEntityOutputsFn>(library_.Symbol(kGameAdapterEntityOutputsSymbol));
    if (query_outputs) {
        GameAdapterEntityOutputsApi api{}; api.size = sizeof(api);
        if (query_outputs(kGameAdapterEntityOutputsVersion,&api) != KEEL_RESULT_OK || api.size != sizeof(api) ||
            api.api_version != kGameAdapterEntityOutputsVersion || !api.start || !api.stop) {
            error = "game adapter entity output API is incompatible"; Reset(); return false;
        }
        entity_outputs_ = api;
    }
    const auto query_entity_input = SymbolFunction<GameAdapterQueryEntityInputFn>(library_.Symbol(kGameAdapterEntityInputSymbol));
    if (query_entity_input) {
        GameAdapterEntityInputApi api{}; api.size = sizeof(api);
        if (query_entity_input(kGameAdapterEntityInputVersion,&api) != KEEL_RESULT_OK || api.size != sizeof(api) ||
            api.api_version != kGameAdapterEntityInputVersion || !api.capabilities || !api.dispatch) {
            error = "game adapter entity input API is incompatible"; Reset(); return false;
        }
        entity_input_ = api;
    }
    const auto query_tools = SymbolFunction<GameAdapterQueryEntityToolsFn>(library_.Symbol(kGameAdapterEntityToolsSymbol));
    if (query_tools) {
        GameAdapterEntityToolsApi api{}; api.size = sizeof(api); api.api_version = kGameAdapterEntityToolsVersion;
        if (query_tools(kGameAdapterEntityToolsVersion,&api) != KEEL_RESULT_OK || api.size != sizeof(api) ||
            api.api_version != kGameAdapterEntityToolsVersion || !api.capabilities || !api.apply) {
            error = "game adapter entity tools API is incompatible"; Reset(); return false;
        }
        entity_tools_ = api;
    }
    const auto query_construction = SymbolFunction<GameAdapterQueryEntityConstructionFn>(library_.Symbol(kGameAdapterEntityConstructionSymbol));
    if (query_construction) {
        GameAdapterEntityConstructionApi api{}; api.size = sizeof(api);
        if (query_construction(kGameAdapterEntityConstructionVersion,&api) != KEEL_RESULT_OK ||
            api.size != sizeof(api) || api.api_version != kGameAdapterEntityConstructionVersion ||
            !api.ready || !api.create || !api.describe || !api.set || !api.teleport || !api.spawn || !api.cancel || !api.visit) {
            error = "game adapter entity construction API is incompatible"; Reset(); return false;
        }
        entity_construction_ = api;
    }
    const auto query_writes = SymbolFunction<GameAdapterQueryEntityWritesFn>(library_.Symbol(kGameAdapterEntityWritesSymbol));
    if (query_writes)
    {
        GameAdapterEntityWritesApi writes{};
        writes.size = sizeof(writes); writes.api_version = kGameAdapterEntityWritesVersion;
        if (query_writes(kGameAdapterEntityWritesVersion, &writes) != KEEL_RESULT_OK ||
            writes.size != sizeof(writes) || writes.api_version != kGameAdapterEntityWritesVersion || !writes.capabilities || !writes.write)
        {
            error = "game adapter entity writes API is incompatible";
            Reset();
            return false;
        }
        entity_writes_ = writes;
    }
    const auto query_management = SymbolFunction<GameAdapterQueryPlayerManagementFn>(
        library_.Symbol(kGameAdapterPlayerManagementSymbol));
    if (query_management)
    {
        GameAdapterPlayerManagementApi management{};
        management.size = sizeof(management);
        management.api_version = kGameAdapterPlayerManagementVersion;
        if (query_management(kGameAdapterPlayerManagementVersion, &management) != KEEL_RESULT_OK ||
            management.size != sizeof(management) || management.api_version != kGameAdapterPlayerManagementVersion ||
            !management.capabilities || !management.apply)
        {
            error = "game adapter player management API is incompatible";
            Reset();
            return false;
        }
        player_management_ = management;
    }
    const auto query_observers = SymbolFunction<GameAdapterQueryConVarObserversFn>(
        library_.Symbol(kGameAdapterConVarObserversSymbol));
    if (query_observers)
    {
        GameAdapterConVarObserversApi observers{};
        observers.size = sizeof(observers);
        observers.api_version = kGameAdapterConVarObserversVersion;
        if (query_observers(kGameAdapterConVarObserversVersion, &observers) != KEEL_RESULT_OK ||
            observers.size != sizeof(observers) || observers.api_version != kGameAdapterConVarObserversVersion ||
            !observers.observe)
        {
            error = "game adapter ConVar observer extension is incompatible";
            Reset();
            return false;
        }
        convar_observers_ = observers;
    }
    const auto query_input = SymbolFunction<GameAdapterQueryPlayerInputFn>(library_.Symbol(kGameAdapterPlayerInputSymbol));
    if (query_input)
    {
        GameAdapterPlayerInputApi input{};
        input.size = sizeof(input);
        if (query_input(kGameAdapterPlayerInputVersion, &input) != KEEL_RESULT_OK ||
            input.size != sizeof(input) || input.api_version != kGameAdapterPlayerInputVersion || !input.read)
        {
            error = "game adapter player input extension is incompatible";
            Reset();
            return false;
        }
        player_input_ = input;
    }
    const auto query_players = SymbolFunction<GameAdapterQueryPlayersFn>(
        library_.Symbol(kGameAdapterPlayersSymbol));
    if (query_players)
    {
        GameAdapterPlayersApi players{};
        players.size = sizeof(players);
        players.api_version = kGameAdapterPlayersVersion;
        if (query_players(kGameAdapterPlayersVersion, &players) != KEEL_RESULT_OK ||
            players.size != sizeof(players) || players.api_version != kGameAdapterPlayersVersion ||
            !players.capacity || !players.read)
        {
            error = "game adapter player provider is incompatible";
            Reset();
            return false;
        }
        players_ = players;
    }
    const auto query_messaging = SymbolFunction<GameAdapterQueryMessagingFn>(
        library_.Symbol(kGameAdapterMessagingSymbol));
    if (query_messaging)
    {
        GameAdapterMessagingApi messaging{};
        messaging.size = sizeof(messaging);
        if (query_messaging(kGameAdapterMessagingVersion, &messaging) != KEEL_RESULT_OK ||
            messaging.size != sizeof(messaging) || messaging.api_version != kGameAdapterMessagingVersion || !messaging.chat)
        {
            error = "game adapter messaging provider is incompatible";
            Reset();
            return false;
        }
        messaging_ = messaging;
    }
    error.clear();
    return true;
}

void GameAdapterModule::Reset() noexcept
{
    if (adapter_ && destroy_)
    {
        try
        {
            destroy_(adapter_);
        }
        catch (...)
        {
        }
    }
    adapter_ = nullptr;
    destroy_ = nullptr;
    command_caller_ = nullptr;
    player_action_ = nullptr;
    players_ = {};
    player_management_ = {};
    entity_writes_ = {};
    entity_input_ = {};
    entity_outputs_ = {};
    entity_tools_ = {};
    entity_construction_ = {};
    entity_access_ = {};
    entity_capture_ = {};
    entity_hook_data_ = {};
    round_control_ = {};
    player_statistics_ = {};
    player_input_ = {};
    messaging_ = {};
    convar_observers_ = {};
    library_.Close();
    path_.clear();
}

GameAdapter* GameAdapterModule::Get() const noexcept
{
    return adapter_;
}

bool GameAdapterModule::SupportsClientCommands() const noexcept
{
    return command_caller_ != nullptr;
}

KeelResult GameAdapterModule::CommandCaller(const void* context, std::int32_t& slot) const noexcept
{
    slot = -1;
    return command_caller_ ? command_caller_(context, &slot) : KEEL_RESULT_OK;
}

const std::filesystem::path& GameAdapterModule::Path() const noexcept
{
    return path_;
}

KeelResult GameAdapterModule::PrintChat(std::int32_t slot, KeelBool broadcast, const char* text) const noexcept
{
    return messaging_.chat ? messaging_.chat(adapter_, slot, broadcast, text) : KEEL_RESULT_UNSUPPORTED;
}

KeelResult GameAdapterModule::ObserveConVar(GameConVarHandle convar,
    GameConVarCallback callback, void* user_data) const noexcept
{
    return convar_observers_.observe
        ? convar_observers_.observe(adapter_, convar, callback, user_data)
        : KEEL_RESULT_UNSUPPORTED;
}

KeelResult GameAdapterModule::ReadPlayerInput(std::int32_t slot, std::uint32_t controller,
    std::uint64_t& buttons, std::uint64_t& context) const noexcept
{
    buttons = context = 0;
    return player_input_.read ? player_input_.read(adapter_, slot, controller, &buttons, &context) : KEEL_RESULT_UNSUPPORTED;
}

std::uint32_t GameAdapterModule::PlayerCapacity() const noexcept
{
    return players_.capacity ? players_.capacity() : 0;
}

KeelResult GameAdapterModule::ReadPlayer(std::int32_t slot, KeelPlayerInfo& player) const noexcept
{
    player = {};
    player.size = sizeof(player);
    player.slot = -1;
    player.user_id = -1;
    player.controller_handle = UINT32_MAX;
    player.pawn_handle = UINT32_MAX;
    return players_.read ? players_.read(adapter_, slot, &player) : KEEL_RESULT_UNSUPPORTED;
}

KeelResult GameAdapterModule::ReadDamage(const void* record, KeelDamageInfo& output) const noexcept
{
    return adapter_ && entity_hook_data_.read_damage ? entity_hook_data_.read_damage(adapter_,record,&output) : KEEL_RESULT_UNSUPPORTED;
}
KeelResult GameAdapterModule::WriteDamage(void* record, const KeelDamageEdit& edit) const noexcept
{
    return adapter_ && entity_hook_data_.write_damage ? entity_hook_data_.write_damage(adapter_,record,&edit) : KEEL_RESULT_UNSUPPORTED;
}
KeelResult GameAdapterModule::WeaponMatches(const GameEntityIdentity& pawn, const void* candidate, KeelBool& matches) const noexcept
{
    matches = KEEL_FALSE;
    return adapter_ && entity_hook_data_.weapon_matches ? entity_hook_data_.weapon_matches(adapter_,&pawn,candidate,&matches) : KEEL_RESULT_UNSUPPORTED;
}

KeelResult GameAdapterModule::CaptureEntity(const void* instance, GameEntityIdentity& entity) const noexcept
{
    entity = {};
    return adapter_ && entity_capture_.capture
        ? entity_capture_.capture(adapter_, instance, &entity) : KEEL_RESULT_UNSUPPORTED;
}

KeelResult GameAdapterModule::VisitEntities(const GameEntityAccessRequest* entities, std::uint32_t count,
    KeelEntityAccessCallback callback, void* user_data) const noexcept
{
    return adapter_ && entity_access_.visit
        ? entity_access_.visit(adapter_, entities, count, callback, user_data) : KEEL_RESULT_UNSUPPORTED;
}

KeelResult GameAdapterModule::EntityToolCapabilities(std::uint32_t& flags) const noexcept
{
    flags = 0;
    return adapter_ && entity_tools_.capabilities ? entity_tools_.capabilities(adapter_,&flags) : KEEL_RESULT_UNSUPPORTED;
}
KeelResult GameAdapterModule::ApplyEntityTool(const GameEntityIdentity& entity, std::uint32_t kind,
    const KeelEntityTeleport* request, const char* model) const noexcept
{
    return adapter_ && entity_tools_.apply ? entity_tools_.apply(adapter_,&entity,kind,request,model) : KEEL_RESULT_UNSUPPORTED;
}
KeelResult GameAdapterModule::EntityWriteCapabilities(std::uint32_t& capabilities) const noexcept
{
    capabilities = 0;
    return adapter_ && entity_writes_.capabilities
        ? entity_writes_.capabilities(adapter_, &capabilities) : KEEL_RESULT_UNSUPPORTED;
}

KeelResult GameAdapterModule::WriteEntityField(const GameEntityIdentity& entity, const GameSchemaField& field,
    const void* value, std::uint32_t size) const noexcept
{
    return adapter_ && entity_writes_.write ? entity_writes_.write(adapter_, &entity, &field, value, size) : KEEL_RESULT_UNSUPPORTED;
}

KeelResult GameAdapterModule::PlayerManagementCapabilities(std::uint32_t& capabilities) const noexcept
{
    capabilities = 0;
    return adapter_ && player_management_.capabilities
        ? player_management_.capabilities(adapter_, &capabilities) : KEEL_RESULT_UNSUPPORTED;
}

KeelResult GameAdapterModule::RoundCapabilities(std::uint32_t& capabilities) const noexcept
{
    capabilities = 0;
    return adapter_ && round_control_.capabilities
        ? round_control_.capabilities(adapter_, &capabilities) : KEEL_RESULT_UNSUPPORTED;
}

KeelResult GameAdapterModule::PlayerStatCapabilities(std::uint32_t& readable, std::uint32_t& writable) const noexcept
{
    readable = writable = 0;
    return adapter_ && player_statistics_.capabilities
        ? player_statistics_.capabilities(adapter_,&readable,&writable) : KEEL_RESULT_UNSUPPORTED;
}
KeelResult GameAdapterModule::ReadPlayerStat(const GameEntityIdentity& controller, std::uint32_t key, std::int32_t& value) const noexcept
{
    value = 0;
    return adapter_ && player_statistics_.read
        ? player_statistics_.read(adapter_,&controller,key,&value) : KEEL_RESULT_UNSUPPORTED;
}
KeelResult GameAdapterModule::WritePlayerStat(const GameEntityIdentity& controller, std::uint32_t key, std::int32_t value) const noexcept
{
    return adapter_ && player_statistics_.write
        ? player_statistics_.write(adapter_,&controller,key,value) : KEEL_RESULT_UNSUPPORTED;
}

KeelResult GameAdapterModule::TerminateRound(const KeelRoundTermination& request) const noexcept
{
    return adapter_ && round_control_.terminate
        ? round_control_.terminate(adapter_, &request) : KEEL_RESULT_UNSUPPORTED;
}

KeelResult GameAdapterModule::ManagePlayer(const GameEntityIdentity& entity, const KeelPlayerManagementAction& action) const noexcept
{
    return adapter_ && player_management_.apply
        ? player_management_.apply(adapter_, &entity, &action) : KEEL_RESULT_UNSUPPORTED;
}

KeelResult GameAdapterModule::PlayerAction(const GameEntityIdentity& entity, const KeelPlayerAction& action) const noexcept
{
    return adapter_ && player_action_ ? player_action_(adapter_, &entity, &action) : KEEL_RESULT_UNSUPPORTED;
}

}
