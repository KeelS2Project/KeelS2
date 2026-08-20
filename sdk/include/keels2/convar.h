#ifndef KEELS2_CONVAR_H
#define KEELS2_CONVAR_H

#include <keels2/plugin.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KEELS2_CONVAR_SERVICE_NAME "keels2.convar"
#define KEELS2_CONVAR_API_VERSION 1u

typedef uint32_t KeelConVarType;
typedef uint64_t KeelConVarHandle;

#define KEELS2_CONVAR_BOOL 1u
#define KEELS2_CONVAR_INT32 2u
#define KEELS2_CONVAR_FLOAT32 3u
#define KEELS2_CONVAR_STRING 4u

#define KEELS2_CONVAR_GLOBAL_SLOT (-1)

#define KEELS2_CVAR_FLAG_PROTECTED (1ull << 5)
#define KEELS2_CVAR_FLAG_SINGLE_PLAYER_ONLY (1ull << 6)
#define KEELS2_CVAR_FLAG_ARCHIVE (1ull << 7)
#define KEELS2_CVAR_FLAG_NOTIFY (1ull << 8)
#define KEELS2_CVAR_FLAG_UNLOGGED (1ull << 11)
#define KEELS2_CVAR_FLAG_REPLICATED (1ull << 13)
#define KEELS2_CVAR_FLAG_CHEAT (1ull << 14)
#define KEELS2_CVAR_FLAG_RELEASE (1ull << 19)
#define KEELS2_CVAR_FLAG_NOT_CONNECTED (1ull << 22)
#define KEELS2_CVAR_FLAG_SERVER_CANNOT_QUERY (1ull << 26)

typedef union KeelConVarScalar
{
    KeelBool boolean_value;
    int32_t int32_value;
    float float32_value;
    const char* string_value;
    uint64_t reserved;
} KeelConVarScalar;

typedef struct KeelConVarValue
{
    uint32_t size;
    KeelConVarType type;
    KeelConVarScalar value;
} KeelConVarValue;

typedef struct KeelConVarChange
{
    uint32_t size;
    int32_t slot;
    KeelConVarHandle convar;
    const char* name;
    KeelConVarValue old_value;
    KeelConVarValue new_value;
} KeelConVarChange;

typedef void (*KeelConVarChangeCallback)(const KeelConVarChange* change, void* user_data);

typedef struct KeelConVarSpec
{
    uint32_t size;
    KeelConVarType type;
    const char* name;
    const char* description;
    uint64_t flags;
    KeelConVarValue default_value;
    KeelBool has_minimum;
    uint32_t reserved_minimum;
    KeelConVarValue minimum_value;
    KeelBool has_maximum;
    uint32_t reserved_maximum;
    KeelConVarValue maximum_value;
    KeelConVarChangeCallback callback;
    void* user_data;
} KeelConVarSpec;

typedef struct KeelConVarInfo
{
    uint32_t size;
    KeelConVarType type;
    const char* name;
    const char* description;
    uint64_t flags;
    KeelConVarValue default_value;
    KeelBool has_minimum;
    uint32_t reserved_minimum;
    KeelConVarValue minimum_value;
    KeelBool has_maximum;
    uint32_t reserved_maximum;
    KeelConVarValue maximum_value;
} KeelConVarInfo;

typedef struct KeelConVarApi
{
    uint32_t size;
    uint32_t api_version;
    KeelResult (*create)(
        KeelPluginHandle plugin,
        const KeelConVarSpec* spec,
        KeelConVarHandle* convar);
    KeelResult (*find)(
        KeelPluginHandle plugin,
        const char* name,
        KeelConVarType expected_type,
        KeelConVarHandle* convar);
    KeelResult (*release)(KeelPluginHandle plugin, KeelConVarHandle convar);
    KeelResult (*read)(
        KeelPluginHandle plugin,
        KeelConVarHandle convar,
        int32_t slot,
        KeelConVarValue* value);
    /* API v1 name retained: applies normally and defers only during Source 2 callbacks. */
    KeelResult (*queue_set)(
        KeelPluginHandle plugin,
        KeelConVarHandle convar,
        int32_t slot,
        const KeelConVarValue* value);
    KeelResult (*describe)(
        KeelPluginHandle plugin,
        KeelConVarHandle convar,
        KeelConVarInfo* info);
} KeelConVarApi;

#ifdef __cplusplus
}
#endif

#endif
