#include "schema_entity_service.h"

#include "host.h"
#include "game_adapter_loader.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace keels2::host
{

std::atomic<SchemaEntityService*> SchemaEntityService::active_{};

SchemaEntityService::SchemaEntityService(Host& host, GameAdapter& adapter)
    : host_(host), adapter_(adapter)
{
    SchemaEntityService* expected{};

    if (!active_.compare_exchange_strong(expected, this, std::memory_order_acq_rel))
    {
        throw std::runtime_error("schema and entity service already exists");
    }

    schema_api_ = {
        sizeof(KeelSchemaApi),
        KEELS2_SCHEMA_API_VERSION,
        &ResolveFieldEntry,
        &ReleaseFieldEntry,
        &DescribeFieldEntry
    };
    player_actions_api_ = {sizeof(KeelPlayerActionsApi), KEELS2_PLAYER_ACTIONS_API_VERSION, &PlayerActionEntry};
    player_management_api_ = {sizeof(KeelPlayerManagementApi), KEELS2_PLAYER_MANAGEMENT_API_VERSION,
        &ManagementCapabilitiesEntry, &ManagePlayerEntry};

    entity_hook_data_api_ = {sizeof(KeelEntityHookDataApi),
                             KEELS2_ENTITY_HOOK_DATA_API_VERSION,
                             &ReadDamageEntry,
                             &WriteDamageEntry,
                             &WeaponMatchesEntry};

    entity_capture_api_ = {sizeof(KeelEntityCaptureApi), KEELS2_ENTITY_CAPTURE_API_VERSION, &CaptureEntityEntry};
    entity_access_api_ = {sizeof(KeelEntityAccessApi), KEELS2_ENTITY_ACCESS_API_VERSION, &VisitEntitiesEntry};
    entity_outputs_api_ = {sizeof(KeelEntityOutputsApi),
                           KEELS2_ENTITY_OUTPUTS_API_VERSION,
                           &OutputsReadyEntry,
                           &SubscribeOutputEntry,
                           &UnsubscribeOutputEntry};

    entity_input_api_ = {
        sizeof(KeelEntityInputApi), KEELS2_ENTITY_INPUT_API_VERSION, &InputCapabilitiesEntry, &DispatchInputEntry};

    entity_tools_api_ = {sizeof(KeelEntityToolsApi),
                         KEELS2_ENTITY_TOOLS_API_VERSION,
                         &ToolCapabilitiesEntry,
                         &TeleportEntry,
                         &SetModelEntry,
                         &RemoveEntry};

    entity_construction_api_ = {sizeof(KeelEntityConstructionApi),KEELS2_ENTITY_CONSTRUCTION_API_VERSION,
        &ConstructionReadyEntry,&CreateEntityEntry,&DescribeConstructionEntry,&SetConstructionEntry,
        &TeleportConstructionEntry,&SpawnConstructionEntry,&ObserveConstructionEntry,&VisitConstructionEntry};

    entity_writes_api_ = {
        sizeof(KeelEntityWritesApi), KEELS2_ENTITY_WRITES_API_VERSION, &WriteCapabilitiesEntry, &WriteFieldEntry};

    round_control_api_ = {
        sizeof(KeelRoundControlApi), KEELS2_ROUND_CONTROL_API_VERSION, &RoundCapabilitiesEntry, &TerminateRoundEntry};

    player_statistics_api_ = {sizeof(KeelPlayerStatisticsApi), KEELS2_PLAYER_STATISTICS_API_VERSION,
        &PlayerStatCapabilitiesEntry, &ReadPlayerStatEntry, &WritePlayerStatEntry};

    entities_api_ = {
        sizeof(KeelEntitiesApi),
        KEELS2_ENTITIES_API_VERSION,
        &FindEntityByIndexEntry,
        &FindEntityBySource2HandleEntry,
        &ReleaseEntityEntry,
        &DescribeEntityEntry,
        &EqualEntityEntry,
        &ReadEntityFieldEntry
    };
}

SchemaEntityService::~SchemaEntityService()
{
    active_.store(nullptr, std::memory_order_release);
    static_cast<void>(Shutdown());
}

const KeelSchemaApi& SchemaEntityService::SchemaApi() const noexcept
{
    return schema_api_;
}

const KeelEntitiesApi& SchemaEntityService::EntitiesApi() const noexcept
{
    return entities_api_;
}

const KeelEntityHookDataApi& SchemaEntityService::EntityHookDataApi() const noexcept
{
    return entity_hook_data_api_;
}

KeelResult SchemaEntityService::ReadDamageEntry(KeelPluginHandle plugin, const void* record, KeelDamageInfo* output)
{
    const bool sized = output && output->size == sizeof(*output);

    if (output)
    {
        *output = {};
        output->size = sizeof(*output);
        output->inflictor = output->attacker = output->ability = UINT32_MAX;
    }

    if (!record || !sized)
        return KEEL_RESULT_INVALID_ARGUMENT;

    try
    {
        auto* service = active_.load(std::memory_order_acquire);

        if (!service)
            return KEEL_RESULT_NOT_READY;

        KeelDamageInfo value{};
        value.size = sizeof(value);
        const auto status = service->AccessDamage(plugin,record,&value,nullptr);

        if (status != KEEL_RESULT_OK)
            return status;

        if (value.size != sizeof(value) || value.reserved || !std::isfinite(value.damage))
            return KEEL_RESULT_INCOMPATIBLE;

        for (unsigned i = 0; i < 3; ++i)
            if (!std::isfinite(value.force[i]) || !std::isfinite(value.position[i]))
                return KEEL_RESULT_INCOMPATIBLE;

        *output = value;
        return KEEL_RESULT_OK;
    }
    catch (...)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult SchemaEntityService::WriteDamageEntry(KeelPluginHandle plugin, void* record, const KeelDamageEdit* input)
{
    if (!record || !input || input->size != sizeof(*input))
        return KEEL_RESULT_INVALID_ARGUMENT;

    const auto edit = *input;

    if (edit.reserved || !std::isfinite(edit.damage) || edit.damage < 0)
        return KEEL_RESULT_INVALID_ARGUMENT;

    for (unsigned i = 0; i < 3; ++i)
        if (!std::isfinite(edit.force[i]) || !std::isfinite(edit.position[i]))
            return KEEL_RESULT_INVALID_ARGUMENT;

    try
    {
        auto* service = active_.load(std::memory_order_acquire);
        return service ? service->AccessDamage(plugin,record,nullptr,&edit) : KEEL_RESULT_NOT_READY;
    }
    catch (...)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult SchemaEntityService::AccessDamage(KeelPluginHandle plugin,
                                             const void* record,
                                             KeelDamageInfo* output,
                                             const KeelDamageEdit* edit)
{
    std::scoped_lock state_lock(host_.state_mutex_);

    if (!PluginReady(plugin))
        return KEEL_RESULT_NOT_READY;

    if (!adapter_.IsGameThread())
        return KEEL_RESULT_WRONG_THREAD;

    auto* owner = host_.PluginByHandle(plugin);

    if (!owner || owner->cleanup_pending || owner->transitioning || owner->active_native_operations == UINT32_MAX ||
        hook_data_depth_ >= 8) return KEEL_RESULT_BUSY;

    ++owner->active_native_operations;
    ++hook_data_depth_;

    struct Hold
    {
        std::uint32_t& active;
        unsigned& depth;

        ~Hold()
        {
            --active;
            --depth;
        }
    } hold{owner->active_native_operations, hook_data_depth_};

    if (!host_.adapter_module_)
        return KEEL_RESULT_UNSUPPORTED;

    return edit ? host_.adapter_module_->WriteDamage(const_cast<void*>(record), *edit)
                : host_.adapter_module_->ReadDamage(record, *output);
}

KeelResult SchemaEntityService::WeaponMatchesEntry(KeelPluginHandle plugin,
                                                   KeelEntityHandle pawn,
                                                   const void* candidate,
                                                   KeelBool* matches)
{
    if (matches)
        *matches = KEEL_FALSE;

    if (!pawn || !candidate || !matches)
        return KEEL_RESULT_INVALID_ARGUMENT;

    try
    {
        auto* service = active_.load(std::memory_order_acquire);
        return service ? service->WeaponMatches(plugin,pawn,candidate,matches) : KEEL_RESULT_NOT_READY;
    }
    catch (...)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult SchemaEntityService::WeaponMatches(KeelPluginHandle plugin,
                                              KeelEntityHandle pawn,
                                              const void* candidate,
                                              KeelBool* matches)
{
    std::scoped_lock state_lock(host_.state_mutex_);

    if (!PluginReady(plugin))
        return KEEL_RESULT_NOT_READY;

    if (!adapter_.IsGameThread())
        return KEEL_RESULT_WRONG_THREAD;

    auto* owner = host_.PluginByHandle(plugin);

    if (!owner || owner->cleanup_pending || owner->transitioning || owner->active_native_operations == UINT32_MAX ||
        hook_data_depth_ >= 8) return KEEL_RESULT_BUSY;

    GameEntityIdentity identity{};
    {
        std::scoped_lock registry_lock(registry_mutex_);
        const auto found = entities_.find(pawn);

        if (found == entities_.end() || !EntityAccessible(found->second, plugin))
            return KEEL_RESULT_NOT_FOUND;

        identity = found->second.entity;
    }

    ++owner->active_native_operations;
    ++hook_data_depth_;

    struct Hold
    {
        std::uint32_t& active;
        unsigned& depth;

        ~Hold()
        {
            --active;
            --depth;
        }
    } hold{owner->active_native_operations, hook_data_depth_};

    if (!host_.adapter_module_)
        return KEEL_RESULT_UNSUPPORTED;

    KeelBool value = KEEL_FALSE;
    const auto status = host_.adapter_module_->WeaponMatches(identity,candidate,value);

    if (status != KEEL_RESULT_OK)
        return status;

    if (value != KEEL_FALSE && value != KEEL_TRUE)
        return KEEL_RESULT_INCOMPATIBLE;

    *matches = value;
    return KEEL_RESULT_OK;
}

const KeelEntityCaptureApi& SchemaEntityService::EntityCaptureApi() const noexcept
{
    return entity_capture_api_;
}

KeelResult
SchemaEntityService::CaptureEntityEntry(KeelPluginHandle plugin, const void* instance, KeelEntityHandle* output)
{
    if (output)
        *output = 0;

    try
    {
        auto* service = active_.load(std::memory_order_acquire);
        return service ? service->CaptureEntity(plugin, instance, output) : KEEL_RESULT_NOT_READY;
    }
    catch (...)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult SchemaEntityService::CaptureEntity(KeelPluginHandle plugin, const void* instance, KeelEntityHandle* output)
{
    if (!instance || !output)
        return KEEL_RESULT_INVALID_ARGUMENT;

    std::scoped_lock state_lock(host_.state_mutex_);

    if (!PluginReady(plugin))
        return KEEL_RESULT_NOT_READY;

    if (!adapter_.IsGameThread())
        return KEEL_RESULT_WRONG_THREAD;

    GameEntityIdentity identity{};
    const auto status = host_.adapter_module_
        ? host_.adapter_module_->CaptureEntity(instance, identity) : KEEL_RESULT_UNSUPPORTED;

    if (status != KEEL_RESULT_OK)
        return status;

    if (identity.index < 0 || !identity.epoch || identity.source2_handle == KEELS2_INVALID_SOURCE2_ENTITY_HANDLE)
        return KEEL_RESULT_INCOMPATIBLE;

    std::string error;
    const auto valid = adapter_.ValidateEntity(identity, error);

    if (valid != KEEL_RESULT_OK)
        return valid;

    std::scoped_lock lock(registry_mutex_);

    if (shutting_down_.load(std::memory_order_acquire) || !next_entity_)
        return KEEL_RESULT_ENGINE_FAILURE;

    const auto handle = next_entity_++;
    entities_.emplace(handle, EntityRecord{plugin, identity});
    *output = handle;
    return KEEL_RESULT_OK;
}

const KeelEntityAccessApi& SchemaEntityService::EntityAccessApi() const noexcept
{
    return entity_access_api_;
}

KeelResult SchemaEntityService::VisitEntitiesEntry(KeelPluginHandle plugin, const KeelEntityAccessSpec* entities,
    std::uint32_t count, KeelEntityAccessCallback callback, void* user_data)
{
    try
    {
        auto* service = active_.load(std::memory_order_acquire);
        return service ? service->VisitEntities(plugin, entities, count, callback, user_data) : KEEL_RESULT_NOT_READY;
    }
    catch (...)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult SchemaEntityService::VisitEntities(KeelPluginHandle plugin, const KeelEntityAccessSpec* entities,
    std::uint32_t count, KeelEntityAccessCallback callback, void* user_data)
{
    if (!entities || !count || count > KEELS2_ENTITY_ACCESS_MAX_COUNT || !callback)
        return KEEL_RESULT_INVALID_ARGUMENT;

    std::scoped_lock state_lock(host_.state_mutex_);

    if (!PluginReady(plugin))
        return KEEL_RESULT_NOT_READY;

    if (!adapter_.IsGameThread())
        return KEEL_RESULT_WRONG_THREAD;

    auto* owner = host_.PluginByHandle(plugin);

    if (!owner || owner->cleanup_pending || owner->transitioning || owner->active_native_operations == UINT32_MAX ||
        entity_access_depth_ == KEELS2_ENTITY_ACCESS_MAX_DEPTH) return KEEL_RESULT_BUSY;

    std::array<std::string, KEELS2_ENTITY_ACCESS_MAX_COUNT> names;
    std::array<GameEntityAccessRequest, KEELS2_ENTITY_ACCESS_MAX_COUNT> requests{};
    {
        std::scoped_lock registry_lock(registry_mutex_);

        for (std::uint32_t i = 0; i < count; ++i)
        {
            const auto& spec = entities[i];

            if (spec.size != sizeof(spec) || spec.reserved || !spec.entity || !ValidSchemaName(spec.class_name))
                return KEEL_RESULT_INVALID_ARGUMENT;

            const auto found = entities_.find(spec.entity);

            if (found == entities_.end() || !EntityAccessible(found->second, plugin))
                return KEEL_RESULT_NOT_FOUND;

            names[i] = spec.class_name;
            requests[i] = {found->second.entity, names[i].c_str()};
        }
    }

    ++owner->active_native_operations;
    ++entity_access_depth_;

    struct Hold
    {
        std::uint32_t& active;
        unsigned& depth;

        ~Hold()
        {
            --active;
            --depth;
        }
    } hold{owner->active_native_operations, entity_access_depth_};
    return host_.adapter_module_
        ? host_.adapter_module_->VisitEntities(requests.data(), count, callback, user_data) : KEEL_RESULT_UNSUPPORTED;
}

const KeelPlayerActionsApi& SchemaEntityService::PlayerActionsApi() const noexcept
{
    return player_actions_api_;
}

KeelResult
SchemaEntityService::PlayerActionEntry(KeelPluginHandle plugin, KeelEntityHandle entity, const KeelPlayerAction* action)
{
    try
    {
        auto* service = active_.load(std::memory_order_acquire);
        return service ? service->PlayerAction(plugin, entity, action) : KEEL_RESULT_NOT_READY;
    }
    catch (...)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult
SchemaEntityService::PlayerAction(KeelPluginHandle plugin, KeelEntityHandle entity, const KeelPlayerAction* action)
{
    if (!entity || !action || action->size != sizeof(KeelPlayerAction) ||
        (action->kind != KEELS2_PLAYER_ACTION_IMPULSE && action->kind != KEELS2_PLAYER_ACTION_KILL) ||
        !std::isfinite(action->damage) || action->damage < 0 || action->damage > 100000)
        return KEEL_RESULT_INVALID_ARGUMENT;

    for (const auto component : action->impulse)
        if (!std::isfinite(component) || std::abs(component) > 4096 ||
            (action->kind == KEELS2_PLAYER_ACTION_KILL && component != 0))
            return KEEL_RESULT_INVALID_ARGUMENT;

    if (action->kind == KEELS2_PLAYER_ACTION_KILL && action->damage != 0)
        return KEEL_RESULT_INVALID_ARGUMENT;

    std::scoped_lock state_lock(host_.state_mutex_);

    if (!PluginReady(plugin))
        return KEEL_RESULT_NOT_READY;

    if (!adapter_.IsGameThread())
        return KEEL_RESULT_WRONG_THREAD;

    GameEntityIdentity identity;
    {
        std::scoped_lock lock(registry_mutex_);
        const auto record = entities_.find(entity);

        if (record == entities_.end() || !EntityAccessible(record->second, plugin))
            return KEEL_RESULT_NOT_FOUND;

        identity = record->second.entity;
    }

    std::string error;
    const auto valid = adapter_.ValidateEntity(identity, error);

    if (valid != KEEL_RESULT_OK)
        return valid;

    auto* owner = host_.PluginByHandle(plugin);

    if (!owner || owner->active_native_operations == UINT32_MAX)
        return KEEL_RESULT_BUSY;

    ++owner->active_native_operations;

    struct ActionHold
    {
        std::uint32_t& count;

        ~ActionHold()
        {
            --count;
        }
    } hold{owner->active_native_operations};
    return host_.adapter_module_ ? host_.adapter_module_->PlayerAction(identity, *action) : KEEL_RESULT_UNSUPPORTED;
}

const KeelPlayerManagementApi& SchemaEntityService::PlayerManagementApi() const noexcept
{
    return player_management_api_;
}

KeelResult SchemaEntityService::ManagementCapabilitiesEntry(KeelPluginHandle plugin, std::uint32_t* capabilities)
{
    if (capabilities)
        *capabilities = 0;

    try
    {
        auto* service = active_.load(std::memory_order_acquire);
        return service ? service->ManagementCapabilities(plugin, capabilities) : KEEL_RESULT_NOT_READY;
    }
    catch (...)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult SchemaEntityService::ManagementCapabilities(KeelPluginHandle plugin, std::uint32_t* capabilities)
{
    if (!capabilities)
        return KEEL_RESULT_INVALID_ARGUMENT;

    std::scoped_lock state_lock(host_.state_mutex_);

    if (!PluginReady(plugin))
        return KEEL_RESULT_NOT_READY;

    if (!adapter_.IsGameThread())
        return KEEL_RESULT_WRONG_THREAD;

    std::uint32_t supported{};
    const auto result = host_.adapter_module_
        ? host_.adapter_module_->PlayerManagementCapabilities(supported) : KEEL_RESULT_UNSUPPORTED;

    if (result == KEEL_RESULT_OK)
        *capabilities = supported;

    return result;
}

KeelResult SchemaEntityService::ManagePlayerEntry(KeelPluginHandle plugin, KeelEntityHandle entity,
    const KeelPlayerManagementAction* action)
{
    try
    {
        auto* service = active_.load(std::memory_order_acquire);
        return service ? service->ManagePlayer(plugin, entity, action) : KEEL_RESULT_NOT_READY;
    }
    catch (...)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult SchemaEntityService::ManagePlayer(KeelPluginHandle plugin, KeelEntityHandle entity,
    const KeelPlayerManagementAction* action)
{
    if (!entity || !action || action->size != sizeof(*action))
        return KEEL_RESULT_INVALID_ARGUMENT;

    const auto request = *action;

    if (request.reserved || (request.kind != KEELS2_PLAYER_MANAGEMENT_RESPAWN &&
        request.kind != KEELS2_PLAYER_MANAGEMENT_CHANGE_TEAM && request.kind != KEELS2_PLAYER_MANAGEMENT_SWITCH_TEAM) ||
        (request.kind == KEELS2_PLAYER_MANAGEMENT_RESPAWN ? request.team != 0 : request.team < 0 || request.team > 255))
        return KEEL_RESULT_INVALID_ARGUMENT;

    std::scoped_lock state_lock(host_.state_mutex_);

    if (!PluginReady(plugin))
        return KEEL_RESULT_NOT_READY;

    if (!adapter_.IsGameThread())
        return KEEL_RESULT_WRONG_THREAD;

    GameEntityIdentity identity;
    {
        std::scoped_lock lock(registry_mutex_);
        const auto record = entities_.find(entity);

        if (record == entities_.end() || !EntityAccessible(record->second, plugin))
            return KEEL_RESULT_NOT_FOUND;

        identity = record->second.entity;
    }

    std::string error;
    const auto valid = adapter_.ValidateEntity(identity, error);

    if (valid != KEEL_RESULT_OK)
        return valid;

    auto* owner = host_.PluginByHandle(plugin);

    if (!owner || owner->active_native_operations == UINT32_MAX)
        return KEEL_RESULT_BUSY;

    ++owner->active_native_operations;

    struct ActionHold
    {
        std::uint32_t& count;

        ~ActionHold()
        {
            --count;
        }
    } hold{owner->active_native_operations};
    return host_.adapter_module_ ? host_.adapter_module_->ManagePlayer(identity, request) : KEEL_RESULT_UNSUPPORTED;
}

const KeelEntityWritesApi& SchemaEntityService::EntityWritesApi() const noexcept
{
    return entity_writes_api_;
}

const KeelRoundControlApi& SchemaEntityService::RoundControlApi() const noexcept
{
    return round_control_api_;
}

const KeelPlayerStatisticsApi& SchemaEntityService::PlayerStatisticsApi() const noexcept
{
    return player_statistics_api_;
}

KeelResult SchemaEntityService::PlayerStatCapabilitiesEntry(KeelPluginHandle plugin,
                                                            std::uint32_t* readable,
                                                            std::uint32_t* writable)
{
    if (readable)
        *readable = 0;

    if (writable)
        *writable = 0;

    try
    {
        auto* service = active_.load(std::memory_order_acquire);
        return service ? service->PlayerStatCapabilities(plugin,readable,writable) : KEEL_RESULT_NOT_READY;
    }
    catch (...)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult
SchemaEntityService::PlayerStatCapabilities(KeelPluginHandle plugin, std::uint32_t* readable, std::uint32_t* writable)
{
    if (!readable || !writable)
        return KEEL_RESULT_INVALID_ARGUMENT;

    std::scoped_lock state_lock(host_.state_mutex_);

    if (!PluginReady(plugin))
        return KEEL_RESULT_NOT_READY;

    if (!adapter_.IsGameThread())
        return KEEL_RESULT_WRONG_THREAD;

    std::uint32_t reads{},writes{};
    const auto result =
        host_.adapter_module_ ? host_.adapter_module_->PlayerStatCapabilities(reads, writes) : KEEL_RESULT_UNSUPPORTED;

    if (result == KEEL_RESULT_OK)
    {
        *readable = reads;
        *writable = writes;
    }

    return result;
}

KeelResult SchemaEntityService::ReadPlayerStatEntry(KeelPluginHandle plugin,
                                                    KeelEntityHandle entity,
                                                    std::uint32_t key,
                                                    std::int32_t* output)
{
    if (!output)
        return KEEL_RESULT_INVALID_ARGUMENT;

    *output = 0;

    try
    {
        auto* service = active_.load(std::memory_order_acquire);

        if (!service)
            return KEEL_RESULT_NOT_READY;

        std::int32_t value{};
        const auto result = service->AccessPlayerStat(plugin,entity,key,value,false);

        if (result == KEEL_RESULT_OK)
            *output = value;

        return result;
    }
    catch (...)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult SchemaEntityService::WritePlayerStatEntry(KeelPluginHandle plugin,
                                                     KeelEntityHandle entity,
                                                     std::uint32_t key,
                                                     std::int32_t value)
{
    try
    {
        auto* service = active_.load(std::memory_order_acquire);
        return service ? service->AccessPlayerStat(plugin,entity,key,value,true) : KEEL_RESULT_NOT_READY;
    }
    catch (...)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult SchemaEntityService::AccessPlayerStat(
    KeelPluginHandle plugin, KeelEntityHandle entity, std::uint32_t key, std::int32_t& value, bool write)
{
    if (!entity || (key != KEELS2_PLAYER_STAT_MONEY && key != KEELS2_PLAYER_STAT_MATCH_KILLS &&
        key != KEELS2_PLAYER_STAT_MATCH_DEATHS && key != KEELS2_PLAYER_STAT_MATCH_ASSISTS) || (write && value < 0))
        return KEEL_RESULT_INVALID_ARGUMENT;

    std::scoped_lock state_lock(host_.state_mutex_);

    if (!PluginReady(plugin))
        return KEEL_RESULT_NOT_READY;

    if (!adapter_.IsGameThread())
        return KEEL_RESULT_WRONG_THREAD;

    GameEntityIdentity identity;
    {
        std::scoped_lock lock(registry_mutex_);
        const auto found = entities_.find(entity);

        if (found == entities_.end() || !EntityAccessible(found->second, plugin))
            return KEEL_RESULT_NOT_FOUND;

        identity = found->second.entity;
    }

    std::string error;
    const auto valid = adapter_.ValidateEntity(identity,error);

    if (valid != KEEL_RESULT_OK)
        return valid;

    auto* owner = host_.PluginByHandle(plugin);

    if (!owner || owner->active_native_operations == UINT32_MAX)
        return KEEL_RESULT_BUSY;

    ++owner->active_native_operations;

    struct Hold
    {
        std::uint32_t& count;

        ~Hold()
        {
            --count;
        }
    } hold{owner->active_native_operations};

    if (!host_.adapter_module_)
        return KEEL_RESULT_UNSUPPORTED;

    return write ? host_.adapter_module_->WritePlayerStat(identity, key, value)
                 : host_.adapter_module_->ReadPlayerStat(identity, key, value);
}

KeelResult SchemaEntityService::RoundCapabilitiesEntry(KeelPluginHandle plugin, std::uint32_t* capabilities)
{
    if (capabilities)
        *capabilities = 0;

    try
    {
        auto* service = active_.load(std::memory_order_acquire);
        return service ? service->RoundCapabilities(plugin, capabilities) : KEEL_RESULT_NOT_READY;
    }
    catch (...)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult SchemaEntityService::RoundCapabilities(KeelPluginHandle plugin, std::uint32_t* capabilities)
{
    if (!capabilities)
        return KEEL_RESULT_INVALID_ARGUMENT;

    std::scoped_lock state_lock(host_.state_mutex_);

    if (!PluginReady(plugin))
        return KEEL_RESULT_NOT_READY;

    if (!adapter_.IsGameThread())
        return KEEL_RESULT_WRONG_THREAD;

    std::uint32_t supported{};
    const auto result =
        host_.adapter_module_ ? host_.adapter_module_->RoundCapabilities(supported) : KEEL_RESULT_UNSUPPORTED;

    if (result == KEEL_RESULT_OK)
        *capabilities = supported;

    return result;
}

KeelResult SchemaEntityService::TerminateRoundEntry(KeelPluginHandle plugin, const KeelRoundTermination* request)
{
    try
    {
        auto* service = active_.load(std::memory_order_acquire);
        return service ? service->TerminateRound(plugin, request) : KEEL_RESULT_NOT_READY;
    }
    catch (...)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult SchemaEntityService::TerminateRound(KeelPluginHandle plugin, const KeelRoundTermination* input)
{
    if (!input || input->size != sizeof(*input))
        return KEEL_RESULT_INVALID_ARGUMENT;

    const auto request = *input;

    if (request.reserved || !std::isfinite(request.delay) || request.delay < 0 || request.delay > 3600 || request.team < 0)
        return KEEL_RESULT_INVALID_ARGUMENT;

    std::scoped_lock state_lock(host_.state_mutex_);

    if (!PluginReady(plugin))
        return KEEL_RESULT_NOT_READY;

    if (!adapter_.IsGameThread())
        return KEEL_RESULT_WRONG_THREAD;

    auto* owner = host_.PluginByHandle(plugin);

    if (!owner || owner->active_native_operations == UINT32_MAX)
        return KEEL_RESULT_BUSY;

    ++owner->active_native_operations;

    struct Hold
    {
        std::uint32_t& count;

        ~Hold()
        {
            --count;
        }
    } hold{owner->active_native_operations};
    return host_.adapter_module_ ? host_.adapter_module_->TerminateRound(request) : KEEL_RESULT_UNSUPPORTED;
}

const KeelEntityToolsApi& SchemaEntityService::EntityToolsApi() const noexcept
{
    return entity_tools_api_;
}

KeelResult SchemaEntityService::ToolCapabilitiesEntry(KeelPluginHandle plugin, std::uint32_t* flags)
{
    if (!flags)
        return KEEL_RESULT_INVALID_ARGUMENT;

    *flags = 0;

    try {
        auto* service = active_.load(std::memory_order_acquire);
        return service ? service->EntityTool(plugin,0,0,nullptr,nullptr,flags) : KEEL_RESULT_NOT_READY;
    }
    catch (...)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult
SchemaEntityService::TeleportEntry(KeelPluginHandle plugin, KeelEntityHandle entity, const KeelEntityTeleport* request)
{
    if (!entity || !request || request->size != sizeof(*request) || !request->flags || (request->flags & ~7u))
        return KEEL_RESULT_INVALID_ARGUMENT;

    const auto copy = *request;
    const float* vectors[]{copy.position,copy.angles,copy.velocity};

    for (unsigned i = 0; i < 3; ++i) if (copy.flags & (1u<<i))
            for (unsigned j = 0; j < 3; ++j)
                if (!std::isfinite(vectors[i][j]))
                    return KEEL_RESULT_INVALID_ARGUMENT;

    try {
        auto* service = active_.load(std::memory_order_acquire);
        return service ? service->EntityTool(plugin, entity, KEELS2_ENTITY_TOOL_TELEPORT, &copy, nullptr, nullptr)
                       : KEEL_RESULT_NOT_READY;
    }
    catch (...)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult SchemaEntityService::SetModelEntry(KeelPluginHandle plugin, KeelEntityHandle entity, const char* model)
{
    if (!entity || !model || !*model)
        return KEEL_RESULT_INVALID_ARGUMENT;

    std::array<char,KEELS2_ENTITY_MODEL_MAX_BYTES+1> copy{};
    std::size_t length{};

    while (length < copy.size() && model[length]) {
        const auto c = static_cast<unsigned char>(model[length]);

        if (c < 32 || c == 127)
            return KEEL_RESULT_INVALID_ARGUMENT;

        copy[length++] = static_cast<char>(c);
    }

    if (length == copy.size())
        return KEEL_RESULT_INVALID_ARGUMENT;

    try {
        auto* service = active_.load(std::memory_order_acquire);
        return service
                   ? service->EntityTool(plugin, entity, KEELS2_ENTITY_TOOL_SET_MODEL, nullptr, copy.data(), nullptr)
                   : KEEL_RESULT_NOT_READY;
    }
    catch (...)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult SchemaEntityService::RemoveEntry(KeelPluginHandle plugin, KeelEntityHandle entity)
{
    if (!entity)
        return KEEL_RESULT_INVALID_ARGUMENT;

    try {
        auto* service = active_.load(std::memory_order_acquire);
        return service ? service->EntityTool(plugin, entity, KEELS2_ENTITY_TOOL_REMOVE, nullptr, nullptr, nullptr)
                       : KEEL_RESULT_NOT_READY;
    }
    catch (...)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult SchemaEntityService::EntityTool(KeelPluginHandle plugin, KeelEntityHandle entity, std::uint32_t kind,
    const KeelEntityTeleport* request, const char* model, std::uint32_t* capabilities)
{
    std::scoped_lock state_lock(host_.state_mutex_);

    if (!PluginReady(plugin))
        return KEEL_RESULT_NOT_READY;

    if (!adapter_.IsGameThread())
        return KEEL_RESULT_WRONG_THREAD;

    auto* owner = host_.PluginByHandle(plugin);

    if (!owner || owner->cleanup_pending || owner->transitioning || owner->active_native_operations == UINT32_MAX ||
        entity_tools_depth_ >= 8)
        return KEEL_RESULT_BUSY;

    ++owner->active_native_operations;
    ++entity_tools_depth_;

    struct Hold
    {
        std::uint32_t& active;
        unsigned& depth;

        ~Hold()
        {
            --active;
            --depth;
        }
    } hold{owner->active_native_operations, entity_tools_depth_};

    if (!kind) {
        std::uint32_t supported{};
        const auto result =
            host_.adapter_module_ ? host_.adapter_module_->EntityToolCapabilities(supported) : KEEL_RESULT_UNSUPPORTED;

        if (result == KEEL_RESULT_OK)
            *capabilities = supported;

        return result;
    }

    GameEntityIdentity identity{};
    {
        std::scoped_lock lock(registry_mutex_);
        const auto found = entities_.find(entity);

        if (found == entities_.end() || !EntityAccessible(found->second, plugin))
            return KEEL_RESULT_NOT_FOUND;

        identity = found->second.entity;
    }

    std::string error;
    const auto valid = adapter_.ValidateEntity(identity,error);

    if (valid != KEEL_RESULT_OK)
        return valid;

    return host_.adapter_module_ ? host_.adapter_module_->ApplyEntityTool(identity, kind, request, model)
                                 : KEEL_RESULT_UNSUPPORTED;
}

KeelResult SchemaEntityService::WriteCapabilitiesEntry(KeelPluginHandle plugin, std::uint32_t* capabilities)
{
    if (capabilities)
        *capabilities = 0;

    try
    {
        auto* service = active_.load(std::memory_order_acquire);
        return service ? service->WriteCapabilities(plugin, capabilities) : KEEL_RESULT_NOT_READY;
    }
    catch (...)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult SchemaEntityService::WriteCapabilities(KeelPluginHandle plugin, std::uint32_t* capabilities)
{
    if (!capabilities)
        return KEEL_RESULT_INVALID_ARGUMENT;

    std::scoped_lock state_lock(host_.state_mutex_);

    if (!PluginReady(plugin))
        return KEEL_RESULT_NOT_READY;

    if (!adapter_.IsGameThread())
        return KEEL_RESULT_WRONG_THREAD;

    std::uint32_t supported{};
    const auto result =
        host_.adapter_module_ ? host_.adapter_module_->EntityWriteCapabilities(supported) : KEEL_RESULT_UNSUPPORTED;

    if (result == KEEL_RESULT_OK)
        *capabilities = supported;

    return result;
}

KeelResult SchemaEntityService::WriteFieldEntry(KeelPluginHandle plugin, KeelEntityHandle entity,
    KeelSchemaFieldHandle field, const void* value, std::uint32_t size)
{
    try
    {
        auto* service = active_.load(std::memory_order_acquire);
        return service ? service->WriteField(plugin, entity, field, value, size) : KEEL_RESULT_NOT_READY;
    }
    catch (...)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult SchemaEntityService::WriteField(KeelPluginHandle plugin, KeelEntityHandle entity,
    KeelSchemaFieldHandle field, const void* value, std::uint32_t size)
{
    if (!entity || !field || !value || !size || size > 12)
        return KEEL_RESULT_INVALID_ARGUMENT;

    std::scoped_lock state_lock(host_.state_mutex_);

    if (!PluginReady(plugin))
        return KEEL_RESULT_NOT_READY;

    if (!adapter_.IsGameThread())
        return KEEL_RESULT_WRONG_THREAD;

    GameEntityIdentity identity;
    std::shared_ptr<GameSchemaField> resolved;
    {
        std::scoped_lock lock(registry_mutex_);
        const auto object = entities_.find(entity);
        const auto property = fields_.find(field);

        if (object == entities_.end() || property == fields_.end() || !EntityAccessible(object->second, plugin) ||
            property->second.owner != plugin)
            return KEEL_RESULT_NOT_FOUND;

        identity = object->second.entity;
        resolved = property->second.field;
    }

    if (resolved->value_type == KEELS2_SCHEMA_ENTITY_HANDLE)
        return KEEL_RESULT_UNSUPPORTED;

    if (size != resolved->value_size)
        return KEEL_RESULT_INVALID_ARGUMENT;

    std::array<unsigned char, 12> copy{};
    std::memcpy(copy.data(), value, size);

    if (resolved->value_type == KEELS2_SCHEMA_BOOL && copy[0] > 1)
        return KEEL_RESULT_INVALID_ARGUMENT;

    if (resolved->value_type == KEELS2_SCHEMA_FLOAT32 || resolved->value_type == KEELS2_SCHEMA_VECTOR3)
        for (unsigned at = 0; at < size; at += sizeof(float))
        {
            float number{};
            std::memcpy(&number, copy.data() + at, sizeof(number));

            if (!std::isfinite(number))
                return KEEL_RESULT_INVALID_ARGUMENT;
        }

    if (resolved->value_type == KEELS2_SCHEMA_FLOAT64)
    {
        double number{};
        std::memcpy(&number, copy.data(), sizeof(number));

        if (!std::isfinite(number))
            return KEEL_RESULT_INVALID_ARGUMENT;
    }

    std::string error;
    const auto valid = adapter_.ValidateEntity(identity, error);

    if (valid != KEEL_RESULT_OK)
        return valid;

    auto* owner = host_.PluginByHandle(plugin);

    if (!owner || owner->active_native_operations == UINT32_MAX)
        return KEEL_RESULT_BUSY;

    ++owner->active_native_operations;

    struct Hold
    {
        std::uint32_t& count;

        ~Hold()
        {
            --count;
        }
    } hold{owner->active_native_operations};
    return host_.adapter_module_ ? host_.adapter_module_->WriteEntityField(identity, *resolved, copy.data(), size)
                                 : KEEL_RESULT_UNSUPPORTED;
}

KeelResult SchemaEntityService::ReleasePlugin(KeelPluginHandle plugin)
{
    if (!plugin)
        return KEEL_RESULT_INVALID_ARGUMENT;

    std::scoped_lock state_lock(host_.state_mutex_);
    auto* owner = host_.PluginByHandle(plugin);

    if (owner && owner->active_native_operations == UINT32_MAX)
        return KEEL_RESULT_BUSY;

    std::vector<std::shared_ptr<Construction>> pending;
    {
        std::scoped_lock lock(registry_mutex_);

        for (const auto& [handle,record] : entities_) {
            static_cast<void>(handle);
            const auto& state = record.construction;

            if (record.owner == plugin && record.construction_owner && state && (state->initializing || state->token)) {
                if (state->thread != std::this_thread::get_id())
                    return KEEL_RESULT_WRONG_THREAD;

                pending.push_back(state);
            }
        }

        ReleaseOutputs(plugin);
        // Invalidate all observers before the first cancellation callback.
        for (const auto& state : pending)
            state->closed.store(true, std::memory_order_release);

        std::erase_if(fields_,
                      [plugin](const auto& entry)
                      {
                          return entry.second.owner == plugin;
                      });

        std::erase_if(entities_,
                      [plugin](const auto& entry)
                      {
                          return entry.second.owner == plugin;
                      });
    }

    KeelResult result = KEEL_RESULT_OK;

    for (const auto& state : pending) {
        const auto current = CancelConstruction(state);

        if (result == KEEL_RESULT_OK)
            result = current;
    }

    return result;
}

bool SchemaEntityService::Shutdown()
{
    std::scoped_lock state_lock(host_.state_mutex_);

    if (!StopOutputs())
        return false;

    std::vector<std::shared_ptr<Construction>> pending;
    {
        std::scoped_lock lock(registry_mutex_);

        if (shutting_down_.load(std::memory_order_acquire))
            return true;

        for (const auto& [handle,record] : entities_) {
            static_cast<void>(handle);
            const auto& state = record.construction;

            if (record.construction_owner && state && (state->initializing || state->token)) {
                if (state->thread != std::this_thread::get_id())
                    return false;

                const auto* owner = host_.PluginByHandle(state->owner);

                if (owner && owner->active_native_operations == UINT32_MAX)
                    return false;

                pending.push_back(state);
            }
        }

        shutting_down_.store(true,std::memory_order_release);

        for (const auto& state : pending)
            state->closed.store(true, std::memory_order_release);

        fields_.clear();
        entities_.clear();
        field_cache_.clear();
    }

    bool complete = true;

    for (const auto& state : pending)
        if (CancelConstruction(state) != KEEL_RESULT_OK)
            complete = false;

    return complete;
}

KeelResult SchemaEntityService::ResolveFieldEntry(
    KeelPluginHandle plugin,
    const KeelSchemaFieldSpec* spec,
    KeelSchemaFieldHandle* field)
{
    SchemaEntityService* service = active_.load(std::memory_order_acquire);

    if (!service)
    {
        return KEEL_RESULT_NOT_READY;
    }

    try
    {
        return service->ResolveField(plugin, spec, field);
    }
    catch (...)
    {
        service->host_.Write(KEEL_LOG_ERROR, "exception while resolving a schema field");
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult SchemaEntityService::ReleaseFieldEntry(
    KeelPluginHandle plugin,
    KeelSchemaFieldHandle field)
{
    SchemaEntityService* service = active_.load(std::memory_order_acquire);

    if (!service)
    {
        return KEEL_RESULT_NOT_READY;
    }

    try
    {
        return service->ReleaseField(plugin, field);
    }
    catch (...)
    {
        service->host_.Write(KEEL_LOG_ERROR, "exception while releasing a schema field");
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult SchemaEntityService::DescribeFieldEntry(
    KeelPluginHandle plugin,
    KeelSchemaFieldHandle field,
    KeelSchemaFieldInfo* info)
{
    SchemaEntityService* service = active_.load(std::memory_order_acquire);

    if (!service)
    {
        return KEEL_RESULT_NOT_READY;
    }

    try
    {
        return service->DescribeField(plugin, field, info);
    }
    catch (...)
    {
        service->host_.Write(KEEL_LOG_ERROR, "exception while describing a schema field");
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult SchemaEntityService::FindEntityByIndexEntry(
    KeelPluginHandle plugin,
    std::int32_t index,
    KeelEntityHandle* entity)
{
    SchemaEntityService* service = active_.load(std::memory_order_acquire);

    if (!service)
    {
        return KEEL_RESULT_NOT_READY;
    }

    try
    {
        return service->FindEntityByIndex(plugin, index, entity);
    }
    catch (...)
    {
        service->host_.Write(KEEL_LOG_ERROR, "exception while finding an entity by index");
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult SchemaEntityService::FindEntityBySource2HandleEntry(
    KeelPluginHandle plugin,
    std::uint32_t source2_handle,
    KeelEntityHandle* entity)
{
    SchemaEntityService* service = active_.load(std::memory_order_acquire);

    if (!service)
    {
        return KEEL_RESULT_NOT_READY;
    }

    try
    {
        return service->FindEntityBySource2Handle(plugin, source2_handle, entity);
    }
    catch (...)
    {
        service->host_.Write(
            KEEL_LOG_ERROR,
            "exception while finding an entity by Source 2 handle");

        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult SchemaEntityService::ReleaseEntityEntry(
    KeelPluginHandle plugin,
    KeelEntityHandle entity)
{
    SchemaEntityService* service = active_.load(std::memory_order_acquire);

    if (!service)
    {
        return KEEL_RESULT_NOT_READY;
    }

    try
    {
        return service->ReleaseEntity(plugin, entity);
    }
    catch (...)
    {
        service->host_.Write(KEEL_LOG_ERROR, "exception while releasing an entity handle");
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult SchemaEntityService::DescribeEntityEntry(
    KeelPluginHandle plugin,
    KeelEntityHandle entity,
    KeelEntityInfo* info)
{
    SchemaEntityService* service = active_.load(std::memory_order_acquire);

    if (!service)
    {
        return KEEL_RESULT_NOT_READY;
    }

    try
    {
        return service->DescribeEntity(plugin, entity, info);
    }
    catch (...)
    {
        service->host_.Write(KEEL_LOG_ERROR, "exception while describing an entity handle");
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult SchemaEntityService::EqualEntityEntry(
    KeelPluginHandle plugin,
    KeelEntityHandle left,
    KeelEntityHandle right,
    KeelBool* equal)
{
    SchemaEntityService* service = active_.load(std::memory_order_acquire);

    if (!service)
    {
        return KEEL_RESULT_NOT_READY;
    }

    try
    {
        return service->EqualEntity(plugin, left, right, equal);
    }
    catch (...)
    {
        service->host_.Write(KEEL_LOG_ERROR, "exception while comparing entity handles");
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult SchemaEntityService::ReadEntityFieldEntry(
    KeelPluginHandle plugin,
    KeelEntityHandle entity,
    KeelSchemaFieldHandle field,
    void* value,
    std::uint32_t value_size)
{
    SchemaEntityService* service = active_.load(std::memory_order_acquire);

    if (!service)
    {
        return KEEL_RESULT_NOT_READY;
    }

    try
    {
        return service->ReadEntityField(plugin, entity, field, value, value_size);
    }
    catch (...)
    {
        service->host_.Write(KEEL_LOG_ERROR, "exception while reading an entity field");
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult SchemaEntityService::ResolveField(
    KeelPluginHandle plugin,
    const KeelSchemaFieldSpec* spec,
    KeelSchemaFieldHandle* output)
{
    if (output)
    {
        *output = 0;
    }

    if (!spec || !output || spec->size != sizeof(KeelSchemaFieldSpec) ||
        spec->reserved != 0 || spec->module != KEELS2_SCHEMA_MODULE_SERVER ||
        !ValidValueType(spec->value_type) || !ValidSchemaName(spec->class_name) ||
        !ValidSchemaName(spec->field_name))
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }

    std::scoped_lock state_lock(host_.state_mutex_);

    if (!PluginReady(plugin))
    {
        return KEEL_RESULT_NOT_READY;
    }

    if (!adapter_.IsGameThread())
    {
        return KEEL_RESULT_WRONG_THREAD;
    }

    const std::string key = FieldCacheKey(host_.compatibility_profile_, *spec);
    std::shared_ptr<GameSchemaField> descriptor;
    {
        std::scoped_lock lock(registry_mutex_);

        if (shutting_down_.load(std::memory_order_acquire))
        {
            return KEEL_RESULT_NOT_READY;
        }

        const auto cached = field_cache_.find(key);

        if (cached != field_cache_.end())
        {
            descriptor = cached->second;
        }
    }

    if (!descriptor)
    {
        auto resolved = std::make_shared<GameSchemaField>();
        std::string error;
        const KeelResult result = adapter_.ResolveSchemaField(*spec, *resolved, error);

        if (result != KEEL_RESULT_OK)
        {
            return result;
        }

        std::scoped_lock lock(registry_mutex_);

        if (shutting_down_.load(std::memory_order_acquire))
        {
            return KEEL_RESULT_NOT_READY;
        }

        descriptor = field_cache_.emplace(key, std::move(resolved)).first->second;
    }

    std::scoped_lock lock(registry_mutex_);

    if (shutting_down_.load(std::memory_order_acquire) || next_field_ == 0)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }

    const KeelSchemaFieldHandle handle = next_field_++;
    fields_.emplace(handle, FieldRecord{plugin, std::move(descriptor)});
    *output = handle;
    return KEEL_RESULT_OK;
}

KeelResult SchemaEntityService::ReleaseField(
    KeelPluginHandle plugin,
    KeelSchemaFieldHandle field)
{
    if (!plugin || !field)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }

    std::scoped_lock lock(registry_mutex_);
    const auto record = fields_.find(field);

    if (record == fields_.end() || record->second.owner != plugin)
    {
        return KEEL_RESULT_NOT_FOUND;
    }

    fields_.erase(record);
    return KEEL_RESULT_OK;
}

KeelResult SchemaEntityService::DescribeField(
    KeelPluginHandle plugin,
    KeelSchemaFieldHandle field,
    KeelSchemaFieldInfo* info)
{
    if (!info || info->size != sizeof(KeelSchemaFieldInfo) || !field)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }

    const std::uint32_t size = info->size;
    *info = {};
    info->size = size;
    std::scoped_lock state_lock(host_.state_mutex_);

    if (!PluginReady(plugin))
    {
        return KEEL_RESULT_NOT_READY;
    }

    std::scoped_lock lock(registry_mutex_);
    const auto record = fields_.find(field);

    if (record == fields_.end() || record->second.owner != plugin || !record->second.field)
    {
        return KEEL_RESULT_NOT_FOUND;
    }

    const GameSchemaField& value = *record->second.field;
    *info = {
        sizeof(KeelSchemaFieldInfo),
        value.module,
        value.value_type,
        value.value_size,
        value.value_alignment,
        value.offset,
        0,
        value.class_name.c_str(),
        value.field_name.c_str(),
        value.module_name.c_str(),
        value.compatibility_profile.c_str()
    };
    return KEEL_RESULT_OK;
}

KeelResult SchemaEntityService::FindEntityByIndex(
    KeelPluginHandle plugin,
    std::int32_t index,
    KeelEntityHandle* output)
{
    if (output)
    {
        *output = 0;
    }

    if (!output || index < 0)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }

    std::scoped_lock state_lock(host_.state_mutex_);

    if (!PluginReady(plugin))
    {
        return KEEL_RESULT_NOT_READY;
    }

    if (!adapter_.IsGameThread())
    {
        return KEEL_RESULT_WRONG_THREAD;
    }

    GameEntityIdentity identity;
    std::string error;
    const KeelResult result = adapter_.FindEntityByIndex(index, identity, error);

    if (result != KEEL_RESULT_OK)
    {
        return result;
    }

    std::scoped_lock lock(registry_mutex_);

    if (shutting_down_.load(std::memory_order_acquire) || next_entity_ == 0)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }

    const KeelEntityHandle handle = next_entity_++;
    entities_.emplace(handle, EntityRecord{plugin, identity});
    *output = handle;
    return KEEL_RESULT_OK;
}

KeelResult SchemaEntityService::FindEntityBySource2Handle(
    KeelPluginHandle plugin,
    std::uint32_t source2_handle,
    KeelEntityHandle* output)
{
    if (output)
    {
        *output = 0;
    }

    if (!output || source2_handle == KEELS2_INVALID_SOURCE2_ENTITY_HANDLE)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }

    std::scoped_lock state_lock(host_.state_mutex_);

    if (!PluginReady(plugin))
    {
        return KEEL_RESULT_NOT_READY;
    }

    if (!adapter_.IsGameThread())
    {
        return KEEL_RESULT_WRONG_THREAD;
    }

    GameEntityIdentity identity;
    std::string error;
    const KeelResult result = adapter_.FindEntityBySource2Handle(
        source2_handle,
        identity,
        error);

    if (result != KEEL_RESULT_OK)
    {
        return result;
    }

    std::scoped_lock lock(registry_mutex_);

    if (shutting_down_.load(std::memory_order_acquire) || next_entity_ == 0)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }

    const KeelEntityHandle handle = next_entity_++;
    entities_.emplace(handle, EntityRecord{plugin, identity});
    *output = handle;
    return KEEL_RESULT_OK;
}

KeelResult SchemaEntityService::ReleaseEntity(
    KeelPluginHandle plugin,
    KeelEntityHandle entity)
{
    if (!plugin || !entity)
        return KEEL_RESULT_INVALID_ARGUMENT;

    {
        std::scoped_lock lock(registry_mutex_);
        const auto record = entities_.find(entity);

        if (record == entities_.end() || record->second.owner != plugin)
            return KEEL_RESULT_NOT_FOUND;

        const auto& state = record->second.construction;
        // Ordinary handles and observers have no engine cleanup. Keep their
        // release independent of the host lock, including from worker threads.
        if (!record->second.construction_owner || !state || (!state->initializing && !state->token)) {
            entities_.erase(record);
            return KEEL_RESULT_OK;
        }

        if (state->thread != std::this_thread::get_id())
            return KEEL_RESULT_WRONG_THREAD;
    }

    std::scoped_lock state_lock(host_.state_mutex_);
    std::shared_ptr<Construction> pending;
    {
        std::scoped_lock lock(registry_mutex_);
        const auto record = entities_.find(entity);

        if (record == entities_.end() || record->second.owner != plugin)
            return KEEL_RESULT_NOT_FOUND;

        auto* owner = host_.PluginByHandle(plugin);

        if (owner && owner->active_native_operations == UINT32_MAX)
            return KEEL_RESULT_BUSY;

        pending = record->second.construction;
        pending->closed.store(true,std::memory_order_release);
        entities_.erase(record);
    }

    return CancelConstruction(pending);
}

KeelResult SchemaEntityService::DescribeEntity(
    KeelPluginHandle plugin,
    KeelEntityHandle entity,
    KeelEntityInfo* info)
{
    if (!info || info->size != sizeof(KeelEntityInfo) || !entity)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }

    const std::uint32_t size = info->size;
    *info = {};
    info->size = size;
    std::scoped_lock state_lock(host_.state_mutex_);

    if (!PluginReady(plugin))
    {
        return KEEL_RESULT_NOT_READY;
    }

    if (!adapter_.IsGameThread())
    {
        return KEEL_RESULT_WRONG_THREAD;
    }

    GameEntityIdentity identity;
    {
        std::scoped_lock lock(registry_mutex_);
        const auto record = entities_.find(entity);

        if (record == entities_.end() || !EntityAccessible(record->second, plugin))
        {
            return KEEL_RESULT_NOT_FOUND;
        }

        identity = record->second.entity;
    }

    std::string error;
    const KeelResult result = adapter_.ValidateEntity(identity, error);

    if (result != KEEL_RESULT_OK)
    {
        return result;
    }

    *info = {
        sizeof(KeelEntityInfo),
        identity.index,
        identity.source2_handle,
        0,
        identity.epoch
    };
    return KEEL_RESULT_OK;
}

KeelResult SchemaEntityService::EqualEntity(
    KeelPluginHandle plugin,
    KeelEntityHandle left,
    KeelEntityHandle right,
    KeelBool* equal)
{
    if (equal)
    {
        *equal = KEEL_FALSE;
    }

    if (!equal || !left || !right)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }

    std::scoped_lock state_lock(host_.state_mutex_);

    if (!PluginReady(plugin))
    {
        return KEEL_RESULT_NOT_READY;
    }

    if (!adapter_.IsGameThread())
    {
        return KEEL_RESULT_WRONG_THREAD;
    }

    GameEntityIdentity left_identity;
    GameEntityIdentity right_identity;
    {
        std::scoped_lock lock(registry_mutex_);
        const auto left_record = entities_.find(left);
        const auto right_record = entities_.find(right);

        if (left_record == entities_.end() || right_record == entities_.end() ||
            !EntityAccessible(left_record->second, plugin) || !EntityAccessible(right_record->second, plugin))
        {
            return KEEL_RESULT_NOT_FOUND;
        }

        left_identity = left_record->second.entity;
        right_identity = right_record->second.entity;
    }

    std::string error;
    KeelResult result = adapter_.ValidateEntity(left_identity, error);

    if (result != KEEL_RESULT_OK)
    {
        return result;
    }

    result = adapter_.ValidateEntity(right_identity, error);

    if (result != KEEL_RESULT_OK)
    {
        return result;
    }

    *equal = left_identity.epoch == right_identity.epoch &&
            left_identity.source2_handle == right_identity.source2_handle
        ? KEEL_TRUE
        : KEEL_FALSE;

    return KEEL_RESULT_OK;
}

KeelResult SchemaEntityService::ReadEntityField(
    KeelPluginHandle plugin,
    KeelEntityHandle entity,
    KeelSchemaFieldHandle field,
    void* value,
    std::uint32_t value_size)
{
    if (!value || !value_size || !entity || !field)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }

    std::memset(value, 0, value_size);
    std::scoped_lock state_lock(host_.state_mutex_);

    if (!PluginReady(plugin))
    {
        return KEEL_RESULT_NOT_READY;
    }

    if (!adapter_.IsGameThread())
    {
        return KEEL_RESULT_WRONG_THREAD;
    }

    GameEntityIdentity identity;
    std::shared_ptr<GameSchemaField> descriptor;
    {
        std::scoped_lock lock(registry_mutex_);
        const auto entity_record = entities_.find(entity);
        const auto field_record = fields_.find(field);

        if (entity_record == entities_.end() || field_record == fields_.end() ||
            !EntityAccessible(entity_record->second, plugin) || field_record->second.owner != plugin ||
            !field_record->second.field)
        {
            return KEEL_RESULT_NOT_FOUND;
        }

        identity = entity_record->second.entity;
        descriptor = field_record->second.field;
    }

    if (value_size != descriptor->value_size)
    {
        return KEEL_RESULT_INCOMPATIBLE;
    }

    std::string error;
    return adapter_.ReadEntityField(
        identity,
        *descriptor,
        value,
        value_size,
        error);
}

bool SchemaEntityService::PluginReady(KeelPluginHandle plugin) const noexcept
{
    PluginRecord* owner = host_.PluginByHandle(plugin);
    return !shutting_down_.load(std::memory_order_acquire) &&
        host_.accepting_resources_ && owner &&
        owner->accepting_resources;
}

bool SchemaEntityService::ValidSchemaName(const char* name) noexcept
{
    if (!name || !name[0])
    {
        return false;
    }

    std::size_t length{};

    for (; name[length]; ++length)
    {
        if (length >= 255)
        {
            return false;
        }

        const unsigned char character = static_cast<unsigned char>(name[length]);

        if (!std::isalnum(character) && character != '_' && character != ':')
        {
            return false;
        }
    }

    return true;
}

bool SchemaEntityService::ValidValueType(KeelSchemaValueType type) noexcept
{
    return type >= KEELS2_SCHEMA_CHAR && type <= KEELS2_SCHEMA_VECTOR3;
}

std::string SchemaEntityService::FieldCacheKey(
    const std::string& profile,
    const KeelSchemaFieldSpec& spec)
{
    std::string key;
    key.reserve(profile.size() + std::strlen(spec.class_name) +
        std::strlen(spec.field_name) + 32);

    key.append(profile);
    key.push_back('\x1f');
    key.append(std::to_string(spec.module));
    key.push_back('\x1f');
    key.append(spec.class_name);
    key.push_back('\x1f');
    key.append(spec.field_name);
    key.push_back('\x1f');
    key.append(std::to_string(spec.value_type));
    return key;
}

}
