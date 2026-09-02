#include <keels2/source2_runtime.h>

#include <stddef.h>

_Static_assert(sizeof(KeelSource2RuntimeApi) == 32, "runtime API layout changed");
_Static_assert(
    offsetof(KeelSource2RuntimeApi, server_command) == 8,
    "server command offset changed");
_Static_assert(
    offsetof(KeelSource2RuntimeApi, client_console_print) == 16,
    "client console offset changed");
_Static_assert(
    offsetof(KeelSource2RuntimeApi, find_user_message) == 24,
    "user message offset changed");

int main(void)
{
    KeelSource2RuntimeApi api = {
        sizeof(KeelSource2RuntimeApi),
        KEELS2_SOURCE2_RUNTIME_API_VERSION,
        NULL,
        NULL,
        NULL
    };
    return api.size == sizeof(api) ? 0 : 1;
}
