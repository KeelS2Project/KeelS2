#ifndef KEELS2_CS2_NATIVE_BRIDGE_H
#define KEELS2_CS2_NATIVE_BRIDGE_H

#include <keels2/plugin.h>
#include <keels2/schema.h>
#include <keels2/player_actions.h>
#include <keels2/player_management.h>
#include <keels2/entity_writes.h>
#include <keels2/round_control.h>
#include <keels2/player_statistics.h>
#include <keels2/players.h>
#include <keels2/player_input.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*KeelCs2GameEventCallback)(void* event, const char* name, void* user_data);

typedef struct KeelCs2SchemaField
{
    void* declaring_class;
    int32_t offset;
    uint32_t value_size;
    uint32_t value_alignment;
    KeelSchemaValueType value_type;
} KeelCs2SchemaField;

typedef struct KeelCs2EntityIdentity
{
    int32_t index;
    uint32_t source2_handle;
} KeelCs2EntityIdentity;

typedef struct KeelCs2PlayerActionBindings
{
    uint32_t teleport_slot;
    uint32_t suicide_slot;
    void* damage_construct;
    void* damage_apply;
    void* damage_destroy;
    uint32_t damage_info_size;
} KeelCs2PlayerActionBindings;

typedef struct KeelCs2PlayerManagementBindings
{
    void** controller_vtable;
    void* change_team;
    void* switch_team;
    void* respawn;
    void* set_pawn;
} KeelCs2PlayerManagementBindings;

typedef struct KeelCs2RoundBindings
{
    void** rules_vtable;
    void** proxy_vtable;
    void* terminate;
} KeelCs2RoundBindings;

typedef struct KeelCs2PlayerStatisticsBindings
{
    void** controller_vtable;
    void** money_vtable;
    void** tracking_vtable;
    void* notify;
} KeelCs2PlayerStatisticsBindings;

/* Private, per-call metadata. Resolve before acquiring the current entity list;
 * never retain a component pointer across schema calls or map transitions. */
typedef struct KeelCs2PlayerStatSchema
{
    void* controller_class;
    void* component_class;
    int32_t pointer_offset;
    int32_t chain_offset;
    int32_t value_offset;
    uint32_t key;
} KeelCs2PlayerStatSchema;

KeelResult KeelCs2_ResolvePlayerStatSchema(void* schema_system, const char* module, uint32_t key,
    KeelCs2PlayerStatSchema* schema);
KeelResult KeelCs2_ReadPlayerStat(void* entity_system, const KeelCs2EntityIdentity* controller,
    const KeelCs2PlayerStatSchema* schema, const KeelCs2PlayerStatisticsBindings* bindings, int32_t* value);
KeelResult KeelCs2_WritePlayerStat(void* entity_system, const KeelCs2EntityIdentity* controller,
    const KeelCs2PlayerStatSchema* schema, const KeelCs2PlayerStatisticsBindings* bindings, int32_t value);

/* Private bridge context, never exposed to plugins/scripts. Preparation may
 * call schema interfaces; revalidate the adapter map epoch before dispatch. */
typedef struct KeelCs2RoundContext
{
    KeelCs2EntityIdentity proxy;
    void* proxy_instance;
    void* rules;
    void* proxy_class;
    void* rules_class;
    int32_t pointer_offset;
} KeelCs2RoundContext;

KeelResult KeelCs2_ResolveRoundSchema(void* schema_system, const char* module, KeelCs2RoundContext* context);
KeelResult KeelCs2_FindRoundContext(void* entity_system,
    const KeelCs2RoundBindings* bindings, KeelCs2RoundContext* context);
KeelResult KeelCs2_TerminateRound(void* entity_system, const KeelCs2RoundContext* context,
    const KeelRoundTermination* request, const KeelCs2RoundBindings* bindings);

/* Respawn preparation may call into the engine. The adapter must revalidate
 * its map epoch and entity system before calling ManagePlayer afterwards. */
