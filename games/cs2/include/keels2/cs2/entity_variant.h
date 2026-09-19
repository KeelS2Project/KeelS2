#ifndef KEELS2_CS2_ENTITY_VARIANT_H
#define KEELS2_CS2_ENTITY_VARIANT_H
#include <keels2/cs2/entity_keyvalues.h>
#ifdef __cplusplus
extern "C" {
#endif
#define KEELS2_CS2_VARIANT_VOID 0u
#define KEELS2_CS2_VARIANT_STRING 1u
#define KEELS2_CS2_VARIANT_BOOL 2u
#define KEELS2_CS2_VARIANT_INT32 3u
#define KEELS2_CS2_VARIANT_FLOAT 4u
#define KEELS2_CS2_VARIANT_VECTOR 5u
#define KEELS2_CS2_VARIANT_ANGLES 6u
#define KEELS2_CS2_VARIANT_COLOR 7u
#define KEELS2_CS2_VARIANT_ENTITY 8u
#define KEELS2_CS2_VARIANT_MAX_STRING 4095u

typedef struct KeelCs2VariantValue
{
    uint32_t size;
    uint32_t type;
    const char* string_value;
    int32_t int_value;
    float float_value;
    float vector_value[3];
    uint8_t color_value[4];
    uint32_t entity_handle;
} KeelCs2VariantValue;

/* Internal main-thread bridge. Build copies the selected payload into a pinned
 * SDK CVariant using the engine allocator. It does not resolve entity handles;
 * the calling operation must validate their complete identity and epoch.
 * Output is borrowed by synchronous engine calls and released once on return.
 * A retaining consumer must make its own deep copy, never a shallow C++ copy.
 * Failure clears output; release accepts null. No script pointers cross this
 * boundary. The engine allocator must remain available until release. */
KEELS2_CS2_KEYVALUES_EXPORT KeelResult KeelCs2Variant_Build(const KeelCs2VariantValue* value, void** output);
KEELS2_CS2_KEYVALUES_EXPORT void KeelCs2Variant_Release(void* value);
#ifdef __cplusplus
}
#endif
#endif
