#ifndef KEELS2_ENTITIES_H
#define KEELS2_ENTITIES_H

#include <keels2/plugin.h>
#include <keels2/schema.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KEELS2_ENTITIES_SERVICE_NAME "keels2.entities"
#define KEELS2_ENTITIES_API_VERSION 1u

typedef uint64_t KeelEntityHandle;

#define KEELS2_INVALID_SOURCE2_ENTITY_HANDLE UINT32_MAX

typedef struct KeelEntityInfo
{
    uint32_t size;
    int32_t index;
    uint32_t source2_handle;
    uint32_t reserved;
    uint64_t epoch;
} KeelEntityInfo;

typedef struct KeelEntitiesApi
{
    uint32_t size;
    uint32_t api_version;
    KeelResult (*find_by_index)(
        KeelPluginHandle plugin,
        int32_t index,
        KeelEntityHandle* entity);
    KeelResult (*find_by_source2_handle)(
        KeelPluginHandle plugin,
        uint32_t source2_handle,
        KeelEntityHandle* entity);
    KeelResult (*release)(
        KeelPluginHandle plugin,
        KeelEntityHandle entity);
    KeelResult (*describe)(
        KeelPluginHandle plugin,
        KeelEntityHandle entity,
        KeelEntityInfo* info);
    KeelResult (*equal)(
        KeelPluginHandle plugin,
        KeelEntityHandle left,
        KeelEntityHandle right,
        KeelBool* equal);
    KeelResult (*read_field)(
        KeelPluginHandle plugin,
        KeelEntityHandle entity,
        KeelSchemaFieldHandle field,
        void* value,
        uint32_t value_size);
} KeelEntitiesApi;

#ifdef __cplusplus
}
#endif

#endif
