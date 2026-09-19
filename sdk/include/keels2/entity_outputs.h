#ifndef KEELS2_ENTITY_OUTPUTS_H
#define KEELS2_ENTITY_OUTPUTS_H
#include <keels2/entities.h>
#include <keels2/entity_input.h>
#ifdef __cplusplus
extern "C" {
#endif
#define KEELS2_ENTITY_OUTPUTS_SERVICE_NAME "keels2.entity_outputs"
#define KEELS2_ENTITY_OUTPUTS_API_VERSION 1u
#define KEELS2_OUTPUT_PRE 1u
#define KEELS2_OUTPUT_POST 2u
#define KEELS2_OUTPUT_BOTH 3u
#define KEELS2_OUTPUT_ORIGINAL_CALLED 1u
#define KEELS2_OUTPUT_CONTINUE 0u
#define KEELS2_OUTPUT_BLOCK 1u
#define KEELS2_OUTPUT_MAX_DEPTH 8u

typedef uint64_t KeelEntityOutputHandle;
typedef struct KeelEntityOutputValue
{
    uint32_t size;
    uint32_t type; /* KEELS2_INPUT_*; UINT32_MAX when unavailable. */
    uint32_t native_type;
    int32_t int_value;
    float float_value;
    float vector_value[3];
    uint8_t color_value[4];
    uint32_t entity_handle;
    char string_value[4096];
} KeelEntityOutputValue;

/* Immutable observations captured before callbacks. Entity identities may be
 * stale by POST; optional identities have epoch=0/index=-1/handle=UINT32_MAX.
 * Values/names contain no engine pointers. value_status describes availability
 * of the typed value independently of the output notification. */
typedef struct KeelEntityOutputEvent
{
    uint32_t size;
    uint32_t phase;
    uint32_t flags;
    KeelResult value_status;
    KeelEntityInfo entity;
    KeelEntityInfo activator;
    KeelEntityInfo caller;
    float delay;
    uint32_t reserved;
    char class_name[256];
    char schema_name[256];
    char output_name[128];
    KeelEntityOutputValue value;
} KeelEntityOutputEvent;

typedef uint32_t (*KeelEntityOutputCallback)(const KeelEntityOutputEvent*, void* user_data);
typedef struct KeelEntityOutputSpec
{
    uint32_t size;
    uint32_t phases;
    int32_t priority;
    uint32_t reserved;
    KeelEntityHandle entity;
    const char* class_name;
    const char* output_name;
    KeelEntityOutputCallback callback;
    void* user_data;
} KeelEntityOutputSpec;

/* Main-thread owned registrations. Null/empty names match all; nonempty names
 * match exactly (case sensitive). entity=0 matches all owners. Otherwise the
 * owner's full identity/epoch is captured independently of the supplied handle.
 * Closing that entity handle does not unsubscribe. BLOCK is valid only in PRE.
 * POST reports whether the original ran, including blocked outputs. Callbacks
 * receive copied PRE values even if the original changes or destroys them.
 * Registrations are removed on plugin cleanup; entity filters expire with maps. */
typedef struct KeelEntityOutputsApi
{
    uint32_t size;
    uint32_t api_version;
    KeelResult (*ready)(KeelPluginHandle);
    KeelResult (*subscribe)(KeelPluginHandle, const KeelEntityOutputSpec*, KeelEntityOutputHandle*);
    KeelResult (*unsubscribe)(KeelPluginHandle, KeelEntityOutputHandle);
} KeelEntityOutputsApi;
#ifdef __cplusplus
}
#endif
#endif
