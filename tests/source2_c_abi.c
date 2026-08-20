#include <keels2/source2.h>

#include <stddef.h>

_Static_assert(sizeof(void*) == 8, "pointer size");
_Static_assert(sizeof(KeelSource2InterfaceInfo) == 64, "interface info size");
_Static_assert(sizeof(KeelSource2Api) == 16, "API size");
_Static_assert(offsetof(KeelSource2InterfaceInfo, instance) == 24, "instance offset");
_Static_assert(
    offsetof(KeelSource2InterfaceInfo, compatibility_profile) == 56,
    "profile offset");
_Static_assert(offsetof(KeelSource2Api, query_interface) == 8, "query offset");

int main(void)
{
    return 0;
}
