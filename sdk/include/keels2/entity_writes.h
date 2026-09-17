#ifndef KEELS2_ENTITY_WRITES_H
#define KEELS2_ENTITY_WRITES_H
#include <keels2/entities.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KEELS2_ENTITY_WRITES_SERVICE_NAME "keels2.entity_writes"
#define KEELS2_ENTITY_WRITES_API_VERSION 1u
#define KEELS2_ENTITY_WRITE_NUMERIC_FIELDS 0x01u

typedef struct KeelEntityWritesApi
{
    uint32_t size;
    uint32_t api_version;
    /* Game-thread only. Output is zero on failure. Binary capabilities do not
     * guarantee a particular entity/field is writable. */
    KeelResult (*capabilities)(KeelPluginHandle plugin, uint32_t* capabilities);
    /* Both handles must belong to this plugin. Supports scalar integer/bool,
     * finite float and finite Vector3 fields. Pointers/handles/arrays/strings
     * are excluded. Values use the field's native width and signedness.
     * A changed value is written before the entity's network state is marked
     * dirty. This does not invoke game-specific setters or change callbacks.
     * Failure during notification can occur after the value was written; no
     * rollback is attempted across engine callbacks. */
    KeelResult (*write_field)(KeelPluginHandle plugin, KeelEntityHandle entity,
        KeelSchemaFieldHandle field, const void* value, uint32_t value_size);
} KeelEntityWritesApi;

#ifdef __cplusplus
}
#endif
#endif
