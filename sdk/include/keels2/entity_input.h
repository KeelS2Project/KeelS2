#ifndef KEELS2_ENTITY_INPUT_H
#define KEELS2_ENTITY_INPUT_H
#include <keels2/entities.h>
#ifdef __cplusplus
extern "C" {
#endif
#define KEELS2_ENTITY_INPUT_SERVICE_NAME "keels2.entity_input"
#define KEELS2_ENTITY_INPUT_API_VERSION 1u
#define KEELS2_INPUT_VOID 0u
#define KEELS2_INPUT_STRING 1u
#define KEELS2_INPUT_BOOL 2u
#define KEELS2_INPUT_INT32 3u
#define KEELS2_INPUT_FLOAT 4u
#define KEELS2_INPUT_VECTOR 5u
#define KEELS2_INPUT_ANGLES 6u
#define KEELS2_INPUT_COLOR 7u
#define KEELS2_INPUT_ENTITY 8u
#define KEELS2_INPUT_MAX_NAME 127u
#define KEELS2_INPUT_MAX_STRING 4095u
#define KEELS2_INPUT_MAX_DEPTH 8u

typedef struct KeelEntityInputValue
{
    uint32_t size;
    uint32_t type;
    const char* string_value;
    int32_t int_value;
    float float_value;
    float vector_value[3];
    uint8_t color_value[4];
} KeelEntityInputValue;
typedef struct KeelEntityInputRequest
{
    uint32_t size;
    KeelBool queued;
    const char* input;
    KeelEntityHandle activator;
    KeelEntityHandle caller;
    KeelEntityHandle value_entity;
    KeelEntityInputValue value;
    float delay;
} KeelEntityInputRequest;
typedef struct KeelEntityInputApi
{
    uint32_t size;
    uint32_t api_version;
    /* Main-thread only. Bit (1u << value_type) identifies a supported payload.
     * Outputs are zero on failure; availability does not validate input names. */
    KeelResult (*capabilities)(KeelPluginHandle plugin, uint32_t* direct_types, uint32_t* queued_types);
    /* Owned live target/participant handles must belong to this plugin and map.
     * Activator/caller zero means absent. value_entity is required only for an
     * entity payload and must be zero otherwise. Strings/name/selected values
     * are copied before callbacks; name is 1..127 non-control bytes, string is
     * 0..4095 bytes. Bool is 0/1, floats/vectors finite, angles are degrees.
     * Delay is finite/nonnegative; direct calls require zero delay. The engine
     * owns copies of queued events: closing handles or unloading the plugin
     * does not cancel accepted queue entries; map lifetime is engine-managed.
     * Queued colors are unsupported on the current CS2 profiles.
     * Invoked starts false and remains true after engine entry, even on error.
     * OK does not prove the input exists, succeeds, or preserves participants.
     * No Source 1 output ID semantics or rollback is provided. */
    KeelResult (*dispatch)(KeelPluginHandle plugin, KeelEntityHandle target,
        const KeelEntityInputRequest* request, KeelBool* invoked);
} KeelEntityInputApi;
#ifdef __cplusplus
}
#endif
#endif
