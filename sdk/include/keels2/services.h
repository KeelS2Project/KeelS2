#ifndef KEELS2_SERVICES_H
#define KEELS2_SERVICES_H

#include <keels2/plugin.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KEELS2_SERVICES_SERVICE_NAME "keels2.services"
#define KEELS2_SERVICES_API_VERSION 1u
#define KEELS2_SERVICE_NAME_CAPACITY 128u

typedef uint64_t KeelServiceHandle;

typedef struct KeelServiceSpec
{
    uint32_t size;
    uint32_t version;
    const char* name;
    const void* service;
} KeelServiceSpec;

typedef struct KeelServicesApi
{
    uint32_t size;
    uint32_t api_version;
    KeelResult (*publish)(
        KeelPluginHandle plugin,
        const KeelServiceSpec* spec,
        KeelServiceHandle* publication);
    KeelResult (*withdraw)(KeelPluginHandle plugin, KeelServiceHandle publication);
    KeelResult (*release)(
        KeelPluginHandle plugin,
        const char* name,
        uint32_t version);
} KeelServicesApi;

#ifdef __cplusplus
}
#endif

#endif
