#ifndef KEELS2_CS2_NATIVE_BRIDGE_H
#define KEELS2_CS2_NATIVE_BRIDGE_H

#include <keels2/plugin.h>
#include <keels2/schema.h>
#include <keels2/player_actions.h>
#include <keels2/player_management.h>
#include <keels2/entity_writes.h>
#include <keels2/entity_tools.h>
#include <keels2/entity_hook_data.h>
#include <keels2/round_control.h>
#include <keels2/player_statistics.h>
#include <keels2/players.h>
#include <keels2/player_input.h>

#include <stdint.h>

/* The MSVC ABI has one destructor entry; the Itanium ABI has two. */
#if defined(_WIN32)
#define KEELS2_CS2_NETWORK_STATE_CHANGED_SLOT 28
#else
#define KEELS2_CS2_NETWORK_STATE_CHANGED_SLOT 29
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*KeelCs2GameEventCallback)(void* event, const char* name, void* user_data);

typedef struct KeelCs2EntityConstructionBindings
{
    void* create;
    void* spawn;
    void* remove;
} KeelCs2EntityConstructionBindings;

typedef struct KeelCs2EntityInputBindings
{
    void* accept;
    void* queue;
} KeelCs2EntityInputBindings;

typedef struct KeelCs2EntityToolBindings
{
    void* set_model;
    void* remove;
    uint32_t teleport_slot;
    uint32_t reserved;
} KeelCs2EntityToolBindings;
typedef struct KeelCs2EntityToolClass
{
    void** vtable;
    void* teleport;
} KeelCs2EntityToolClass;
typedef struct KeelCs2EntityToolContext
{
    void* class_info;
    void* base_class;
    char class_name[256];
} KeelCs2EntityToolContext;

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

/* Internal main-thread operation after the caller reacquires its map epoch.
 * native_value is a live, immutable helper-built CVariant. All supplied entity
 * identities must be live; the entity-valued payload requires value_entity.
 * Queue rejects colors because the pinned game only shallow-copies that type.
 * Delay is finite/nonnegative, and zero for direct calls. The input name is
 * copied (1..127 non-control bytes). Invoked means entry into the engine call,
 * not acceptance of the input; it remains true if the engine throws. No entity
 * memory is read after invocation. The caller keeps native_value alive through
 * this call and releases it afterward, including failures. */
KeelResult KeelCs2_EntityInput(void* system, const KeelCs2EntityIdentity* entity,
    const KeelCs2EntityIdentity* activator, const KeelCs2EntityIdentity* caller,
    const KeelCs2EntityIdentity* value_entity, const KeelCs2EntityInputBindings* bindings,
    const char* input, const void* native_value, KeelBool queued, float delay, KeelBool* invoked);

/* Per-operation schema snapshots. Resolve before reacquiring the adapter map
 * epoch and entity system; never retain native argument pointers in these. */
typedef struct KeelCs2DamageSchema
{
    void* class_info;
    int32_t offsets[8];
} KeelCs2DamageSchema;
typedef struct KeelCs2WeaponSchema
{
    void* pawn_class;
    void* component_class;
    int32_t pointer_offset;
    int32_t chain_offset;
} KeelCs2WeaponSchema;
KeelResult KeelCs2_ResolveDamageSchema(void* schema_system, const char* module, KeelCs2DamageSchema* output);
KeelResult KeelCs2_ReadDamage(const KeelCs2DamageSchema* schema, const void* record, KeelDamageInfo* output);
KeelResult KeelCs2_WriteDamage(const KeelCs2DamageSchema* schema, void* record, const KeelDamageEdit* edit);
KeelResult KeelCs2_ResolveWeaponSchema(void* schema_system, const char* module, KeelCs2WeaponSchema* output);
KeelResult KeelCs2_WeaponMatches(void* entity_system, const KeelCs2EntityIdentity* pawn,
    const KeelCs2WeaponSchema* schema, const void* candidate, KeelBool* matches);

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
KeelResult KeelCs2_CaptureEntity(void* entity_system, const void* instance, KeelCs2EntityIdentity* output);
/* Internal construction primitives. The adapter must retain its map epoch and
 * reacquire the entity system after factory callbacks, before capturing the
 * returned address. The factory result is opaque and may already be dangling.
 * Capture scans registered identities without dereferencing that address.
 * Capture also permits an already-spawned factory result, so cancellation can
 * clean it up. Spawn separately requires a pre-spawn CBaseEntity. Cancellation
 * accepts any registered CEntityInstance, including unsupported factory types.
 * Only the construction owner may invoke these with a captured identity. */
