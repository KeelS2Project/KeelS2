#include <keels2/player_management.h>
#include <keels2/entity_writes.h>
#include <keels2/round_control.h>
#include <keels2/player_statistics.h>
#include <keels2/keels2.hpp>

#include <string.h>
#include <limits>

namespace
{
const KeelHostApi* g_api{};
KeelPluginHandle g_owner{};
void (*g_deferred_dispatch)(){};
class DeferredDispatch
{
public:
    virtual void Run() { if (g_deferred_dispatch) g_deferred_dispatch(); }
};
DeferredDispatch g_deferred_target;

class SchemaEntityPlugin final : public keels2::Plugin
{
public:
    static constexpr keels2::PluginInfo Info{
        "Schema Entity Test",
        "KeelS2 Project",
        "0.6",
        "Schema and entity service integration fixture"
    };

    bool Load() override
    {
        keels2::SchemaField<int32> repeated;
        keels2::SchemaField<float32> wrong_type;
        keels2::SchemaField<int32> missing;
        keels2::SchemaField<int32> malformed;
        keels2::Entity unavailable;
        const bool resolved = FindSchemaField(
                "CBaseEntity",
                "m_iHealth",
                health_) &&
            FindSchemaField("CBaseEntity", "m_iHealth", repeated) &&
            health_.Offset() == repeated.Offset() &&
            !FindSchemaField("CBaseEntity", "m_iHealth", wrong_type) &&
            !FindSchemaField("CBaseEntity", "m_missing", missing) &&
            !FindSchemaField("CBaseEntity!", "m_iHealth", malformed) &&
            !FindEntity(7, unavailable);
        if (!resolved || !HookPre(&g_deferred_target, &DeferredDispatch::Run, &SchemaEntityPlugin::BeforeDispatch) || !CreateCommand(
                "keel_schema_entity_check",
                "Checks schema fields and validated entity handles",
                &SchemaEntityPlugin::Check))
        {
            LogError("schema and entity load validation failed");
            return false;
        }
        LogMessage("schema field resolution passed");
        return true;
    }

    void Unload() override
    {
        LogMessage(!health_ && !retained_
            ? "schema and entity views invalidated before unload"
            : "schema or entity view remained active during unload");
    }

    void OnLevelShutdown() override
    {
        int32 health;
        LogMessage(!retained_.Valid() && !retained_.Read(health_, health)
            ? "map epoch invalidation passed"
            : "map epoch invalidation failed");
    }

    void OnGameFrame(bool, bool, bool) override {}

private:
    keels2::kh::Action BeforeDispatch() { return keels2::kh::Action::Continue; }
    void Check(const CCommandContext&, const CCommand& command)
    {
        if (command.ArgC() != 2)
        {
            LogError("usage: keel_schema_entity_check [initial|stale|reuse|offthread]");
            return;
        }
        if (strcmp(command[1], "initial") == 0)
        {
            Initial();
            return;
        }
        if (strcmp(command[1], "stale") == 0)
        {
            Stale();
            return;
        }
        if (strcmp(command[1], "reuse") == 0)
        {
            Reuse();
            return;
        }
        if (strcmp(command[1], "offthread") == 0)
        {
            OffThread();
            return;
        }
        LogError("unknown schema and entity check");
    }

    void Initial()
    {
        keels2::Entity same;
        int32 health;
        if (FindEntity(7, retained_) &&
            FindEntity(CEntityHandle(retained_.Source2Handle()), same) &&
            retained_.Same(same) && retained_.Read(health_, health) && health == 42 &&
            ReadDiagnostics() &&
            Actions())
        {
            original_handle_ = retained_.Source2Handle();
            LogMessage("entity lookup and typed read passed");
            return;
        }
        LogError("entity lookup or typed read failed");
    }

    void Stale()
    {
        int32 health;
        LogMessage(!retained_.Valid() && !retained_.Read(health_, health) &&
                health == 0 && retained_.LastResult() == KEEL_RESULT_NOT_FOUND &&
                strcmp(retained_.LastError(), "not found") == 0 &&
                !retained_.Same(retained_) && retained_.Kill() == KEEL_RESULT_NOT_FOUND &&
                !retained_.TryKill() && retained_.LastResult() == KEEL_RESULT_NOT_FOUND
            ? "entity destruction invalidation passed"
            : "entity destruction invalidation failed");
    }

