#ifndef KEELS2_SOURCE2_CALLBACKS_H
#define KEELS2_SOURCE2_CALLBACKS_H

#include <keels2/plugin.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KEELS2_SOURCE2_CALLBACKS_SERVICE_NAME "keels2.source2.callbacks"
#define KEELS2_SOURCE2_CALLBACKS_API_VERSION 1u
#define KEELS2_SOURCE2_REJECTION_CAPACITY 256u

typedef uint32_t KeelSource2CallbackType;
typedef uint64_t KeelSource2SubscriptionHandle;

#define KEELS2_SOURCE2_LEVEL_INIT 1u
#define KEELS2_SOURCE2_LEVEL_SHUTDOWN 2u
#define KEELS2_SOURCE2_GAME_EVENT 3u
#define KEELS2_SOURCE2_CLIENT_CONNECT 4u
#define KEELS2_SOURCE2_CLIENT_COMMAND 5u

typedef struct KeelSource2LevelInit
{
    uint32_t size;
    uint32_t reserved;
    const void* key_values;
    const void* prerequisite_registry;
} KeelSource2LevelInit;

typedef struct KeelSource2LevelShutdown
{
    uint32_t size;
    uint32_t reserved;
} KeelSource2LevelShutdown;

typedef struct KeelSource2GameEvent
{
    uint32_t size;
    uint32_t reserved;
    void* event;
    const char* name;
} KeelSource2GameEvent;

typedef struct KeelSource2ClientConnect
{
    uint32_t size;
    int32_t slot;
    uint64_t xuid;
    const char* name;
    const char* network_id;
    KeelBool unknown;
    uint32_t reserved;
    char* rejection_message;
    uint32_t rejection_capacity;
    uint32_t reserved_message;
} KeelSource2ClientConnect;

typedef struct KeelSource2ClientCommand
{
    uint32_t size;
    int32_t slot;
    const void* command;
} KeelSource2ClientCommand;

typedef struct KeelSource2CallbackEvent
{
    uint32_t size;
    KeelSource2CallbackType type;
    uint32_t payload_size;
    uint32_t reserved;
    const void* payload;
} KeelSource2CallbackEvent;

typedef KeelBool (*KeelSource2Callback)(
    const KeelSource2CallbackEvent* event,
    void* user_data);

typedef struct KeelSource2SubscriptionSpec
{
    uint32_t size;
    KeelSource2CallbackType type;
    int32_t priority;
    uint32_t reserved;
    const char* game_event;
    KeelSource2Callback callback;
    void* user_data;
} KeelSource2SubscriptionSpec;

typedef struct KeelSource2CallbacksApi
{
    uint32_t size;
    uint32_t api_version;
    KeelResult (*subscribe)(
        KeelPluginHandle plugin,
        const KeelSource2SubscriptionSpec* spec,
        KeelSource2SubscriptionHandle* subscription);
    KeelResult (*unsubscribe)(
        KeelPluginHandle plugin,
        KeelSource2SubscriptionHandle subscription);
} KeelSource2CallbacksApi;

#ifdef __cplusplus
}
#endif

#endif
