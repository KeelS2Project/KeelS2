#ifndef KEELS2_ENTITY_KEYVALUES_H
#define KEELS2_ENTITY_KEYVALUES_H
#include <keels2/plugin.h>
#ifdef __cplusplus
extern "C" {
#endif
#define KEELS2_ENTITY_KEY_STRING 1u
#define KEELS2_ENTITY_KEY_BOOL 2u
#define KEELS2_ENTITY_KEY_INT32 3u
#define KEELS2_ENTITY_KEY_FLOAT 4u
#define KEELS2_ENTITY_KEY_VECTOR 5u
#define KEELS2_ENTITY_KEY_ANGLES 6u
#define KEELS2_ENTITY_KEY_COLOR 7u
#define KEELS2_ENTITY_KEY_MAX_COUNT 128u
#define KEELS2_ENTITY_KEY_MAX_NAME 127u
#define KEELS2_ENTITY_KEY_MAX_STRING 4095u
#define KEELS2_ENTITY_KEY_MAX_BYTES 32768u

typedef struct KeelEntityKeyValue
{
    /* Selects one payload: strings, bool/int32, float, vector/angles, or RGBA.
     * Strings and names are copied by construction setters. Classname is fixed
     * at creation; key names cannot replace it or collide with its engine hash. */
    uint32_t size;
    uint32_t type;
    const char* name;
    const char* string_value;
    int32_t int_value;
    float float_value;
    float vector_value[3];
    uint8_t color_value[4];
} KeelEntityKeyValue;

#ifdef __cplusplus
}
#endif
#endif
