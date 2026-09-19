#ifndef KEELS2_ENTITY_CONSTRUCTION_H
#define KEELS2_ENTITY_CONSTRUCTION_H
#include <keels2/entity_access.h>
#include <keels2/entity_tools.h>
#include <keels2/entity_keyvalues.h>
#ifdef __cplusplus
extern "C" {
#endif
#define KEELS2_ENTITY_CONSTRUCTION_SERVICE_NAME "keels2.entity_construction"
#define KEELS2_ENTITY_CONSTRUCTION_API_VERSION 1u
#define KEELS2_ENTITY_CONSTRUCTION_MAX_PENDING 64u
#define KEELS2_ENTITY_CONSTRUCTION_MAX_OBSERVERS 256u
#define KEELS2_ENTITY_CONSTRUCTION_MAX_DEPTH 8u

typedef struct KeelEntityConstructionApi
{
    uint32_t size;
    uint32_t api_version;
    /* Calls run on the game thread and may enter game callbacks. A game may
     * impose an additional shared capacity limit. Ordinary entity lookup does
     * not discover pending entities. Outputs are cleared on failure. */
    KeelResult (*ready)(KeelPluginHandle plugin);
    /* Allocates a real pending entity. Release its handle through entities.release
     * on the game thread to cancel it. Releasing a spawned entity only drops the
     * handle. Classname is ASCII letters/digits/underscore, 1..127 bytes. */
    KeelResult (*create)(KeelPluginHandle plugin, const char* class_name, KeelEntityHandle* entity);
    /* Describes a still-pending owner or observer handle, including during a
     * spawn callback. Ordinary entities.describe applies after spawning. */
    KeelResult (*describe)(KeelPluginHandle plugin, KeelEntityHandle entity, KeelEntityInfo* info);
    /* Owner-only: staged keys are copied and applied at spawn. Same-name keys
     * replace earlier values case-insensitively. Selected numeric values must
     * be finite; booleans are 0/1. The declared key/string bounds apply. */
    KeelResult (*set)(KeelPluginHandle plugin, KeelEntityHandle entity, const KeelEntityKeyValue* value);

    KeelResult (*teleport)(KeelPluginHandle plugin, KeelEntityHandle entity, const KeelEntityTeleport* request);
    /* Owner-only. Invoked consumes construction even on failure; never retry
     * an invoked spawn. A hook may block spawn, causing pending cancellation. */
    KeelResult (*spawn)(KeelPluginHandle plugin, KeelEntityHandle entity, KeelBool* invoked);
    /* Creates an independent observation lease for a tracked pending creation.
     * Closing it never cancels the entity. Pending owner-close invalidates all
     * observers. Surviving observers use ordinary entity access after spawn. */
    KeelResult (*observe)(KeelPluginHandle plugin, uint32_t source2_handle, KeelEntityHandle* entity);
    /* Exact schema class; read-only borrowed pointer, valid only in callback.
     * May nest during spawn. Do not mutate or retain the pointer. Closing
     * handles from the callback is permitted; cleanup waits for active access. */
    KeelResult (*visit)(KeelPluginHandle plugin, KeelEntityHandle entity, const char* class_name,
        KeelEntityAccessCallback callback, void* user_data);
} KeelEntityConstructionApi;
#ifdef __cplusplus
}
#endif
#endif
