#include <keels2/source2.h>

#include <stddef.h>

_Static_assert(sizeof(void*) == 8, "pointer size");
_Static_assert(sizeof(KeelSource2InterfaceInfo) == 64, "interface info size");
_Static_assert(sizeof(KeelSource2ApiV1) == 16, "v1 API size");
_Static_assert(sizeof(KeelSource2Api) == 24, "API size");
_Static_assert(KEELS2_SOURCE2_API_VERSION_1 == 1, "v1 API version");
_Static_assert(KEELS2_SOURCE2_API_VERSION == 2, "API version");
_Static_assert(offsetof(KeelSource2InterfaceInfo, instance) == 24, "instance offset");
_Static_assert(
    offsetof(KeelSource2InterfaceInfo, compatibility_profile) == 56,
    "profile offset");
_Static_assert(offsetof(KeelSource2ApiV1, query_interface) == 8, "v1 query offset");
_Static_assert(offsetof(KeelSource2Api, query_interface) == 8, "query offset");
_Static_assert(
    offsetof(KeelSource2Api, query_named_interface) == 16,
    "named query offset");

int main(void)
{
    return 0;
}
