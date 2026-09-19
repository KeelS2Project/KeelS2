#ifndef KEELS2_CS2_ENTITY_KEYVALUES_H
#define KEELS2_CS2_ENTITY_KEYVALUES_H
#include <keels2/plugin.h>
#if defined(_WIN32)
#if defined(KEELS2_CS2_KEYVALUES_BUILD)
#define KEELS2_CS2_KEYVALUES_EXPORT __declspec(dllexport)
#else
#define KEELS2_CS2_KEYVALUES_EXPORT __declspec(dllimport)
#endif
#else
#define KEELS2_CS2_KEYVALUES_EXPORT __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define KEELS2_CS2_KEY_STRING 1u
#define KEELS2_CS2_KEY_BOOL 2u
#define KEELS2_CS2_KEY_INT32 3u
#define KEELS2_CS2_KEY_FLOAT 4u
#define KEELS2_CS2_KEY_VECTOR 5u
#define KEELS2_CS2_KEY_ANGLES 6u
#define KEELS2_CS2_KEY_COLOR 7u
#define KEELS2_CS2_KEY_MAX_COUNT 128u
#define KEELS2_CS2_KEY_MAX_NAME 127u
#define KEELS2_CS2_KEY_MAX_STRING 4095u
#define KEELS2_CS2_KEY_MAX_BYTES 32768u

typedef struct KeelCs2EntityKeyValue
{
    uint32_t size;
    uint32_t type;
    const char* name;
    const char* string_value;
    int32_t int_value;
    float float_value;
    float vector_value[3];
    uint8_t color_value[4];
} KeelCs2EntityKeyValue;

/* Internal main-thread bridge. Input strings/records are readable for this
 * call; values are copied into an engine-allocated CEntityKeyValues object.
 * Classname is fixed by the factory and cannot be overridden by a key.
 * Duplicate names and engine key-hash collisions are rejected. A build returns
 * one retained reference. Release that reference after DispatchSpawn returns;
 * engine-held references remain valid and may be released by the engine. */
KEELS2_CS2_KEYVALUES_EXPORT KeelResult KeelCs2KeyValues_Build(const char* class_name,
    const KeelCs2EntityKeyValue* values, uint32_t count, void** output);
KEELS2_CS2_KEYVALUES_EXPORT void KeelCs2KeyValues_Release(void* values);
#ifdef __cplusplus
}
#endif
#endif