    void OffThread()
    {
        const void* raw{};
        if (g_api->query_service(g_owner, KEELS2_PLAYER_MANAGEMENT_SERVICE_NAME, 1, &raw) != KEEL_RESULT_OK || !raw)
        {
            LogError("player management wrong-thread service lookup failed");
            return;
        }
        const auto* management = static_cast<const KeelPlayerManagementApi*>(raw);
        std::uint32_t capabilities = UINT32_MAX;
        const KeelPlayerManagementAction request{sizeof(request), KEELS2_PLAYER_MANAGEMENT_RESPAWN, 0, 0};
        if (management->capabilities(g_owner, &capabilities) != KEEL_RESULT_WRONG_THREAD || capabilities ||
            management->apply(g_owner, 1, &request) != KEEL_RESULT_WRONG_THREAD)
        {
            LogError("player management wrong-thread rejection failed");
            return;
        }
        if (g_api->query_service(g_owner, KEELS2_ENTITY_WRITES_SERVICE_NAME, 1, &raw) != KEEL_RESULT_OK || !raw)
        { LogError("entity write wrong-thread service lookup failed"); return; }
        const auto* writes = static_cast<const KeelEntityWritesApi*>(raw);
        const std::int32_t value = 1;
        capabilities = UINT32_MAX;
        if (writes->capabilities(g_owner, &capabilities) != KEEL_RESULT_WRONG_THREAD || capabilities ||
            writes->write_field(g_owner, 1, 1, &value, sizeof(value)) != KEEL_RESULT_WRONG_THREAD)
        { LogError("entity write wrong-thread rejection failed"); return; }
        if (g_api->query_service(g_owner, KEELS2_ROUND_CONTROL_SERVICE_NAME, 1, &raw) != KEEL_RESULT_OK || !raw)
        { LogError("round control wrong-thread service lookup failed"); return; }
        const auto* round = static_cast<const KeelRoundControlApi*>(raw);
        const KeelRoundTermination termination{sizeof(termination),8,1,0,0};
        capabilities = UINT32_MAX;
        if (round->capabilities(g_owner,&capabilities) != KEEL_RESULT_WRONG_THREAD || capabilities ||
            round->terminate(g_owner,&termination) != KEEL_RESULT_WRONG_THREAD)
        { LogError("round control wrong-thread rejection failed"); return; }
        if (g_api->query_service(g_owner,KEELS2_PLAYER_STATISTICS_SERVICE_NAME,1,&raw) != KEEL_RESULT_OK || !raw)
        { LogError("statistics wrong-thread lookup failed"); return; }
        const auto* stats = static_cast<const KeelPlayerStatisticsApi*>(raw);
        std::uint32_t reads = UINT32_MAX, writes_mask = UINT32_MAX;
        std::int32_t result_value = 99;
        if (stats->capabilities(g_owner,&reads,&writes_mask) != KEEL_RESULT_WRONG_THREAD || reads || writes_mask ||
            stats->read(g_owner,1,KEELS2_PLAYER_STAT_MONEY,&result_value) != KEEL_RESULT_WRONG_THREAD || result_value ||
            stats->write(g_owner,1,KEELS2_PLAYER_STAT_MONEY,1) != KEEL_RESULT_WRONG_THREAD)
        { LogError("statistics wrong-thread rejection failed"); return; }
        keels2::SchemaField<int32> field;
        keels2::Entity entity;
        int32 health;
        LogMessage(!FindSchemaField("CBaseEntity", "m_iHealth", field) &&
                !FindEntity(7, entity) && !retained_.Valid() &&
                !retained_.Read(health_, health) && health == 0 &&
                retained_.LastResult() == KEEL_RESULT_WRONG_THREAD &&
                strcmp(retained_.LastError(), "operation requires the game thread") == 0 &&
                retained_.Kill() == KEEL_RESULT_WRONG_THREAD && !retained_.TryKill() &&
                retained_.LastResult() == KEEL_RESULT_WRONG_THREAD
            ? "wrong-thread access rejected"
            : "wrong-thread access was accepted");
    }

