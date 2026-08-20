#ifndef KEELS2_PLUGIN_H
#define KEELS2_PLUGIN_H

#include <stdint.h>

#if defined(_WIN32) && defined(KEELS2_PLUGIN_BUILD)
#define KEELS2_PLUGIN_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) && defined(KEELS2_PLUGIN_BUILD)
#define KEELS2_PLUGIN_EXPORT __attribute__((visibility("default")))
#else
#define KEELS2_PLUGIN_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define KEELS2_PLUGIN_ABI_VERSION 4u
#define KEELS2_PLUGIN_MANIFEST_VERSION 1u

typedef uint32_t KeelBool;
typedef uint32_t KeelResult;
typedef uint32_t KeelLogLevel;
typedef uint64_t KeelPluginHandle;
typedef uint64_t KeelCommandHandle;
typedef uint32_t KeelPluginDependencyRequirement;

#define KEELS2_PLUGIN_DEPENDENCY_EXACT 1u
#define KEELS2_PLUGIN_DEPENDENCY_AT_LEAST 2u

#define KEEL_FALSE 0u
#define KEEL_TRUE 1u

#define KEEL_RESULT_OK 0u
#define KEEL_RESULT_INVALID_ARGUMENT 1u
#define KEEL_RESULT_NOT_READY 2u
#define KEEL_RESULT_NOT_FOUND 3u
#define KEEL_RESULT_ALREADY_EXISTS 4u
#define KEEL_RESULT_ENGINE_FAILURE 5u
#define KEEL_RESULT_RESERVED_NAME 6u
#define KEEL_RESULT_INCOMPATIBLE 7u
#define KEEL_RESULT_UNSUPPORTED 8u
#define KEEL_RESULT_AMBIGUOUS 9u
#define KEEL_RESULT_BUSY 10u

#define KEEL_LOG_INFO 0u
#define KEEL_LOG_WARNING 1u
#define KEEL_LOG_ERROR 2u

typedef struct KeelHostQuery
{
    uint32_t size;
    uint32_t abi_version;
    const char* host_version;
    const char* game;
    const char* platform;
} KeelHostQuery;

typedef struct KeelPluginInfo
{
    uint32_t size;
    uint32_t abi_version;
    const char* name;
    const char* author;
    const char* version;
    const char* description;
} KeelPluginInfo;

typedef struct KeelPluginDependency
{
    uint32_t size;
    KeelPluginDependencyRequirement requirement;
    const char* name;
    const char* version;
} KeelPluginDependency;

typedef struct KeelPluginManifest
{
    uint32_t size;
    uint32_t manifest_version;
    uint32_t dependency_count;
    uint32_t reserved;
    const KeelPluginDependency* dependencies;
} KeelPluginManifest;

typedef struct KeelCommandInvocation
{
    uint32_t size;
    uint32_t argument_count;
    const char* name;
    const char* const* arguments;
} KeelCommandInvocation;

typedef void (*KeelCommandCallback)(const KeelCommandInvocation* invocation, void* user_data);

typedef struct KeelCommandSpec
{
    uint32_t size;
    const char* name;
    const char* description;
    uint64_t flags;
    KeelCommandCallback callback;
    void* user_data;
} KeelCommandSpec;

typedef struct KeelHostApi
{
    uint32_t size;
    uint32_t abi_version;
    void (*log)(KeelPluginHandle plugin, KeelLogLevel level, const char* message);
    KeelResult (*register_command)(KeelPluginHandle plugin, const KeelCommandSpec* spec, KeelCommandHandle* command);
    KeelResult (*unregister_command)(KeelPluginHandle plugin, KeelCommandHandle command);
    KeelResult (*query_service)(
        KeelPluginHandle plugin,
        const char* name,
        uint32_t version,
        const void** service);
} KeelHostApi;

typedef KeelBool (*KeelPluginQueryFn)(const KeelHostQuery* query, KeelPluginInfo* info);
typedef KeelBool (*KeelPluginManifestFn)(
    const KeelHostQuery* query,
    KeelPluginManifest* manifest);
typedef KeelBool (*KeelPluginLoadFn)(const KeelHostApi* api, KeelPluginHandle plugin);
typedef void (*KeelPluginUnloadFn)(KeelPluginHandle plugin);

KEELS2_PLUGIN_EXPORT KeelBool KeelPlugin_Query(const KeelHostQuery* query, KeelPluginInfo* info);
/* Optional: plugins without declared dependencies may omit this export. */
KEELS2_PLUGIN_EXPORT KeelBool KeelPlugin_Manifest(
    const KeelHostQuery* query,
    KeelPluginManifest* manifest);
KEELS2_PLUGIN_EXPORT KeelBool KeelPlugin_Load(const KeelHostApi* api, KeelPluginHandle plugin);
KEELS2_PLUGIN_EXPORT void KeelPlugin_Unload(KeelPluginHandle plugin);

#ifdef __cplusplus
}
#endif

#endif
