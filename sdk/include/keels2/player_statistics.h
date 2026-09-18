#ifndef KEELS2_PLAYER_STATISTICS_H
#define KEELS2_PLAYER_STATISTICS_H
#include <keels2/entities.h>

#ifdef __cplusplus
extern "C" {
#endif
#define KEELS2_PLAYER_STATISTICS_SERVICE_NAME "keels2.player_statistics"
#define KEELS2_PLAYER_STATISTICS_API_VERSION 1u
#define KEELS2_PLAYER_STAT_MONEY 0x01u
#define KEELS2_PLAYER_STAT_MATCH_KILLS 0x02u
#define KEELS2_PLAYER_STAT_MATCH_DEATHS 0x04u
#define KEELS2_PLAYER_STAT_MATCH_ASSISTS 0x08u

/* Game-thread operations on an owned, current player controller. A key is one
 * bit, not a mask. Values are signed int32 snapshots; writes require >= 0.
 * MONEY is the current in-game balance; match counters are game-specific.
 * Writes change stored values and notify replication, without simulating
 * transactions, kills, deaths, awards or achievements. Game rules can overwrite
 * them. Failure can follow mutation if notification fails; no rollback. */
typedef struct KeelPlayerStatisticsApi
{
    uint32_t size;
    uint32_t api_version;
    /* Clears both masks on failure; binary support does not imply readiness. */
    KeelResult (*capabilities)(KeelPluginHandle plugin, uint32_t* readable, uint32_t* writable);
    /* Clears value on failure. */
    KeelResult (*read)(KeelPluginHandle plugin, KeelEntityHandle controller, uint32_t key, int32_t* value);
    KeelResult (*write)(KeelPluginHandle plugin, KeelEntityHandle controller, uint32_t key, int32_t value);
} KeelPlayerStatisticsApi;
#ifdef __cplusplus
}
#endif
#endif
