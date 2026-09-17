#ifndef KEELS2_ROUND_CONTROL_H
#define KEELS2_ROUND_CONTROL_H
#include <keels2/plugin.h>

#ifdef __cplusplus
extern "C" {
#endif
#define KEELS2_ROUND_CONTROL_SERVICE_NAME "keels2.round_control"
#define KEELS2_ROUND_CONTROL_API_VERSION 1u
#define KEELS2_ROUND_CONTROL_TERMINATE 0x01u

/* Reason and team identifiers belong to the selected game adapter. Team zero
 * leaves winner selection to the game. Delay must be finite and in [0,3600]
 * seconds; reserved must be zero. This acts on the current map's round. */
typedef struct KeelRoundTermination
{
    uint32_t size;
    uint32_t reason;
    float delay;
    int32_t team;
    uint32_t reserved;
} KeelRoundTermination;

typedef struct KeelRoundControlApi
{
    uint32_t size;
    uint32_t api_version;
    /* Game-thread only; clears output on failure. Binary support does not
     * guarantee that a map/round is currently ready. */
    KeelResult (*capabilities)(KeelPluginHandle plugin, uint32_t* capabilities);
    /* May synchronously dispatch engine callbacks. Success means dispatched;
     * game rules determine the resulting transition. No rollback is promised. */
    KeelResult (*terminate)(KeelPluginHandle plugin, const KeelRoundTermination* request);
} KeelRoundControlApi;
#ifdef __cplusplus
}
#endif
#endif