    void Reuse()
    {
        int32 health;
        const bool old_stale = !retained_.Valid();
        if (old_stale && FindEntity(7, retained_) &&
            retained_.Source2Handle() != original_handle_ &&
            retained_.Read(health_, health) && health == 84)
        {
            LogMessage("entity serial reuse validation passed");
            return;
        }
        LogError("entity serial reuse validation failed");
    }

    bool ReadDiagnostics()
    {
        keels2::Entity empty;
        keels2::SchemaField<int32> empty_field;
        int32 health{99};
        if (empty.Read(health_, health) || health != 0 ||
            empty.LastResult() != KEEL_RESULT_NOT_READY || !empty.LastError()[0])
            return false;
        health = 99;
        if (retained_.Read(empty_field, health) || health != 0 ||
            retained_.LastResult() != KEEL_RESULT_NOT_READY || !retained_.LastError()[0])
            return false;
        return retained_.Read(health_, health) && health == 42 &&
            retained_.LastResult() == KEEL_RESULT_OK && !retained_.LastError()[0];
    }

    bool Actions()
    {
        const void* raw{};
        if (g_api->query_service(g_owner, KEELS2_PLAYER_ACTIONS_SERVICE_NAME, 2, &raw) != KEEL_RESULT_INCOMPATIBLE ||
            g_api->query_service(g_owner, KEELS2_PLAYER_ACTIONS_SERVICE_NAME, 1, &raw) != KEEL_RESULT_OK)
            return false;
        const auto* actions = static_cast<const KeelPlayerActionsApi*>(raw);
        if (!actions || actions->size != sizeof(*actions) || actions->api_version != 1 || !actions->apply)
            return false;
        if (g_api->query_service(g_owner, KEELS2_ENTITIES_SERVICE_NAME, 1, &raw) != KEEL_RESULT_OK)
            return false;
        const auto* entities = static_cast<const KeelEntitiesApi*>(raw);
        KeelEntityHandle entity{};
        if (entities->find_by_index(g_owner, 7, &entity) != KEEL_RESULT_OK)
            return false;
        KeelPlayerAction action{sizeof(KeelPlayerAction), KEELS2_PLAYER_ACTION_KILL, {}, 0};
        bool valid = actions->apply(g_owner, entity, &action) == KEEL_RESULT_UNSUPPORTED &&
            actions->apply(g_owner + 10000, entity, &action) == KEEL_RESULT_NOT_READY &&
            actions->apply(g_owner, entity + 10000, &action) == KEEL_RESULT_NOT_FOUND;
        const void* management_raw{};
        valid = valid && g_api->query_service(g_owner, KEELS2_PLAYER_MANAGEMENT_SERVICE_NAME, 2, &management_raw) == KEEL_RESULT_INCOMPATIBLE &&
            g_api->query_service(g_owner, KEELS2_PLAYER_MANAGEMENT_SERVICE_NAME, 1, &management_raw) == KEEL_RESULT_OK;
        const auto* management = static_cast<const KeelPlayerManagementApi*>(management_raw);
        if (!valid || !management || management->size != sizeof(*management) || management->api_version != 1 ||
            !management->capabilities || !management->apply) return false;
        std::uint32_t capabilities = UINT32_MAX;
        KeelPlayerManagementAction request{sizeof(request), KEELS2_PLAYER_MANAGEMENT_RESPAWN, 0, 0};
        valid = valid && management->capabilities(g_owner, &capabilities) == KEEL_RESULT_UNSUPPORTED && !capabilities &&
            management->apply(g_owner, entity, &request) == KEEL_RESULT_UNSUPPORTED &&
            management->apply(g_owner + 10000, entity, &request) == KEEL_RESULT_NOT_READY &&
            management->apply(g_owner, entity + 10000, &request) == KEEL_RESULT_NOT_FOUND;
        request.team = 1;
        valid = valid && management->apply(g_owner, entity, &request) == KEEL_RESULT_INVALID_ARGUMENT;
        request.team = 0; request.reserved = 1;
        valid = valid && management->apply(g_owner, entity, &request) == KEEL_RESULT_INVALID_ARGUMENT;
        request.reserved = 0; request.kind = 7;
        valid = valid && management->apply(g_owner, entity, &request) == KEEL_RESULT_INVALID_ARGUMENT;
        request.kind = KEELS2_PLAYER_MANAGEMENT_CHANGE_TEAM; request.team = 256;
        valid = valid && management->apply(g_owner, entity, &request) == KEEL_RESULT_INVALID_ARGUMENT;
        const void* stat_raw{};
        valid = valid && g_api->query_service(g_owner,KEELS2_PLAYER_STATISTICS_SERVICE_NAME,2,&stat_raw) == KEEL_RESULT_INCOMPATIBLE &&
            g_api->query_service(g_owner,KEELS2_PLAYER_STATISTICS_SERVICE_NAME,1,&stat_raw) == KEEL_RESULT_OK;
        const auto* stats = static_cast<const KeelPlayerStatisticsApi*>(stat_raw);
        if (!valid || !stats || stats->size != sizeof(*stats) || stats->api_version != 1 || !stats->capabilities || !stats->read || !stats->write) return false;
        std::uint32_t readable = UINT32_MAX, writable = UINT32_MAX;
        std::int32_t stat_value = 99;
        valid = valid && stats->capabilities(g_owner,&readable,&writable) == KEEL_RESULT_UNSUPPORTED && !readable && !writable &&
            stats->capabilities(g_owner,nullptr,&writable) == KEEL_RESULT_INVALID_ARGUMENT &&
            stats->read(g_owner,entity,KEELS2_PLAYER_STAT_MONEY,&stat_value) == KEEL_RESULT_UNSUPPORTED && !stat_value &&
            stats->write(g_owner,entity,KEELS2_PLAYER_STAT_MONEY,1) == KEEL_RESULT_UNSUPPORTED &&
            stats->write(g_owner+10000,entity,KEELS2_PLAYER_STAT_MONEY,1) == KEEL_RESULT_NOT_READY &&
            stats->write(g_owner,entity+10000,KEELS2_PLAYER_STAT_MONEY,1) == KEEL_RESULT_NOT_FOUND &&
            stats->read(g_owner,entity,KEELS2_PLAYER_STAT_MONEY,nullptr) == KEEL_RESULT_INVALID_ARGUMENT &&
            stats->write(g_owner,entity,3,1) == KEEL_RESULT_INVALID_ARGUMENT &&
            stats->write(g_owner,entity,KEELS2_PLAYER_STAT_MONEY,-1) == KEEL_RESULT_INVALID_ARGUMENT;
        const void* round_raw{};
        valid = valid && g_api->query_service(g_owner,KEELS2_ROUND_CONTROL_SERVICE_NAME,2,&round_raw) == KEEL_RESULT_INCOMPATIBLE &&
            g_api->query_service(g_owner,KEELS2_ROUND_CONTROL_SERVICE_NAME,1,&round_raw) == KEEL_RESULT_OK;
        const auto* round = static_cast<const KeelRoundControlApi*>(round_raw);
        if (!valid || !round || round->size != sizeof(*round) || round->api_version != 1 || !round->capabilities || !round->terminate) return false;
        KeelRoundTermination termination{sizeof(termination),8,1,0,0};
        capabilities = UINT32_MAX;
        valid = valid && round->capabilities(g_owner,&capabilities) == KEEL_RESULT_UNSUPPORTED && !capabilities &&
            round->capabilities(g_owner,nullptr) == KEEL_RESULT_INVALID_ARGUMENT &&
            round->terminate(g_owner,&termination) == KEEL_RESULT_UNSUPPORTED &&
            round->terminate(g_owner+10000,&termination) == KEEL_RESULT_NOT_READY &&
            round->terminate(g_owner,nullptr) == KEEL_RESULT_INVALID_ARGUMENT;
        termination.reserved = 1; valid = valid && round->terminate(g_owner,&termination) == KEEL_RESULT_INVALID_ARGUMENT;
        termination.reserved = 0; termination.delay = -1; valid = valid && round->terminate(g_owner,&termination) == KEEL_RESULT_INVALID_ARGUMENT;
        termination.delay = 1; --termination.size; valid = valid && round->terminate(g_owner,&termination) == KEEL_RESULT_INVALID_ARGUMENT;
        const void* writes_raw{};
        valid = valid && g_api->query_service(g_owner, KEELS2_ENTITY_WRITES_SERVICE_NAME, 2, &writes_raw) == KEEL_RESULT_INCOMPATIBLE &&
            g_api->query_service(g_owner, KEELS2_ENTITY_WRITES_SERVICE_NAME, 1, &writes_raw) == KEEL_RESULT_OK;
        const auto* writes = static_cast<const KeelEntityWritesApi*>(writes_raw);
        if (!valid || !writes || writes->size != sizeof(*writes) || !writes->capabilities || !writes->write_field) return false;
        if (g_api->query_service(g_owner, KEELS2_SCHEMA_SERVICE_NAME, 1, &raw) != KEEL_RESULT_OK || !raw) return false;
        const auto* schema = static_cast<const KeelSchemaApi*>(raw);
        const KeelSchemaFieldSpec spec{sizeof(spec), KEELS2_SCHEMA_MODULE_SERVER, KEELS2_SCHEMA_INT32, 0, "CBaseEntity", "m_iHealth"};
        KeelSchemaFieldHandle field{};
        if (schema->resolve_field(g_owner, &spec, &field) != KEEL_RESULT_OK) return false;
        const std::int32_t value = 77;
        capabilities = UINT32_MAX;
        valid = valid && writes->capabilities(g_owner, &capabilities) == KEEL_RESULT_UNSUPPORTED && !capabilities &&
            writes->write_field(g_owner, entity, field, &value, sizeof(value)) == KEEL_RESULT_UNSUPPORTED &&
            writes->write_field(g_owner, entity, field, &value, 1) == KEEL_RESULT_INVALID_ARGUMENT &&
            writes->write_field(g_owner + 10000, entity, field, &value, sizeof(value)) == KEEL_RESULT_NOT_READY &&
            writes->write_field(g_owner, entity + 10000, field, &value, sizeof(value)) == KEEL_RESULT_NOT_FOUND;
        const auto released = schema->release_field(g_owner, field);
        valid = valid && released == KEEL_RESULT_OK &&
            writes->write_field(g_owner, entity, field, &value, sizeof(value)) == KEEL_RESULT_NOT_FOUND;
        action.damage = 1;
        valid = valid && actions->apply(g_owner, entity, &action) == KEEL_RESULT_INVALID_ARGUMENT;
        action.kind = KEELS2_PLAYER_ACTION_IMPULSE;
        action.impulse[0] = std::numeric_limits<float>::infinity();
        valid = valid && actions->apply(g_owner, entity, &action) == KEEL_RESULT_INVALID_ARGUMENT;
        action.impulse[0] = 0;
        action.damage = -1;
        valid = valid && actions->apply(g_owner, entity, &action) == KEEL_RESULT_INVALID_ARGUMENT;
        action.damage = 0;
        valid = valid && entities->release(g_owner, entity) == KEEL_RESULT_OK &&
            actions->apply(g_owner, entity, &action) == KEEL_RESULT_NOT_FOUND;
        LogMessage(valid ? "player action service rejection checks passed" : "player action service rejection checks failed");
        return valid;
    }

