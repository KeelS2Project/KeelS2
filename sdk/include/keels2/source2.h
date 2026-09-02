#ifndef KEELS2_SOURCE2_H
#define KEELS2_SOURCE2_H

#include <keels2/plugin.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KEELS2_SOURCE2_SERVICE_NAME "keels2.source2"
#define KEELS2_SOURCE2_API_VERSION_1 1u
#define KEELS2_SOURCE2_API_VERSION 2u

typedef uint32_t KeelSource2Capability;
typedef uint32_t KeelSource2Factory;
typedef uint32_t KeelSource2Ownership;
typedef uint32_t KeelSource2Lifetime;

#define KEELS2_SOURCE2_CAPABILITY_NAMED 0u
#define KEELS2_SOURCE2_CAPABILITY_SERVER 1u
#define KEELS2_SOURCE2_CAPABILITY_GAME_CLIENTS 2u
#define KEELS2_SOURCE2_CAPABILITY_CVAR 3u

#define KEELS2_SOURCE2_FACTORY_ENGINE 1u
#define KEELS2_SOURCE2_FACTORY_SERVER 2u
#define KEELS2_SOURCE2_FACTORY_FILESYSTEM 3u
#define KEELS2_SOURCE2_FACTORY_PHYSICS 4u
#define KEELS2_SOURCE2_FACTORY_NETWORK 5u
#define KEELS2_SOURCE2_FACTORY_SERVER_SERVICE 6u

#define KEELS2_SOURCE2_OWNERSHIP_BORROWED 1u

#define KEELS2_SOURCE2_LIFETIME_HOST 1u

typedef struct KeelSource2InterfaceInfo
{
    uint32_t size;
    KeelSource2Capability capability;
    KeelSource2Factory factory;
    KeelSource2Ownership ownership;
    KeelSource2Lifetime lifetime;
    uint32_t reserved;
    void* instance;
    const char* interface_name;
    const char* module_name;
    const char* module_path;
    const char* compatibility_profile;
} KeelSource2InterfaceInfo;

typedef struct KeelSource2ApiV1
{
    uint32_t size;
    uint32_t api_version;
    KeelResult (*query_interface)(
        KeelPluginHandle plugin,
        KeelSource2Capability capability,
        KeelSource2InterfaceInfo* info);
} KeelSource2ApiV1;

typedef struct KeelSource2Api
{
    uint32_t size;
    uint32_t api_version;
    KeelResult (*query_interface)(
        KeelPluginHandle plugin,
        KeelSource2Capability capability,
        KeelSource2InterfaceInfo* info);
    KeelResult (*query_named_interface)(
        KeelPluginHandle plugin,
        KeelSource2Factory factory,
        const char* interface_name,
        KeelSource2InterfaceInfo* info);
} KeelSource2Api;

#ifdef __cplusplus
}
#endif

#endif
