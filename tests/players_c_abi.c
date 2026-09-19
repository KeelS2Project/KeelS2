#include <keels2/players.h>

#include <stddef.h>
#include <string.h>

_Static_assert(sizeof(KeelPlayerConnection) == 16, "connection ABI");
_Static_assert(offsetof(KeelPlayerConnection, generation) == 8, "generation ABI");
_Static_assert(sizeof(KeelPlayerInfo) == 184, "player info ABI");
_Static_assert(offsetof(KeelPlayerInfo, connection) == 16, "connection offset ABI");
_Static_assert(offsetof(KeelPlayerInfo, name) == 48, "player name ABI");
_Static_assert(sizeof(KeelPlayersApi) == 32, "player service ABI");

int main(void)
{
    KeelPlayerInfo player = {
        .size = sizeof(KeelPlayerInfo),
        .slot = 3,
        .user_id = 70,
        .flags = KEELS2_PLAYER_CONNECTED | KEELS2_PLAYER_AUTHENTICATED | KEELS2_PLAYER_ALIVE,
        .connection = 17,
        .steam_id = UINT64_C(76561197960265851),
        .controller_handle = 0x12003,
        .pawn_handle = 0x23004,
        .team = 2,
        .reserved = 0,
        .name = "Example player"
    };
    const KeelPlayerConnection saved = {player.slot, 0, player.connection};
    const KeelPlayerInfo copied = player;
    player.name[0] = 'X';
    player.connection = 18;

    if (strcmp(copied.name, "Example player") != 0 || copied.connection != saved.generation ||
        player.connection == saved.generation || saved.reserved != 0) return 1;

    const KeelPlayerInfo connecting = {
        .size = sizeof(KeelPlayerInfo), .slot = 8, .user_id = -1,
        .flags = KEELS2_PLAYER_CONNECTED | KEELS2_PLAYER_CONNECTING | KEELS2_PLAYER_BOT,
        .connection = 19, .controller_handle = UINT32_MAX, .pawn_handle = UINT32_MAX,
        .name = "Connecting bot"
    };
    return connecting.steam_id != 0 || (connecting.flags & KEELS2_PLAYER_AUTHENTICATED) != 0 ||
        (connecting.flags & KEELS2_PLAYER_SOURCE_TV) != 0;
}
