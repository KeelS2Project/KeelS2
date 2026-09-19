#ifndef KEELS2_CS2_ENTITY_KEYVALUES_H
#define KEELS2_CS2_ENTITY_KEYVALUES_H
#include <keels2/entity_keyvalues.h>
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
#define KEELS2_CS2_KEY_STRING KEELS2_ENTITY_KEY_STRING
#define KEELS2_CS2_KEY_BOOL KEELS2_ENTITY_KEY_BOOL
#define KEELS2_CS2_KEY_INT32 KEELS2_ENTITY_KEY_INT32
#define KEELS2_CS2_KEY_FLOAT KEELS2_ENTITY_KEY_FLOAT
#define KEELS2_CS2_KEY_VECTOR KEELS2_ENTITY_KEY_VECTOR
#define KEELS2_CS2_KEY_ANGLES KEELS2_ENTITY_KEY_ANGLES
#define KEELS2_CS2_KEY_COLOR KEELS2_ENTITY_KEY_COLOR
#define KEELS2_CS2_KEY_MAX_COUNT KEELS2_ENTITY_KEY_MAX_COUNT
#define KEELS2_CS2_KEY_MAX_NAME KEELS2_ENTITY_KEY_MAX_NAME
#define KEELS2_CS2_KEY_MAX_STRING KEELS2_ENTITY_KEY_MAX_STRING
#define KEELS2_CS2_KEY_MAX_BYTES KEELS2_ENTITY_KEY_MAX_BYTES

typedef KeelEntityKeyValue KeelCs2EntityKeyValue;

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
