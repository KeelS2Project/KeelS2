#ifndef KEELS2_PAUSE_H
#define KEELS2_PAUSE_H

#include <keels2/plugin.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KEELS2_PAUSE_SERVICE_NAME "keels2.pause"
#define KEELS2_PAUSE_API_VERSION 1u

typedef KeelBool (*KeelPreparePauseCallback)(void* user_data);

typedef struct KeelPauseApi
{
    uint32_t size;
    uint32_t api_version;
    KeelResult (*set_prepare_callback)(
        KeelPluginHandle plugin, KeelPreparePauseCallback callback, void* user_data);
} KeelPauseApi;

#ifdef __cplusplus
}
#endif

#endif
