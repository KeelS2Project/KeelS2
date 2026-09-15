#ifndef KEELS2_NATIVE_RUNTIME_H
#define KEELS2_NATIVE_RUNTIME_H

#include <keels2/plugin.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KEELS2_NATIVE_RUNTIME_SERVICE_NAME "keels2.source2.native_runtime"
#define KEELS2_NATIVE_RUNTIME_API_VERSION 1u

typedef struct KeelNativeRuntimeApi
{
    uint32_t size;
    uint32_t api_version;
    KeelResult (*check_game_thread)(KeelPluginHandle plugin);
    KeelResult (*client_console_print)(KeelPluginHandle plugin, int32_t slot, const char* text);
    KeelResult (*client_chat_print)(KeelPluginHandle plugin, int32_t slot, const char* text);
    KeelResult (*broadcast_chat)(KeelPluginHandle plugin, const char* text);
} KeelNativeRuntimeApi;

#ifdef __cplusplus
}
#endif

#endif
