#include <keels2/factories.h>

#include <stddef.h>

_Static_assert(KEELS2_FACTORIES_API_VERSION == 1, "factory API version");
_Static_assert(sizeof(KeelFactoryRequest) == 40, "request size");
_Static_assert(sizeof(KeelFactoryResult) == 16, "result size");
_Static_assert(sizeof(KeelFactorySubscriptionSpec) == 40, "subscription size");
_Static_assert(sizeof(KeelFactoriesApi) == 32, "API size");
_Static_assert(offsetof(KeelFactoryRequest, original_result) == 16, "original offset");
_Static_assert(offsetof(KeelFactoryRequest, original_return_code) == 32, "code offset");
_Static_assert(offsetof(KeelFactorySubscriptionSpec, callback) == 24, "callback offset");
_Static_assert(offsetof(KeelFactoriesApi, query_original) == 24, "query offset");

int main(void)
{
    return 0;
}