KeelResult KeelCs2_CreateEntity(const KeelCs2EntityConstructionBindings* bindings,
    const char* class_name, void** instance);
KeelResult KeelCs2_CaptureCreatedEntity(void* system, const void* instance,
    KeelCs2EntityIdentity* output);
#define KEELS2_CS2_ENTITY_CAPACITY 32768u
KeelResult KeelCs2_SnapshotEntityHandles(void* system, uint32_t* handles, uint32_t count);
KeelResult KeelCs2_ValidatePendingEntity(void* system, const KeelCs2EntityIdentity* entity);
/* Reacquire the current epoch/system first. Cancel a still-pending spawn that
 * did not enter engine spawning. Never remove a live or spawning entity. */
KeelResult KeelCs2_FinishCreatedSpawn(void* system, const KeelCs2EntityIdentity* entity,
    const KeelCs2EntityConstructionBindings* bindings);
KeelResult KeelCs2_ValidateCreatedEntity(void* system, const KeelCs2EntityIdentity* entity,
    void* base_class, KeelBool require_pre_spawn);
/* Invoked becomes true immediately before entering the engine and remains
 * true if the engine throws. Callers must not retry a dispatched spawn or
 * cancellation. No borrowed entity is accessed after the engine call. */
KeelResult KeelCs2_SpawnCreatedEntity(void* system, const KeelCs2EntityIdentity* entity,
    void* base_class, const KeelCs2EntityConstructionBindings* bindings, const void* key_values, KeelBool* invoked);
KeelResult KeelCs2_RemoveCreatedEntity(void* system, const KeelCs2EntityIdentity* entity,
    const KeelCs2EntityConstructionBindings* bindings, KeelBool* invoked);
KeelResult KeelCs2_ResolveCreatedEntityPointer(void* system, const KeelCs2EntityIdentity* entity,
    const char* class_name, void** output);
KeelResult KeelCs2_PrepareCreatedEntityTool(void* system, const KeelCs2EntityIdentity* entity, void* base,
    KeelCs2EntityToolContext* context);
KeelResult KeelCs2_TeleportCreatedEntity(void* system, const KeelCs2EntityIdentity* entity,
    const KeelCs2EntityToolContext* context, const KeelCs2EntityToolBindings* bindings,
    const KeelCs2EntityToolClass* target, const KeelEntityTeleport* request);
KeelResult KeelCs2_ResolveEntityPointer(void* entity_system, const KeelCs2EntityIdentity* entity,
    const char* class_name, void** output);
KeelResult KeelCs2_ReadEntityField(
    void* entity_system,
    const KeelCs2EntityIdentity* entity,
    const KeelCs2SchemaField* field,
    void* value,
    uint32_t value_size);
KeelResult KeelCs2_ResolveEntityToolBase(void* schema_system, const char* module, uint32_t kind, void** output);
KeelResult KeelCs2_PrepareEntityTool(void* system, const KeelCs2EntityIdentity* entity, void* base,
    KeelCs2EntityToolContext* context);
KeelResult KeelCs2_ApplyEntityTool(void* system, const KeelCs2EntityIdentity* entity,
    const KeelCs2EntityToolContext* context, const KeelCs2EntityToolBindings* bindings,
    const KeelCs2EntityToolClass* target, uint32_t kind, const KeelEntityTeleport* request, const char* model);
KeelResult KeelCs2_ResolveEntityWriteClass(void* schema_system, const char* module, void** base_class);
KeelResult KeelCs2_WriteEntityField(void* entity_system, void* base_class,
    const KeelCs2EntityIdentity* entity, const KeelCs2SchemaField* field,
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
