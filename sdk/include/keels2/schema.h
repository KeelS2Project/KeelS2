#ifndef KEELS2_SCHEMA_H
#define KEELS2_SCHEMA_H

#include <keels2/plugin.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KEELS2_SCHEMA_SERVICE_NAME "keels2.schema"
#define KEELS2_SCHEMA_API_VERSION 1u

typedef uint64_t KeelSchemaFieldHandle;
typedef uint32_t KeelSchemaModule;
typedef uint32_t KeelSchemaValueType;

#define KEELS2_SCHEMA_MODULE_SERVER 1u

#define KEELS2_SCHEMA_CHAR 1u
#define KEELS2_SCHEMA_INT8 2u
#define KEELS2_SCHEMA_UINT8 3u
#define KEELS2_SCHEMA_INT16 4u
#define KEELS2_SCHEMA_UINT16 5u
#define KEELS2_SCHEMA_INT32 6u
#define KEELS2_SCHEMA_UINT32 7u
#define KEELS2_SCHEMA_INT64 8u
#define KEELS2_SCHEMA_UINT64 9u
#define KEELS2_SCHEMA_FLOAT32 10u
#define KEELS2_SCHEMA_FLOAT64 11u
#define KEELS2_SCHEMA_BOOL 12u

typedef struct KeelSchemaFieldSpec
{
    uint32_t size;
    KeelSchemaModule module;
    KeelSchemaValueType value_type;
    uint32_t reserved;
    const char* class_name;
    const char* field_name;
} KeelSchemaFieldSpec;

typedef struct KeelSchemaFieldInfo
{
    uint32_t size;
    KeelSchemaModule module;
    KeelSchemaValueType value_type;
    uint32_t value_size;
    uint32_t value_alignment;
    int32_t offset;
    uint32_t reserved;
    const char* class_name;
    const char* field_name;
    const char* module_name;
    const char* compatibility_profile;
} KeelSchemaFieldInfo;

typedef struct KeelSchemaApi
{
    uint32_t size;
    uint32_t api_version;
    KeelResult (*resolve_field)(
        KeelPluginHandle plugin,
        const KeelSchemaFieldSpec* spec,
        KeelSchemaFieldHandle* field);
    KeelResult (*release_field)(
        KeelPluginHandle plugin,
        KeelSchemaFieldHandle field);
    KeelResult (*describe_field)(
        KeelPluginHandle plugin,
        KeelSchemaFieldHandle field,
        KeelSchemaFieldInfo* info);
} KeelSchemaApi;

#ifdef __cplusplus
}
#endif

#endif
