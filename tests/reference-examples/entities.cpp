#include <keels2/authoring.hpp>
#include <utility>

namespace docs
{
using namespace keels2::authoring;

class Entities final : public Plugin
{
public:
    static constexpr PluginInfo Info{
        "Docs Entities", "KeelS2 documentation", "1.0.0", "Read a caller pawn through a validated entity view"};

    bool Load() override
    {
        if (!FindSchemaField("CBaseEntity", "m_iHealth", health))
        {
            LogError("Health field: {}", LastError());
            return false;
        }

        return CreateCommand("keel_docs_health",
                             "Read your current health",
                             &Entities::Health,
                             FCVAR_GAMEDLL | FCVAR_CLIENT_CAN_EXECUTE);
    }

private:
    void Health(const CCommandContext& context, const CCommand&)
    {
        PlayerInfo player;
        Entity entity;

        if (!GetPlayer(context.GetPlayerSlot(), player) || !FindEntity(player.pawn, entity))
        {
            LogWarning("A current player pawn is required: {}", LastError());
            return;
        }

        int32 value{};

        if (!entity.Read(health, value))
        {
            LogWarning("Read failed: {}", entity.LastError());
            return;
        }

        Entity same;

        if (FindEntity(entity.Index(), same) && entity.Same(same))
            LogMessage("{}::{} = {} (offset {}, entity {})", health.ClassName(), health.FieldName(),
                value, health.Offset(), entity.Source2Handle());

        LogMessage("Schema module {} profile {}", health.ModuleName(), health.CompatibilityProfile());
        Entity moved = std::move(entity);

        if (moved.Valid())
            LogMessage("The moved view is still valid.");

        const KeelResult result = moved.Reset();

        if (result != KEEL_RESULT_OK)
            LogWarning("View release returned {}", result);
    }

    SchemaField<int32> health;
};
}

KEELS2_PLUGIN(docs::Entities)
