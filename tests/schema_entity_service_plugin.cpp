#include <keels2/keels2.hpp>

#include <string.h>

namespace
{

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
        if (!resolved || !CreateCommand(
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

private:
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
            retained_.Same(same) && retained_.Read(health_, health) && health == 42)
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
                !retained_.Same(retained_)
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
                !retained_.Read(health_, health) && health == 0
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

    keels2::SchemaField<int32> health_;
    keels2::Entity retained_;
    uint32 original_handle_ = 0;
};

}

KEELS2_PLUGIN(SchemaEntityPlugin)