KeelResult KeelCs2_PrepareRespawn(void* entity_system, void* schema_system, const char* module,
    const KeelCs2EntityIdentity* controller, const KeelCs2PlayerManagementBindings* bindings,
    KeelCs2EntityIdentity* prepared_pawn);
KeelResult KeelCs2_ManagePlayer(void* entity_system, void* schema_system, const char* module,
    const KeelCs2EntityIdentity* controller, const KeelCs2EntityIdentity* prepared_pawn,
    const KeelPlayerManagementAction* action,
    const KeelCs2PlayerManagementBindings* bindings);

KeelResult KeelCs2_PlayerAction(void* entity_system, void* schema_system, const char* module,
    const KeelCs2EntityIdentity* entity, const KeelPlayerAction* action,
    const KeelCs2PlayerActionBindings* bindings);

void* KeelCs2_CreateGameEventListener(
    void* manager,
    KeelCs2GameEventCallback callback,
    void* user_data);
void KeelCs2_DestroyGameEventListener(void* listener);
uint32_t KeelCs2_ListenForGameEvent(void* listener, const char* name);
uint32_t KeelCs2_WriteRejectionMessage(void* buffer, const char* message, uint32_t length);
KeelResult KeelCs2_ResolveSchemaField(
    void* schema_system,
    const char* module_name,
    const char* class_name,
    const char* field_name,
    KeelSchemaValueType value_type,
    KeelCs2SchemaField* field);
void* KeelCs2_ReadGameEntitySystem(void* game_resource_service, uint32_t offset);
KeelResult KeelCs2_FindEntityByIndex(
    void* entity_system,
    int32_t index,
    KeelCs2EntityIdentity* entity);
KeelResult KeelCs2_FindEntityBySource2Handle(
    void* entity_system,
    uint32_t source2_handle,
    KeelCs2EntityIdentity* entity);
KeelResult KeelCs2_ValidateEntity(
    void* entity_system,
    const KeelCs2EntityIdentity* entity);
KeelResult KeelCs2_ResolveEntityPointer(void* entity_system, const KeelCs2EntityIdentity* entity,
    const char* class_name, void** output);
KeelResult KeelCs2_ReadEntityField(
    void* entity_system,
    const KeelCs2EntityIdentity* entity,
    const KeelCs2SchemaField* field,
    void* value,
    uint32_t value_size);
KeelResult KeelCs2_WriteEntityField(void* entity_system, void* schema_system,
    const char* module, const KeelCs2EntityIdentity* entity, const KeelCs2SchemaField* field,
    const void* value, uint32_t value_size, void* notify);
KeelResult KeelCs2_CommandCaller(const void* context, int32_t* slot);
KeelResult KeelCs2_ServerCommand(void* engine_server, const char* command);
KeelResult KeelCs2_ClientConsolePrint(
    void* engine_server,
    int32_t slot,
    const char* message);
KeelResult KeelCs2_FindUserMessage(
    void* network_messages,
    const char* name,
    uint32_t* message_id);

KeelResult KeelCs2_PrintChat(void* engine_server, void* network_messages,
    void* game_events, int32_t slot, KeelBool broadcast, const char* text);

KeelResult KeelCs2_ReadControllerInput(void* entity_system, void* schema_system, const char* module,
    const KeelCs2EntityIdentity* controller, uint64_t* buttons, void** component, uint32_t* pawn);

KeelResult KeelCs2_ReadPlayerButtons(void* entity_system, void* schema_system, const char* module,
    const KeelCs2EntityIdentity* pawn, uint64_t* buttons, void** component);

uint32_t KeelCs2_PlayerCapacity(void);
KeelResult KeelCs2_ReadPlayer(void* engine_server, void* entity_system,
    void* schema_system, const char* module, int32_t slot, KeelPlayerInfo* player);

#ifdef __cplusplus
}
#endif

#endif
