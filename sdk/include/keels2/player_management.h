#ifndef KEELS2_PLAYER_MANAGEMENT_H
#define KEELS2_PLAYER_MANAGEMENT_H

#include <keels2/entities.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KEELS2_PLAYER_MANAGEMENT_SERVICE_NAME "keels2.player_management"
#define KEELS2_PLAYER_MANAGEMENT_API_VERSION 1u
#define KEELS2_PLAYER_MANAGEMENT_RESPAWN 0x01u
#define KEELS2_PLAYER_MANAGEMENT_CHANGE_TEAM 0x02u
#define KEELS2_PLAYER_MANAGEMENT_SWITCH_TEAM 0x04u

/* Exactly one kind, not a mask. Team identifiers and supported operations are
 * adapter-specific. Respawn requires team == 0. Reserved must always be zero.
 * CHANGE_TEAM follows game rules; SWITCH_TEAM uses the direct game operation.
 * These operations can synchronously dispatch engine events. Success means the
 * operation was dispatched, not that an asynchronous transition has completed. */
typedef struct KeelPlayerManagementAction
{
    uint32_t size;
    uint32_t kind;
    int32_t team;
    uint32_t reserved;
} KeelPlayerManagementAction;

typedef struct KeelPlayerManagementApi
{
    uint32_t size;
    uint32_t api_version;
    /* Game-thread only. Writes zero on failure. The returned mask describes
     * binary support; a particular player or map may still be unavailable. */
    KeelResult (*capabilities)(KeelPluginHandle plugin, uint32_t* capabilities);
    /* Controller must be an owned, current entity handle. */
    KeelResult (*apply)(KeelPluginHandle plugin, KeelEntityHandle controller,
        const KeelPlayerManagementAction* action);
} KeelPlayerManagementApi;

#ifdef __cplusplus
}
#endif
#endif
