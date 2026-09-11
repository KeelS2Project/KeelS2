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
                !retained_.Same(retained_) && retained_.Kill() == KEEL_RESULT_NOT_FOUND
            ? "entity destruction invalidation passed"
            : "entity destruction invalidation failed");
    }

    void OffThread()
    {
        keels2::SchemaField<int32> field;
        keels2::Entity entity;
        int32 health;
        LogMessage(!FindSchemaField("CBaseEntity", "m_iHealth", field) &&
                !FindEntity(7, entity) && !retained_.Valid() &&
                !retained_.Read(health_, health) && health == 0 &&
                retained_.Kill() == KEEL_RESULT_WRONG_THREAD
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
