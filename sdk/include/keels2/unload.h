#ifndef KEELS2_UNLOAD_H
#define KEELS2_UNLOAD_H

#include <keels2/plugin.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KEELS2_UNLOAD_SERVICE_NAME "keels2.unload"
#define KEELS2_UNLOAD_API_VERSION 1u

typedef KeelBool (*KeelPrepareUnloadCallback)(void* user_data);

typedef struct KeelUnloadApi
{
    uint32_t size;
    uint32_t api_version;

    KeelResult (*set_prepare_callback)(
        KeelPluginHandle plugin, KeelPrepareUnloadCallback callback, void* user_data);
} KeelUnloadApi;

#ifdef __cplusplus
}
#endif

#endif
