#ifndef KEELS2_PLUGINS_H
#define KEELS2_PLUGINS_H

#include <keels2/plugin.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KEELS2_PLUGINS_SERVICE_NAME "keels2.plugins"
#define KEELS2_PLUGINS_API_VERSION 1u

#define KEELS2_PLUGIN_NAME_CAPACITY 128u
#define KEELS2_PLUGIN_AUTHOR_CAPACITY 128u
#define KEELS2_PLUGIN_VERSION_CAPACITY 64u
#define KEELS2_PLUGIN_DESCRIPTION_CAPACITY 512u
#define KEELS2_PLUGIN_FILE_CAPACITY 260u
#define KEELS2_PLUGIN_DIAGNOSTIC_CAPACITY 512u

typedef uint32_t KeelPluginRuntimeState;
typedef uint32_t KeelPluginEventType;
typedef uint64_t KeelPluginSubscriptionHandle;

#define KEELS2_PLUGIN_STATE_UNKNOWN 0u
#define KEELS2_PLUGIN_STATE_LOADING 1u
#define KEELS2_PLUGIN_STATE_RUNNING 2u
#define KEELS2_PLUGIN_STATE_PAUSED 3u
#define KEELS2_PLUGIN_STATE_INVALID 4u
#define KEELS2_PLUGIN_STATE_ERROR 5u

#define KEELS2_PLUGIN_EVENT_LOADED 1u
#define KEELS2_PLUGIN_EVENT_UNLOADED 2u
#define KEELS2_PLUGIN_EVENT_PAUSED 3u
#define KEELS2_PLUGIN_EVENT_RESUMED 4u
#define KEELS2_PLUGIN_EVENT_ALL_LOADED 5u

typedef struct KeelPluginSnapshot
{
    uint32_t size;
    KeelPluginRuntimeState state;
    KeelPluginHandle handle;
    char name[KEELS2_PLUGIN_NAME_CAPACITY];
    char author[KEELS2_PLUGIN_AUTHOR_CAPACITY];
    char version[KEELS2_PLUGIN_VERSION_CAPACITY];
    char description[KEELS2_PLUGIN_DESCRIPTION_CAPACITY];
    char file[KEELS2_PLUGIN_FILE_CAPACITY];
    char diagnostic[KEELS2_PLUGIN_DIAGNOSTIC_CAPACITY];
} KeelPluginSnapshot;

typedef struct KeelPluginEvent
{
    uint32_t size;
    KeelPluginEventType type;
    uint64_t sequence;
    KeelPluginSnapshot plugin;
} KeelPluginEvent;

typedef void (*KeelPluginEventCallback)(const KeelPluginEvent* event, void* user_data);

typedef struct KeelPluginSubscriptionSpec
{
    uint32_t size;
    KeelPluginEventType event;
    uint64_t reserved;
    KeelPluginEventCallback callback;
    void* user_data;
} KeelPluginSubscriptionSpec;

typedef struct KeelPluginsApi
{
    uint32_t size;
    uint32_t api_version;
    KeelResult (*count)(KeelPluginHandle plugin, uint32_t* count);
    KeelResult (*at)(
        KeelPluginHandle plugin,
        uint32_t index,
        KeelPluginSnapshot* snapshot);
    KeelResult (*get)(
        KeelPluginHandle plugin,
        KeelPluginHandle target,
        KeelPluginSnapshot* snapshot);
    KeelResult (*find)(
        KeelPluginHandle plugin,
        const char* name,
        KeelPluginSnapshot* snapshot);
    KeelResult (*pause)(KeelPluginHandle plugin, KeelPluginHandle target);
    KeelResult (*resume)(KeelPluginHandle plugin, KeelPluginHandle target);
    KeelResult (*subscribe)(
        KeelPluginHandle plugin,
        const KeelPluginSubscriptionSpec* spec,
        KeelPluginSubscriptionHandle* subscription);
    KeelResult (*unsubscribe)(
        KeelPluginHandle plugin,
        KeelPluginSubscriptionHandle subscription);
} KeelPluginsApi;

#ifdef __cplusplus
}
#endif

#endif
