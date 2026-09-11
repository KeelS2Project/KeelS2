#ifndef KEELS2_PLAYERS_H
#define KEELS2_PLAYERS_H

#include <keels2/plugin.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KEELS2_PLAYERS_SERVICE_NAME "keels2.players"
#define KEELS2_PLAYERS_API_VERSION 1u
#define KEELS2_PLAYER_NAME_CAPACITY 129u

#define KEELS2_PLAYER_CONNECTED 0x01u
#define KEELS2_PLAYER_CONNECTING 0x02u
#define KEELS2_PLAYER_AUTHENTICATED 0x04u
#define KEELS2_PLAYER_BOT 0x08u
#define KEELS2_PLAYER_SOURCE_TV 0x10u
#define KEELS2_PLAYER_ALIVE 0x20u

typedef struct KeelPlayerConnection
{
    int32_t slot;
    uint32_t reserved;
    uint64_t generation;
} KeelPlayerConnection;

typedef struct KeelPlayerInfo
{
    uint32_t size;
    int32_t slot;
    int32_t user_id;
    uint32_t flags;
    uint64_t connection;
    uint64_t steam_id;
    uint32_t controller_handle;
    uint32_t pawn_handle;
    int32_t team;
    uint32_t reserved;
    char name[KEELS2_PLAYER_NAME_CAPACITY];
} KeelPlayerInfo;

typedef struct KeelPlayersApi
{
    uint32_t size;
    uint32_t api_version;
    KeelResult (*get_player)(KeelPluginHandle plugin, int32_t slot, KeelPlayerInfo* player);
    KeelResult (*get_next_player)(KeelPluginHandle plugin, int32_t after_slot, KeelPlayerInfo* player);
    KeelResult (*validate_connection)(KeelPluginHandle plugin,
        const KeelPlayerConnection* connection, KeelPlayerInfo* player);
} KeelPlayersApi;

#ifdef __cplusplus
}
#endif

#endif
