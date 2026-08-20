#ifndef KEELS2_LIFECYCLE_H
#define KEELS2_LIFECYCLE_H

#include <keels2/plugin.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KEELS2_LIFECYCLE_SERVICE_NAME "keels2.lifecycle"
#define KEELS2_LIFECYCLE_API_VERSION 1u

typedef uint32_t KeelLifecycleEventType;
typedef uint64_t KeelLifecycleSubscriptionHandle;

#define KEELS2_LIFECYCLE_GAME_FRAME 1u
#define KEELS2_LIFECYCLE_CLIENT_CONNECTED 2u
#define KEELS2_LIFECYCLE_CLIENT_PUT_IN_SERVER 3u
#define KEELS2_LIFECYCLE_CLIENT_ACTIVE 4u
#define KEELS2_LIFECYCLE_CLIENT_FULLY_CONNECTED 5u
#define KEELS2_LIFECYCLE_CLIENT_DISCONNECTING 6u
#define KEELS2_LIFECYCLE_CLIENT_SETTINGS_CHANGED 7u

typedef struct KeelLifecycleGameFrame
{
    uint32_t size;
    KeelBool simulating;
    KeelBool first_tick;
    KeelBool last_tick;
} KeelLifecycleGameFrame;

typedef struct KeelLifecycleClientConnected
{
    uint32_t size;
    int32_t slot;
    uint64_t xuid;
    const char* name;
    const char* network_id;
    const char* address;
    KeelBool fake_player;
    uint32_t reserved;
} KeelLifecycleClientConnected;

typedef struct KeelLifecycleClientPutInServer
{
    uint32_t size;
    int32_t slot;
    uint64_t xuid;
    const char* name;
    int32_t client_type;
    uint32_t reserved;
} KeelLifecycleClientPutInServer;

typedef struct KeelLifecycleClientActive
{
    uint32_t size;
    int32_t slot;
    uint64_t xuid;
    const char* name;
    KeelBool load_game;
    uint32_t reserved;
} KeelLifecycleClientActive;

typedef struct KeelLifecycleClientFullyConnected
{
    uint32_t size;
    int32_t slot;
} KeelLifecycleClientFullyConnected;

typedef struct KeelLifecycleClientDisconnecting
{
    uint32_t size;
    int32_t slot;
    uint64_t xuid;
    const char* name;
    const char* network_id;
    int32_t reason;
    uint32_t reserved;
} KeelLifecycleClientDisconnecting;

typedef struct KeelLifecycleClientSettingsChanged
{
    uint32_t size;
    int32_t slot;
} KeelLifecycleClientSettingsChanged;

typedef struct KeelLifecycleEvent
{
    uint32_t size;
    KeelLifecycleEventType type;
    uint32_t payload_size;
    uint32_t reserved;
    const void* payload;
} KeelLifecycleEvent;

typedef void (*KeelLifecycleCallback)(const KeelLifecycleEvent* event, void* user_data);

typedef struct KeelLifecycleSubscriptionSpec
{
    uint32_t size;
    KeelLifecycleEventType event;
    uint32_t reserved;
    KeelLifecycleCallback callback;
    void* user_data;
} KeelLifecycleSubscriptionSpec;

typedef struct KeelLifecycleApi
{
    uint32_t size;
    uint32_t api_version;
    KeelResult (*subscribe)(
        KeelPluginHandle plugin,
        const KeelLifecycleSubscriptionSpec* spec,
        KeelLifecycleSubscriptionHandle* subscription);
    KeelResult (*unsubscribe)(
        KeelPluginHandle plugin,
        KeelLifecycleSubscriptionHandle subscription);
} KeelLifecycleApi;

#ifdef __cplusplus
}
#endif

#endif
