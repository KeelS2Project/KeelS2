#include <keels2/cs2/native_bridge.h>

#include <eiface.h>
#include <entity2/entityclass.h>
#include <networkbasetypes.pb.h>
#include <steam/steamclientpublic.h>

#include <algorithm>
#include <cstring>

namespace
{

template <typename Value>
bool ReadField(void* entities, void* schema, const char* module,
    const KeelCs2EntityIdentity& entity, const char* class_name, const char* field_name,
    KeelSchemaValueType type, Value& value)
{
    KeelCs2SchemaField field{};
    return KeelCs2_ResolveSchemaField(schema, module, class_name, field_name, type, &field) == KEEL_RESULT_OK &&
        KeelCs2_ReadEntityField(entities, &entity, &field, &value, sizeof(value)) == KEEL_RESULT_OK;
}

bool AuthenticatedIdentity(const CSteamID* identity)
{
    return identity && identity->IsValid() && identity->GetEUniverse() == k_EUniversePublic &&
        identity->GetEAccountType() == k_EAccountTypeIndividual &&
        identity->GetUnAccountInstance() == k_unSteamUserDefaultInstance && identity->GetAccountID() != 0;
}

}

extern "C" uint32_t KeelCs2_PlayerCapacity()
{
    return ABSOLUTE_PLAYER_LIMIT;
}

extern "C" KeelResult KeelCs2_ReadPlayer(void* engine_server, void* entity_system,
    void* schema_system, const char* module, int32_t slot, KeelPlayerInfo* player)
{
    if (!player)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    *player = {};
    player->size = sizeof(*player);
    player->slot = -1;
    player->user_id = -1;
    player->controller_handle = UINT32_MAX;
    player->pawn_handle = UINT32_MAX;
    if (!engine_server || slot < 0 || slot >= ABSOLUTE_PLAYER_LIMIT)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    try
    {
        auto* engine = static_cast<IVEngineServer2*>(engine_server);
        const CPlayerSlot native_slot(slot);
        CMsgPlayerInfo info;
        if (!engine->GetPlayerInfo(native_slot, info))
        {
            return KEEL_RESULT_NOT_FOUND;
        }
        KeelPlayerInfo current = *player;
        current.slot = slot;
        current.user_id = engine->GetPlayerUserId(native_slot).Get();
        current.flags = KEELS2_PLAYER_CONNECTED;
        if (current.user_id < 0)
        {
            current.flags |= KEELS2_PLAYER_CONNECTING;
        }
        if (info.fakeplayer())
        {
            current.flags |= KEELS2_PLAYER_BOT;
        }
        if (info.ishltv())
        {
            current.flags |= KEELS2_PLAYER_SOURCE_TV;
        }
        const auto name_length = std::min(info.name().size(), sizeof(current.name) - 1);
        std::memcpy(current.name, info.name().data(), name_length);
        current.name[name_length] = '\0';
        if (!info.fakeplayer() && !info.ishltv() && engine->IsClientFullyAuthenticated(native_slot))
        {
            const CSteamID* identity = engine->GetClientSteamID(native_slot);
            if (AuthenticatedIdentity(identity))
            {
                current.steam_id = identity->ConvertToUint64();
                current.flags |= KEELS2_PLAYER_AUTHENTICATED;
            }
        }

        KeelCs2EntityIdentity controller{};
        uint64 account{};
        const bool has_identity = info.fakeplayer() || (current.flags & KEELS2_PLAYER_AUTHENTICATED);
        if (!info.ishltv() && has_identity && entity_system && schema_system && module &&
            KeelCs2_FindEntityByIndex(entity_system, slot + 1, &controller) == KEEL_RESULT_OK &&
            ReadField(entity_system, schema_system, module, controller,
                "CBasePlayerController", "m_steamID", KEELS2_SCHEMA_UINT64, account) &&
            (info.fakeplayer() || account == current.steam_id))
        {
            current.controller_handle = controller.source2_handle;
            uint8 team{};
            if (ReadField(entity_system, schema_system, module, controller,
                "CBaseEntity", "m_iTeamNum", KEELS2_SCHEMA_UINT8, team))
            {
                current.team = team;
            }
            CEntityHandle pawn_handle;
            KeelCs2EntityIdentity pawn{};
            if (ReadField(entity_system, schema_system, module, controller,
                "CCSPlayerController", "m_hPlayerPawn", KEELS2_SCHEMA_ENTITY_HANDLE, pawn_handle) &&
                pawn_handle.IsValid() && KeelCs2_FindEntityBySource2Handle(
                    entity_system, static_cast<std::uint32_t>(pawn_handle.ToInt()), &pawn) == KEEL_RESULT_OK)
            {
                current.pawn_handle = pawn.source2_handle;
                uint8 life{};
                if (ReadField(entity_system, schema_system, module, pawn,
                    "CBaseEntity", "m_lifeState", KEELS2_SCHEMA_UINT8, life) && life == LIFE_ALIVE)
                {
                    current.flags |= KEELS2_PLAYER_ALIVE;
                }
            }
        }
        *player = current;
        return KEEL_RESULT_OK;
    }
    catch (...)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}
