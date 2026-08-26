#include "plugin.h"

bool EntitiesPlugin::Load()
{
    if (!FindSchemaField("CBaseEntity", "m_iHealth", health_))
    {
        LogError("Could not resolve CBaseEntity::m_iHealth.");
        return false;
    }
    return CreateCommand(
        "keel_entity_health",
        "Reads the world entity health through a validated handle",
        &EntitiesPlugin::ReadWorldHealth);
}

void EntitiesPlugin::OnLevelShutdown()
{
    if (world_.Valid())
    {
        LogError("The world entity handle remained valid during level shutdown.");
    }
    world_.Reset();
}

void EntitiesPlugin::ReadWorldHealth(const CCommandContext&, const CCommand&)
{
    int32 health;
    if (!FindEntity(0, world_))
    {
        LogError("The world entity is not available.");
        return;
    }
    if (!world_.Read(health_, health))
    {
        LogError("Could not read CBaseEntity::m_iHealth.");
        return;
    }
    LogMessage("Read CBaseEntity::m_iHealth through a validated entity handle.");
}

KEELS2_PLUGIN(EntitiesPlugin)
