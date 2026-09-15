#ifndef KEELS2_CONVAR_OBSERVE_H
#define KEELS2_CONVAR_OBSERVE_H

#include <keels2/convar.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KEELS2_CONVAR_OBSERVE_SERVICE_NAME "keels2.convar.observe"
#define KEELS2_CONVAR_OBSERVE_API_VERSION 1u

typedef struct KeelConVarObserveApi
{
    uint32_t size;
    uint32_t api_version;
    KeelResult (*observe)(KeelPluginHandle plugin, KeelConVarHandle convar,
        KeelConVarChangeCallback callback, void* user_data);
} KeelConVarObserveApi;

#ifdef __cplusplus
}
#endif

#endif
