#include <keels2/players.h>

#include <stddef.h>

_Static_assert(sizeof(KeelPlayerConnection) == 16, "connection ABI");
_Static_assert(offsetof(KeelPlayerConnection, generation) == 8, "generation ABI");
_Static_assert(sizeof(KeelPlayerInfo) == 184, "player info ABI");
_Static_assert(offsetof(KeelPlayerInfo, connection) == 16, "connection offset ABI");
_Static_assert(offsetof(KeelPlayerInfo, name) == 48, "player name ABI");
_Static_assert(sizeof(KeelPlayersApi) == 32, "player service ABI");

int main(void)
{
    const KeelPlayersApi api = {0};
    return api.size != 0;
}
