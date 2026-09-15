#ifndef KEELS2_PLAYER_ACTIONS_H
#define KEELS2_PLAYER_ACTIONS_H

#include <keels2/entities.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KEELS2_PLAYER_ACTIONS_SERVICE_NAME "keels2.player_actions"
#define KEELS2_PLAYER_ACTIONS_API_VERSION 1u
#define KEELS2_PLAYER_ACTION_IMPULSE 1u
#define KEELS2_PLAYER_ACTION_KILL 2u

typedef struct KeelPlayerAction
{
    uint32_t size;
    uint32_t kind;
    float impulse[3];
    float damage;
} KeelPlayerAction;

typedef struct KeelPlayerActionsApi
{
    uint32_t size;
    uint32_t api_version;
    KeelResult (*apply)(KeelPluginHandle plugin, KeelEntityHandle pawn, const KeelPlayerAction* action);
} KeelPlayerActionsApi;

#ifdef __cplusplus
}
#endif

#endif
