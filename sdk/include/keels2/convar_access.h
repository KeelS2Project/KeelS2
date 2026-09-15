#ifndef KEELS2_CONVAR_ACCESS_H
#define KEELS2_CONVAR_ACCESS_H

#include <keels2/convar.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KEELS2_CONVAR_ACCESS_SERVICE_NAME "keels2.source2.convar_access"
#define KEELS2_CONVAR_ACCESS_API_VERSION 1u

typedef KeelResult (*KeelConVarAccessCallback)(const void* reference, void* user_data);

typedef struct KeelConVarAccessApi
{
    uint32_t size;
    uint32_t api_version;
    KeelResult (*invoke)(KeelPluginHandle plugin, KeelConVarHandle convar,
        KeelConVarAccessCallback callback, void* user_data);
} KeelConVarAccessApi;

#ifdef __cplusplus
}
#endif

#endif
