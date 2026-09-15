#ifndef KEELS2_PLAYER_INPUT_H
#define KEELS2_PLAYER_INPUT_H

#include <keels2/players.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KEELS2_PLAYER_INPUT_SERVICE_NAME "keels2.player_input"
#define KEELS2_PLAYER_INPUT_API_VERSION 1u
#define KEELS2_BUTTON_FORWARD (UINT64_C(1) << 0)
#define KEELS2_BUTTON_BACK (UINT64_C(1) << 1)
#define KEELS2_BUTTON_LEFT (UINT64_C(1) << 2)
#define KEELS2_BUTTON_RIGHT (UINT64_C(1) << 3)
#define KEELS2_BUTTON_USE (UINT64_C(1) << 4)
#define KEELS2_BUTTON_RELOAD (UINT64_C(1) << 5)
#define KEELS2_BUTTON_JUMP (UINT64_C(1) << 6)
#define KEELS2_BUTTON_DUCK (UINT64_C(1) << 7)
#define KEELS2_BUTTON_ATTACK (UINT64_C(1) << 8)
#define KEELS2_BUTTON_ATTACK2 (UINT64_C(1) << 9)
#define KEELS2_BUTTON_WALK (UINT64_C(1) << 10)
#define KEELS2_BUTTON_SCORE (UINT64_C(1) << 11)
#define KEELS2_BUTTON_ZOOM (UINT64_C(1) << 12)
#define KEELS2_BUTTON_INSPECT (UINT64_C(1) << 13)
#define KEELS2_BUTTON_USE_OR_RELOAD (UINT64_C(1) << 14)
#define KEELS2_BUTTON_ALL ((UINT64_C(1) << 15) - 1)

typedef struct KeelPlayerInput
{
    uint32_t size;
    uint32_t reserved;
    uint64_t buttons;
    uint64_t context;
} KeelPlayerInput;

typedef struct KeelPlayerInputApi
{
    uint32_t size;
    uint32_t api_version;
    KeelResult (*read)(KeelPluginHandle plugin, const KeelPlayerConnection* player, KeelPlayerInput* input);
} KeelPlayerInputApi;

#ifdef __cplusplus
}
#endif
#endif
