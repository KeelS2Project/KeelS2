#ifndef KEELS2_ENTITY_HOOK_DATA_H
#define KEELS2_ENTITY_HOOK_DATA_H
#include <keels2/entities.h>
#ifdef __cplusplus
extern "C" {
#endif
#define KEELS2_ENTITY_HOOK_DATA_SERVICE_NAME "keels2.entity_hook_data"
#define KEELS2_ENTITY_HOOK_DATA_API_VERSION 1u

typedef struct KeelDamageInfo
{
    uint32_t size;
    uint32_t reserved;
    float damage;
    uint32_t damage_type;
    int32_t damage_custom;
    uint32_t inflictor;
    uint32_t attacker;
    uint32_t ability;
    float force[3];
    float position[3];
} KeelDamageInfo;

typedef struct KeelDamageEdit
{
    uint32_t size;
    uint32_t reserved;
    float damage;
    uint32_t damage_type;
    float force[3];
    float position[3];
} KeelDamageEdit;

/* Native game-thread hook argument adapters, not a memory sandbox. The caller
 * must pass a live, readable complete damage record from a reviewed engine
 * callback and keep it valid for the operation. Writes also require writable
 * storage and a pre-original callback. Never retain these native pointers.
 * Missing game adapters/schema return an error without changing the record.
 * No native pointer is part of the returned snapshots. Source entity handles
 * can be absent/stale; they are references, not owned KeelEntities handles. */
typedef struct KeelEntityHookDataApi
{
    uint32_t size;
    uint32_t api_version;
    /* Initialize output.size. Failure clears values; absent references use
     * KEELS2_INVALID_SOURCE2_ENTITY_HANDLE. No ownership of native memory. */
    KeelResult (*read_damage)(KeelPluginHandle plugin, const void* record, KeelDamageInfo* output);
    /* Changes only the four named fields, after validating every field and
     * value. Damage must be finite and >=0; all vector components finite.
     * Reference fields, custom damage and owning native members are preserved.
     * This neither dispatches damage nor initializes a damage-result record. */
    KeelResult (*write_damage)(KeelPluginHandle plugin, void* record, const KeelDamageEdit* edit);
    /* Check that candidate is the owned pawn's current weapon-services
     * component and that the component's owner points back to this pawn.
     * Unknown candidate pointers are compared without dereferencing them.
     * Does not extend the component's lifetime across callbacks. */
    KeelResult (*weapon_matches)(KeelPluginHandle plugin, KeelEntityHandle pawn,
        const void* candidate, KeelBool* matches);
} KeelEntityHookDataApi;
#ifdef __cplusplus
}
#endif
#endif
