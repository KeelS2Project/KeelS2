#ifndef KEELS2_FACTORIES_H
#define KEELS2_FACTORIES_H

#include <keels2/source2.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KEELS2_FACTORIES_SERVICE_NAME "keels2.factories"
#define KEELS2_FACTORIES_API_VERSION 1u
#define KEELS2_FACTORY_OBSERVE 0u
#define KEELS2_FACTORY_REPLACE 1u
#define KEELS2_FACTORY_PROCESS_LIFETIME 1u

typedef uint64_t KeelFactorySubscriptionHandle;

typedef struct KeelFactoryRequest
{
    uint32_t size;
    KeelSource2Factory factory;
    const char* interface_name;
    void* original_result;
    void* current_result;
    int32_t original_return_code;
    int32_t current_return_code;
} KeelFactoryRequest;

typedef struct KeelFactoryResult
{
    uint32_t size;
    int32_t return_code;
    void* instance;
} KeelFactoryResult;

typedef uint32_t (*KeelFactoryCallback)(
    const KeelFactoryRequest* request,
    KeelFactoryResult* replacement,
    void* user_data);

typedef struct KeelFactorySubscriptionSpec
{
    uint32_t size;
    KeelSource2Factory factory;
    const char* interface_name;
    int32_t priority;
    uint32_t flags;
    KeelFactoryCallback callback;
    void* user_data;
} KeelFactorySubscriptionSpec;

typedef struct KeelFactoriesApi
{
    uint32_t size;
    uint32_t api_version;
    KeelResult (*subscribe)(KeelPluginHandle plugin,
        const KeelFactorySubscriptionSpec* spec,
        KeelFactorySubscriptionHandle* subscription);
    KeelResult (*unsubscribe)(KeelPluginHandle plugin,
        KeelFactorySubscriptionHandle subscription);
    KeelResult (*query_original)(KeelPluginHandle plugin,
        KeelSource2Factory factory, const char* interface_name,
        KeelFactoryResult* result);
} KeelFactoriesApi;

#ifdef __cplusplus
}
#endif

#endif
