#include <keels2/keels2.hpp>

#include <string.h>

namespace
{

class SchemaEntityLivePlugin final : public keels2::Plugin
{
public:
    static constexpr keels2::PluginInfo Info{
        "Schema Entity Live Gate",
        "KeelS2 Project",
        "0.6-acceptance",
        "Genuine CS2 schema and entity acceptance helper"
    };

    bool Load() override
    {
        if (!FindSchemaField("CBaseEntity", "m_iHealth", health_) ||
            !CreateCommand(
                "keel_schema_entity_live",
                "Runs the schema and entity live acceptance stages",
                &SchemaEntityLivePlugin::Check))
        {
            LogError("schema field resolution or command registration failed");
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

    void OnLevelInit(KeyValues*, ILoopModePrerequisiteRegistry*) override
    {
        LogMessage("level initialized");
    }

    void OnLevelShutdown() override
    {
        if (!expect_map_invalidation_)
        {
            LogMessage("level shutdown observed");
            return;
        }
        int32 health;
        LogMessage(!retained_.Valid() && !retained_.Read(health_, health)
            ? "map epoch invalidation passed"
            : "map epoch invalidation failed");
        retained_.Reset();
        expect_map_invalidation_ = false;
    }

private:
    void Check(const CCommandContext&, const CCommand& command)
    {
        if (command.ArgC() != 2)
        {
            LogError("usage: keel_schema_entity_live [snapshot|capture|stale|replacement|world]");
            return;
        }
        if (strcmp(command[1], "snapshot") == 0)
        {
            Snapshot();
            return;
        }
        if (strcmp(command[1], "capture") == 0)
        {
            Capture();
            return;
        }
        if (strcmp(command[1], "stale") == 0)
        {
            Stale();
            return;
        }
        if (strcmp(command[1], "replacement") == 0)
        {
            Replacement();
            return;
        }
        if (strcmp(command[1], "world") == 0)
        {
            World();
            return;
        }
        LogError("unknown schema and entity live acceptance stage");
    }

    void Snapshot()
    {
        retained_.Reset();
        original_index_ = -1;
        original_handle_ = KEELS2_INVALID_SOURCE2_ENTITY_HANDLE;
        expect_map_invalidation_ = false;
        for (int index = 0; index < 4096; index++)
        {
            keels2::Entity entity;
            snapshot_[index] = FindEntity(index, entity)
                ? entity.Source2Handle()
                : KEELS2_INVALID_SOURCE2_ENTITY_HANDLE;
        }
        LogMessage("entity snapshot captured");
    }

    void Capture()
    {
        for (int index = 0; index < 4096; index++)
        {
            keels2::Entity entity;
            int32 health;
            if (!FindEntity(index, entity) ||
                entity.Source2Handle() == snapshot_[index] ||
                !entity.Read(health_, health) || health != 100)
            {
                continue;
            }
            keels2::Entity same;
            const uint32 source2_handle = entity.Source2Handle();
            if (!FindEntity(CEntityHandle(source2_handle), same) || !entity.Same(same) ||
                !FindEntity(index, retained_))
            {
                LogError("created entity handle lookup failed");
                return;
            }
            original_index_ = index;
            original_handle_ = source2_handle;
            LogMessage("entity creation, lookup, and typed read passed");
            return;
        }
        LogMessage("waiting for a newly created player entity");
    }

    void Stale()
    {
        int32 health;
        keels2::Entity missing;
        if (original_index_ >= 0 &&
            original_handle_ != KEELS2_INVALID_SOURCE2_ENTITY_HANDLE &&
            !retained_.Valid() && !retained_.Read(health_, health) &&
            !retained_.Same(retained_) &&
            !FindEntity(CEntityHandle(original_handle_), missing))
        {
            LogMessage("entity destruction invalidation passed");
            return;
        }
        LogMessage("waiting for retained entity destruction");
    }

    void Replacement()
    {
        if (original_index_ < 0)
        {
            LogError("replacement requested before entity capture");
            return;
        }
        for (int index = 0; index < 4096; index++)
        {
            keels2::Entity replacement;
            int32 health;
            if (!FindEntity(index, replacement) ||
                replacement.Source2Handle() == snapshot_[index] ||
                replacement.Source2Handle() == original_handle_ ||
                !replacement.Read(health_, health) || health != 100)
            {
                continue;
            }
            keels2::Entity same;
            if (!FindEntity(CEntityHandle(replacement.Source2Handle()), same) ||
                !replacement.Same(same) || !FindEntity(index, retained_))
            {
                LogError("replacement entity handle lookup failed");
                return;
            }
            expect_map_invalidation_ = true;
            LogMessage("replacement entity validation passed");
            return;
        }
        LogMessage("waiting for replacement entity");
    }

    void World()
    {
        int32 health;
        if (FindEntity(0, retained_) && retained_.Read(health_, health))
        {
            LogMessage("post-reload world lookup and typed read passed");
            return;
        }
        LogMessage("waiting for post-reload world entity");
    }

    keels2::SchemaField<int32> health_;
    keels2::Entity retained_;
    uint32 snapshot_[4096]{};
    int original_index_ = -1;
    uint32 original_handle_ = KEELS2_INVALID_SOURCE2_ENTITY_HANDLE;
    bool expect_map_invalidation_ = false;
};

}

KEELS2_PLUGIN(SchemaEntityLivePlugin)
