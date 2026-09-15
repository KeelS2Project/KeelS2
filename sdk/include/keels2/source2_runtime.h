#ifndef KEELS2_SOURCE2_RUNTIME_H
#define KEELS2_SOURCE2_RUNTIME_H

#include <keels2/plugin.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KEELS2_SOURCE2_RUNTIME_SERVICE_NAME "keels2.source2.runtime"
#define KEELS2_SOURCE2_RUNTIME_API_VERSION 1u

typedef struct KeelSource2RuntimeApi
{
    uint32_t size;
    uint32_t api_version;
    KeelResult (*server_command)(KeelPluginHandle plugin, const char* command);
    KeelResult (*client_console_print)(
        KeelPluginHandle plugin,
        int32_t slot,
        const char* message);
    KeelResult (*find_user_message)(
        KeelPluginHandle plugin,
        const char* name,
        uint32_t* message_id);
} KeelSource2RuntimeApi;

#ifdef __cplusplus
}
#endif

#endif