    keels2::SchemaField<int32> health_;
    keels2::Entity retained_;
    uint32 original_handle_ = 0;
};

}

extern "C" KEELS2_PLUGIN_EXPORT KeelBool KeelPlugin_Query(const KeelHostQuery* query, KeelPluginInfo* info)
{
    return keels2::detail::AuthoringAdapter<SchemaEntityPlugin>::Query(query, info);
}
extern "C" KEELS2_PLUGIN_EXPORT KeelBool KeelPlugin_Manifest(const KeelHostQuery* query, KeelPluginManifest* manifest)
{
    return keels2::detail::AuthoringAdapter<SchemaEntityPlugin>::Manifest(query, manifest);
}
extern "C" KEELS2_PLUGIN_EXPORT KeelBool KeelPlugin_Load(const KeelHostApi* api, KeelPluginHandle plugin)
{
    g_api = api;
    g_owner = plugin;
    return keels2::detail::AuthoringAdapter<SchemaEntityPlugin>::Load(api, plugin);
}
extern "C" KEELS2_PLUGIN_EXPORT void KeelPlugin_Unload(KeelPluginHandle plugin)
{
    keels2::detail::AuthoringAdapter<SchemaEntityPlugin>::Unload(plugin);
}
extern "C" KEELS2_PLUGIN_EXPORT void* KeelTest_DeferredDispatch(void (*callback)())
{
    g_deferred_dispatch = callback;
    return &g_deferred_target;
}
