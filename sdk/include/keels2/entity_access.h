#ifndef KEELS2_ENTITY_ACCESS_H
#define KEELS2_ENTITY_ACCESS_H

#include <keels2/entities.h>

#ifdef __cplusplus
extern "C" {
#endif
#define KEELS2_ENTITY_ACCESS_SERVICE_NAME "keels2.entity_access"
#define KEELS2_ENTITY_ACCESS_API_VERSION 1u
#define KEELS2_ENTITY_ACCESS_MAX_COUNT 32u
#define KEELS2_ENTITY_ACCESS_MAX_DEPTH 8u

typedef struct KeelEntityAccessSpec
{
    uint32_t size;
    uint32_t reserved;
    KeelEntityHandle entity;
    const char* class_name;
} KeelEntityAccessSpec;

typedef KeelResult (*KeelEntityAccessCallback)(void* user_data, void* const* entities, uint32_t count);

typedef struct KeelEntityAccessApi
{
    uint32_t size;
    uint32_t api_version;
    /* Synchronous game-thread native access. All handles must belong to plugin;
     * class_name is the exact dynamic schema class, not a base-class cast.
     * The adapter validates all identities, map epochs and classes before one
     * callback, in request order. 1..32 entities and at most 8 nested visits.
     * Inputs are copied; the plugin remains retained through the callback.
     * Missing adapters return UNSUPPORTED without calling the callback.
     * Pointers are borrowed for this immediate operation only. Engine calls or
     * user callbacks may destroy entities; do not retain or reuse a pointer
     * after such a call. This does not pin entity memory or intercept deletion.
     * The callback must not throw across the C ABI. Its result is returned;
     * failure does not roll back callback or engine side effects. */
    KeelResult (*visit)(KeelPluginHandle plugin, const KeelEntityAccessSpec* entities,
        uint32_t count, KeelEntityAccessCallback callback, void* user_data);
} KeelEntityAccessApi;

#define KEELS2_ENTITY_CAPTURE_SERVICE_NAME "keels2.entity_capture"
#define KEELS2_ENTITY_CAPTURE_API_VERSION 1u

typedef struct KeelEntityCaptureApi
{
    uint32_t size;
    uint32_t api_version;
    /* Game-thread native bridge for an entity pointer from a reviewed engine
     * callback. The caller must supply a readable complete entity instance,
     * not an interior/base-adjusted pointer or a script-provided address.
     * The adapter checks the canonical entity registry, liveness and schema
     * alignment before capturing the current identity and map epoch.
     * Success returns an owned KeelEntities handle; release it with that API.
     * Failure clears entity. Missing adapters return UNSUPPORTED. A retained
     * handle detects deletion, serial reuse and map changes; it does not pin
     * engine memory. Do not use a stale native pointer to reacquire identity. */
    KeelResult (*capture)(KeelPluginHandle plugin, const void* instance, KeelEntityHandle* entity);
} KeelEntityCaptureApi;
#ifdef __cplusplus
}
#endif
#endif
