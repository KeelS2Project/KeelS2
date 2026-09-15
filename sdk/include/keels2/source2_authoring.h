#ifndef KEELS2_SOURCE2_AUTHORING_H
#define KEELS2_SOURCE2_AUTHORING_H

#include <keels2/convar.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KEELS2_SOURCE2_AUTHORING_SERVICE_NAME "keels2.source2.authoring"
#define KEELS2_SOURCE2_AUTHORING_API_VERSION 1u

typedef void (*KeelSource2CommandCallback)(
    const void* context,
    const void* command,
    void* user_data);

typedef struct KeelSource2CommandSpec
{
    uint32_t size;
    uint32_t reserved;
    const char* name;
    const char* description;
    uint64_t flags;
    KeelSource2CommandCallback callback;
    void* user_data;
} KeelSource2CommandSpec;

typedef void (*KeelSource2ConVarChangeCallback)(
    void* convar,
    int32_t slot,
    const void* new_value,
    const void* old_value,
    void* user_data);

typedef struct KeelSource2AuthoringApi
{
    uint32_t size;
    uint32_t api_version;
    KeelResult (*register_command)(
        KeelPluginHandle plugin,
        const KeelSource2CommandSpec* spec,
        KeelCommandHandle* command);
    KeelResult (*unregister_command)(
        KeelPluginHandle plugin,
        KeelCommandHandle command);
    KeelResult (*create_convar)(
        KeelPluginHandle plugin,
        const KeelConVarSpec* spec,
        KeelSource2ConVarChangeCallback callback,
        void* user_data,
        KeelConVarHandle* convar,
        void** native_convar);
    KeelResult (*find_convar)(
        KeelPluginHandle plugin,
        const char* name,
        KeelConVarType expected_type,
        KeelConVarHandle* convar,
        void** native_convar);
    KeelResult (*release_convar)(
        KeelPluginHandle plugin,
        KeelConVarHandle convar);
} KeelSource2AuthoringApi;

#ifdef __cplusplus
}
#endif

#endif
